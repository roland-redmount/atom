#include "btree/btree.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/tuple.h"
#include "kernel/typedtuple.h"
#include "lang/formula.h"
#include "lang/name.h"
#include "lang/ClauseForm.h"
#include "lang/ConjunctionForm.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "memory/allocator.h"
#include "memory/paging.h"
#include "memory/references.h"
#include "util/hashing.h"
#include "util/sort.h"


/**
 * The registry stores a FormulaRecord for every formula, keyed on the formula
 * hash, and owns the actors tuple of each one. A record is only reachable
 * through peekFormulaRecord(), so a caller never sees one.
 *
 * A record moves within the B-tree when another record is inserted or deleted,
 * so a FormulaRecord pointer must never be held across the creation or release
 * of a formula. The actors tuple is allocated separately from the record, so a
 * tuple pointer handed out by FormulaGetActors() does stay valid; that is what
 * lets a caller keep the tuple while building further formulas from it.
 */
typedef struct s_FormulaRecord {
	data64 hash;
	uint32 nReferences;
	Atom form;
	TypedTuple * actors;
} FormulaRecord;


typedef struct s_FormulaStorage {
	BTree * tree;
	uint32 nReferencesTotal;
} FormulaStorage;


static FormulaStorage * formulaStorage = 0;


size8 FormArity(Atom form)
{
	if(IsPredicateForm(form))
		return PredicateArity(form);
	else if(IsTermForm(form))
		return TermFormArity(form);
	else if(IsClauseForm(form))
		return ClauseArity(form);
	else if(IsConjunctionForm(form))
		return ConjunctionFormArity(form);
	else {
		ASSERT(false);
		return 0;
	}
}


static data64 formulaHash(Atom form, TypedTuple const * actors, data64 initialHash)
{
	data64 hash = DJB2DoubleHashAdd(&(form.hash), sizeof(data64), initialHash);
	return TypedTupleHash(actors, hash);
}


static int8 btreeCompareFormulaRecords(void const * item1, void const * item2, size32 itemSize)
{
	FormulaRecord const * record1 = item1;
	FormulaRecord const * record2 = item2;
	return CompareAtoms((Atom) {.hash = record1->hash}, (Atom) {.hash = record2->hash});
}


static NamedFunction const formulaFunctions[] = {
	{"formula.btreeCompareFormulaRecords", (AnyFunction) btreeCompareFormulaRecords},
};


void RegisterFormulaFunctions(void)
{
	RegisterFunctions(formulaFunctions, sizeof(formulaFunctions) / sizeof(NamedFunction));
}


static FormulaRecord * peekFormulaRecord(data64 hash)
{
	FormulaRecord keyRecord;
	keyRecord.hash = hash;
	return (FormulaRecord *) BTreePeekItem(formulaStorage->tree, &keyRecord);
}


void InitializeFormulaStorage(void)
{
	formulaStorage = Allocate(sizeof(FormulaStorage));
	SetPersistentState(STATE_KEY_FORMULAS, formulaStorage);
	// Create the B-tree. No freeItem() callback used here; a FormulaRecord is taken apart by
	// ReleaseFormula() before it is deleted; see ReleaseFormula().
	formulaStorage->tree = BTreeCreate(sizeof(FormulaRecord), btreeCompareFormulaRecords, 0);
	formulaStorage->nReferencesTotal = 0;
}


void RestoreFormulaStorage(void)
{
	formulaStorage = GetPersistentState(STATE_KEY_FORMULAS);
	ASSERT(formulaStorage)
}


void FreeFormulaStorage(void)
{
	ASSERT(NumberOfFormulas() == 0)
	ASSERT(formulaStorage->nReferencesTotal == 0)
	BTreeFree(formulaStorage->tree);
	Free(formulaStorage);
	SetPersistentState(STATE_KEY_FORMULAS, 0);
	formulaStorage = 0;
}


size32 NumberOfFormulas(void)
{
	return BTreeNItems(formulaStorage->tree);
}


uint32 FormulaTotalReferenceCount(void)
{
	return formulaStorage->nReferencesTotal;
}


static bool sameFormula(FormulaRecord const * record, Atom form, TypedTuple const * actors)
{
	return SameAtoms(record->form, form) && TypedTupleEqual(record->actors, actors);
}


/*
 * CLAUDE: Sort the actors of one term or predicate within each role, by CompareTypedAtoms().
 * The actors start at the given offset of the permutation array, which holds indices into
 * the actors array; see GetCanonicalActorOrder(). The insertion sort is stable.
 */
static void sortActorsWithinRoles(
	Atom predicateForm, TypedAtom const actors[], index8 * permutation, index8 offset)
{
	size8 arity = PredicateArity(predicateForm);
	index8 roleIndices[arity];
	PredicateFormGetActorRoles(predicateForm, roleIndices);
	index8 * order = permutation + offset;
	for(index8 i = 1; i < arity; i++) {
		for(index8 j = i; (j > 0) && (roleIndices[j - 1] == roleIndices[j])
			&& (CompareTypedAtoms(actors[order[j - 1]], actors[order[j]]) > 0); j--)
		{
			index8 swap = order[j - 1];
			order[j - 1] = order[j];
			order[j] = swap;
		}
	}
}


/*
 * CLAUDE: Compare two terms of one term form by their actors, as TypedTupleCompare() does.
 * Each term is given by the offset of its first actor in the permutation array.
 */
static int8 compareTermActors(
	TypedAtom const actors[], index8 const * permutation, index8 offset1, index8 offset2, size8 arity)
{
	for(index8 i = 0; i < arity; i++) {
		int8 order = CompareTypedAtoms(
			actors[permutation[offset1 + i]], actors[permutation[offset2 + i]]);
		if(order)
			return order;
	}
	return 0;
}


/*
 * CLAUDE: Sort the terms of one term form of a clause or conjunction by their actors. The
 * terms are adjacent, and the first starts at the given offset of the permutation array.
 * The insertion sort is stable.
 */
static void sortTermsOfForm(
	TypedAtom const actors[], index8 * permutation, index8 offset, size8 termArity, size8 nTerms)
{
	index8 swap[termArity];
	for(index8 i = 1; i < nTerms; i++) {
		for(index8 j = i; (j > 0) && (compareTermActors(actors, permutation,
			offset + (j - 1) * termArity, offset + j * termArity, termArity) > 0); j--)
		{
			index8 * term1 = permutation + offset + (j - 1) * termArity;
			index8 * term2 = permutation + offset + j * termArity;
			CopyMemory(term1, swap, termArity * sizeof(index8));
			CopyMemory(term2, term1, termArity * sizeof(index8));
			CopyMemory(swap, term2, termArity * sizeof(index8));
		}
	}
}


void GetCanonicalActorOrder(Atom form, TypedAtom const actors[], index8 * permutation)
{
	size8 arity = FormArity(form);
	for(index8 i = 0; i < arity; i++)
		permutation[i] = i;
	if(IsPredicateForm(form)) {
		sortActorsWithinRoles(form, actors, permutation, 0);
		return;
	}
	if(IsTermForm(form)) {
		sortActorsWithinRoles(TermFormGetPredicateForm(form), actors, permutation, 0);
		return;
	}
	// CLAUDE: The terms of each term form of a clause or conjunction are adjacent
	ASSERT(IsClauseForm(form) || IsConjunctionForm(form))
	MultisetIterator iterator;
	MultisetIterate(form, AT_ID, &iterator);
	index8 offset = 0;
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		Atom predicateForm = TermFormGetPredicateForm(em.element);
		size8 termArity = TermFormArity(em.element);
		for(index8 m = 0; m < em.multiple; m++)
			sortActorsWithinRoles(predicateForm, actors, permutation, offset + m * termArity);
		sortTermsOfForm(actors, permutation, offset, termArity, em.multiple);
		offset += em.multiple * termArity;
	}
	MultisetIteratorEnd(&iterator);
}


/*
 * Put the actors of a formula of the given form in canonical order, in place.
 */
static void putActorsInCanonicalOrder(Atom form, TypedTuple * actors)
{
	size8 arity = actors->nAtoms;
	// CLAUDE: Without actors there is nothing to order. Returning here also keeps an
	// optimized build from reading the arrays below as regions of size 0.
	if(arity == 0)
		return;
	TypedAtom elements[arity];
	for(index8 i = 0; i < arity; i++)
		elements[i] = TypedTupleGetElement(actors, i);
	index8 permutation[arity];
	GetCanonicalActorOrder(form, elements, permutation);
	if(IsIdentityPermutation(permutation, arity))
		return;
	// CLAUDE: The tuple holds a reference to every element, so the elements stay valid while
	// they are written back in another order
	for(index8 i = 0; i < arity; i++)
		AcquireTypedAtom(elements[i]);
	for(index8 i = 0; i < arity; i++)
		TypedTupleSetElement(actors, i, elements[permutation[i]]);
	for(index8 i = 0; i < arity; i++)
		ReleaseTypedAtom(elements[i]);
}


/**
 * Find or create the formula with the given form and actors. The actors tuple
 * is adopted by the registry if the formula is new, and freed if it is not.
 */
static Atom internFormula(Atom form, TypedTuple * actors)
{
	// CLAUDE: A formula is identified by its actors in canonical order
	putActorsInCanonicalOrder(form, actors);
	data64 hash = formulaHash(form, actors, djb2InitialHash);
	FormulaRecord * existingRecord = peekFormulaRecord(hash);
	if(existingRecord) {
		// A formula with the same hash exists.
		// Check for hash collision
		if(!sameFormula(existingRecord, form, actors)) {
			PrintCString("Hash collision between formulas ");
			PrintFormActorsAsFormula(form, actors, 0);
			PrintCString(" and ");
			PrintFormActorsAsFormula(existingRecord->form, existingRecord->actors, 0);
			PrintChar('\n');
			Panic("Hash collision for formulas, hash = %llx", hash);
		}
		existingRecord->nReferences++;
		FreeTypedTuple(actors);
	}
	else {
		// create new formula
		FormulaRecord record;
		record.hash = hash;
		record.nReferences = 1;
		record.form = form;
		record.actors = actors;
		IFactAcquire(form);
		ASSERT(BTreeInsert(formulaStorage->tree, &record) == BTREE_INSERTED)
	}
	formulaStorage->nReferencesTotal++;
	return (Atom) {.hash = hash};
}


Atom CreateFormula(Atom form, TypedTuple const * actors)
{
	return internFormula(form, CreateTupleFromTuple(actors));
}


Atom CreateFormulaFromArray(Atom form, TypedAtom const actors[])
{
	return internFormula(form, CreateTypedTupleFromArray(actors, FormArity(form)));
}


void AcquireFormula(Atom formula)
{
	FormulaRecord * record = peekFormulaRecord(formula.hash);
	ASSERT(record)
	record->nReferences++;
	formulaStorage->nReferencesTotal++;
}


void ReleaseFormula(Atom formula)
{
	FormulaRecord * record = peekFormulaRecord(formula.hash);
	ASSERT(record)
	ASSERT(record->nReferences > 0)
	ASSERT(formulaStorage->nReferencesTotal > 0)

	record->nReferences--;
	formulaStorage->nReferencesTotal--;

	if(record->nReferences == 0) {
		// Copy the record and remove it from the registry before taking it apart.
		// Releasing the form retracts its defining facts, and releasing an actor
		// that is itself a formula re-enters this function, so neither may find
		// the formula being released still in the B-tree.
		FormulaRecord recordCopy = *record;
		ASSERT(BTreeDelete(formulaStorage->tree, &recordCopy, 0) == BTREE_DELETED)
		IFactRelease(recordCopy.form);
		FreeTypedTuple(recordCopy.actors);
	}
}


Atom FormulaGetForm(Atom formula)
{
	FormulaRecord const * record = peekFormulaRecord(formula.hash);
	ASSERT(record)
	return record->form;
}


TypedTuple const * FormulaGetActors(Atom formula)
{
	FormulaRecord const * record = peekFormulaRecord(formula.hash);
	ASSERT(record)
	return record->actors;
}


FormulaView FormulaGetView(Atom formula)
{
	FormulaRecord const * record = peekFormulaRecord(formula.hash);
	ASSERT(record)
	return (FormulaView) {.form = record->form, .actors = record->actors};
}


FormulaView CreateFormulaView(Atom form, TypedTuple * actors)
{
	putActorsInCanonicalOrder(form, actors);
	return (FormulaView) {.form = form, .actors = actors};
}


void FormulaDump(void)
{
	PrintF("Formula table %u formulas:\n", NumberOfFormulas());

	BTreeIterator iterator;
	BTreeIterate(&iterator, formulaStorage->tree);
	while(BTreeIteratorNext(&iterator)) {
		FormulaRecord const * record = BTreeIteratorPeekItem(&iterator);
		PrintF("%llx (%llu) ", record->hash, record->hash);
		PrintFormActorsAsFormula(record->form, record->actors, 0);
		PrintF(" %u references\n", record->nReferences);
	}
	BTreeIteratorEnd(&iterator);
}


bool FormulaIsPredicate(Atom formula)
{
	return IsPredicateForm(FormulaGetForm(formula));
}


bool FormulaIsTerm(Atom formula)
{
	return IsTermForm(FormulaGetForm(formula));
}


bool FormulaIsClause(Atom formula)
{
	return IsClauseForm(FormulaGetForm(formula));
}


bool FormulaIsConjunction(Atom formula)
{
	return IsConjunctionForm(FormulaGetForm(formula));
}


index32 FormulaRoleIndex(Atom formula, Atom roleName)
{
	// TODO: currently this only supports predicates.
	// Need to implement GetClauseRoleIndex() &c
	ASSERT(FormulaIsPredicate(formula))
	return PredicateRoleIndex(FormulaGetForm(formula), roleName);
}


Atom CreatePredicate(Atom const roleNames[], TypedAtom actors[], size8 arity)
{
	Atom predicateForm = CreatePredicateForm(roleNames, arity);

	index8 roleOrder[arity];
	MultisetIterationOrder(predicateForm, AT_NAME, roleNames, roleOrder, arity);

	TypedAtom actorsOrdered[arity];
	CopyMemory(actors, actorsOrdered, arity * sizeof(TypedAtom));
	ReorderArray(actorsOrdered, roleOrder, arity, sizeof(TypedAtom));

	Atom predicate = CreateFormulaFromArray(predicateForm, actorsOrdered);
	IFactRelease(predicateForm);
	return predicate;
}


Atom CreateTerm(Atom predicate, bool sign)
{
	ASSERT(FormulaIsPredicate(predicate));
	Atom termForm = CreateTermForm(FormulaGetForm(predicate), sign);

	Atom term = CreateFormula(termForm, FormulaGetActors(predicate));
	IFactRelease(termForm);
	return term;
}


Atom TermGetRoleActor(Atom termForm, Atom const termActors[], const char * role, uint8 m)
{
	ASSERT(m > 0)
	Atom predicateForm = TermFormGetPredicateForm(termForm);
	Atom roleName = CreateNameFromCString(role);
	index8 actorIndex = PredicateRoleIndex(predicateForm, roleName) + (m - 1);
	NameRelease(roleName);
	return termActors[actorIndex];
}


/* CLAUDE: Create a clause or a conjunction from its terms. The two differ only
   in the form they build, so createForm is CreateClauseForm or CreateConjunctionForm. */
static Atom createTermMultiset(
	Atom const terms[], size8 nTerms, Atom (* createForm)(Atom const termForms[], size8 nTermForms),
	index8 termOrder[])
{
	// a term multiset without terms is meaningless, and would give zero length arrays below
	ASSERT(nTerms > 0);
	// All terms must be unique
	ASSERT(!TupleContainsDuplicates(terms, nTerms))

	// Take a view of every term before building the form, so that the terms
	// are read with one registry lookup each
	FormulaView termViews[nTerms];
	for(index8 i = 0; i < nTerms; i++)
		termViews[i] = FormulaGetView(terms[i]);

	// collect term forms and their arities
	Atom termForms[nTerms];
	size8 termArities[nTerms];
	size8 arity = 0;
	for(index8 i = 0; i < nTerms; i++) {
		termForms[i] = termViews[i].form;
		termArities[i] = termViews[i].actors->nAtoms;
		ASSERT(arity < 255 - termArities[i]);
		arity += termArities[i];
	}
	Atom form = createForm(termForms, nTerms);

	// Collect actors from terms into a single array
	TypedAtom actors[arity];
	for(index8 i = 0, k = 0; i < nTerms; i++) {
		for(index8 j = 0; j < termArities[i]; j++)
			actors[k++] = TypedTupleGetElement(termViews[i].actors, j);
	}

	// reorder actors to match the name order of the form
	index8 order[nTerms];
	// find ordering
	MultisetIterationOrder(form, AT_ID, termForms, order, nTerms);
	// reorder actors
	size32 blockSizes[nTerms];
	for(index8 i = 0; i < nTerms; i++)
		blockSizes[i] = termArities[i] * sizeof(TypedAtom);
	ReorderRaggedArray(actors, order, blockSizes, nTerms);

	// CLAUDE: Sort the terms of each term form; see GetCanonicalActorOrder(). A term moves as a
	// whole to the position of another term of its form, which starts at the same offset as
	// the term of that form moved there.
	index8 permutation[arity];
	GetCanonicalActorOrder(form, actors, permutation);
	TypedAtom canonicalActors[arity];
	for(index8 i = 0; i < arity; i++)
		canonicalActors[i] = actors[permutation[i]];
	if(termOrder) {
		index8 termOffsets[nTerms];
		for(index8 i = 0, offset = 0; i < nTerms; i++) {
			termOffsets[i] = offset;
			offset += termArities[order[i]];
		}
		for(index8 i = 0; i < nTerms; i++) {
			index8 sourceOffset = permutation[termOffsets[i]];
			index8 source = 0;
			while(termOffsets[source] != sourceOffset)
				source++;
			termOrder[i] = order[source];
		}
	}

	Atom formula = CreateFormulaFromArray(form, canonicalActors);
	IFactRelease(form);
	return formula;
}


Atom CreateClause(Atom const terms[], size8 nTerms, index8 termOrder[])
{
	return createTermMultiset(terms, nTerms, CreateClauseForm, termOrder);
}


Atom CreateConjunction(Atom const terms[], size8 nTerms, index8 termOrder[])
{
	return createTermMultiset(terms, nTerms, CreateConjunctionForm, termOrder);
}


index8 ClauseGetTermIndex(Atom clauseForm, Atom termForm, uint8 m)
{
	// iterate over terms in the clause to compoute the index
	MultisetIterator iterator;
	MultisetIterate(clauseForm, AT_ID, &iterator);

	index8 index = 0;
	bool found = false;
	ElementMultiple elementMultiple;
	while(MultisetIteratorNext(&iterator)) {
		elementMultiple = MultisetIteratorGetElement(&iterator);
		if(SameAtoms(elementMultiple.element, termForm)) {
			found = true;
			break;
		}
		index += elementMultiple.multiple;
	}
	MultisetIteratorEnd(&iterator);
	ASSERT(found);
	// Select the k'th occurence of the term form
	// (all terms of the same form must be contiguous in the clause form)
	ASSERT((m > 0) && (m <= elementMultiple.multiple))
	index += m - 1;
	return index;	
}


index8 ClauseGetTermActorsIndex(Atom clauseForm, Atom termForm, uint8 m)
{
	// iterate over terms in the clause to compoute the index
	MultisetIterator iterator;
	MultisetIterate(clauseForm, AT_ID, &iterator);

	index8 index = 0;
	bool found = false;
	ElementMultiple elementMultiple;
	while(MultisetIteratorNext(&iterator)) {
		elementMultiple = MultisetIteratorGetElement(&iterator);
		if(SameAtoms(elementMultiple.element, termForm)) {
			found = true;
			break;
		}
		size8 termArity = TermFormArity(elementMultiple.element);
		index += elementMultiple.multiple * termArity;
	}
	MultisetIteratorEnd(&iterator);
	ASSERT(found);
	// Select the k'th occurence of the term form
	// (all terms of the same form must be contiguous in the clause form)
	ASSERT((m > 0) && (m <= elementMultiple.multiple))
	size8 termArity = FormArity(termForm);
	index += (m - 1) * termArity;
	return index;
}


uint8 FormulaArity(Atom formula)
{
	return FormArity(FormulaGetForm(formula));
}


/**
 * Print a predicate.
 * The actors of the predicate begin at index actorsOffset of the actors tuple.
 * Unless roleOrder is 0, the roles are printed in that order; see FormOrdering.
 */
static void printPredicate(
	Atom predicateForm, TypedTuple const * actors, index8 actorsOffset, index8 const roleOrder[])
{
	// CLAUDE: collect the role names, so that a role can be read by its index in the form
	size8 arity = PredicateArity(predicateForm);
	Atom roleNames[arity];
	MultisetIterator iterator;
	MultisetIterate(predicateForm, AT_NAME, &iterator);
	index8 roleIndex = 0;
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < em.multiple; j++)
			roleNames[roleIndex++] = em.element;
	}
	MultisetIteratorEnd(&iterator);

	index8 enteredOrder[arity];
	if(roleOrder)
		InvertPermutation(roleOrder, enteredOrder, arity);
	for(index8 i = 0; i < arity; i++) {
		index8 k = roleOrder ? enteredOrder[i] : i;
		PrintName(roleNames[k]);
		PrintChar(' ');
		PrintTypedAtom(TypedTupleGetElement(actors, actorsOffset + k));
		if(i < arity - 1)
			PrintChar(' ');
	}
}


static void printTerm(
	Atom termForm, TypedTuple const * actors, index8 actorsOffset, index8 const roleOrder[])
{
	bool sign = TermFormGetSign(termForm);
	if(!sign) {
		PrintChar('!');
		PrintChar(' ');
	}
	printPredicate(TermFormGetPredicateForm(termForm), actors, actorsOffset, roleOrder);
}


/*
 * Print the terms of a clause or conjunction form, separated by the connective.
 * Unless ordering is 0, the terms and roles are printed in that order.
 */
static void printTerms(
	Atom form, TypedTuple const * actors, FormOrdering const * ordering, char const * connective)
{
	// CLAUDE: collect the term forms and the index of the first actor of each term,
	// so that a term can be read by its index in the form
	size8 nTerms = 0;
	Atom termForms[FormArity(form)];
	index8 actorsOffsets[FormArity(form)];
	index8 actorsOffset = 0;
	MultisetIterator iterator;
	MultisetIterate(form, AT_ID, &iterator);
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			termForms[nTerms] = em.element;
			actorsOffsets[nTerms] = actorsOffset;
			actorsOffset += TermFormArity(em.element);
			nTerms++;
		}
	}
	MultisetIteratorEnd(&iterator);

	index8 enteredOrder[nTerms];
	if(ordering) {
		ASSERT(ordering->nTerms == nTerms)
		InvertPermutation(ordering->termOrder, enteredOrder, nTerms);
	}
	for(index8 i = 0; i < nTerms; i++) {
		index8 k = ordering ? enteredOrder[i] : i;
		index8 const * roleOrder = ordering ? ordering->roleOrder + actorsOffsets[k] : 0;
		printTerm(termForms[k], actors, actorsOffsets[k], roleOrder);
		if(i < nTerms - 1)
			PrintCString(connective);
	}
}


/**
 * Traverse and print a formula
 */
void PrintFormula(Atom formula)
{
	PrintFormulaView(FormulaGetView(formula));
}


void PrintFormulaView(FormulaView formulaView)
{
	PrintFormActorsAsFormula(formulaView.form, formulaView.actors, 0);
}


void PrintFormActorsAsFormula(Atom form, TypedTuple const * actors, FormOrdering const * ordering)
{
	index8 const * roleOrder = ordering ? ordering->roleOrder : 0;
	if(IsPredicateForm(form))
		printPredicate(form, actors, 0, roleOrder);
	else if(IsTermForm(form))
		printTerm(form, actors, 0, roleOrder);
	else if(IsClauseForm(form))
		printTerms(form, actors, ordering, " | ");
	else if(IsConjunctionForm(form))
		printTerms(form, actors, ordering, " & ");
	else
		ASSERT(false);
}


void FormOrderingGetEnteredActors(Atom form, FormOrdering const * ordering, index8 actorIndices[])
{
	if(IsPredicateForm(form) || IsTermForm(form)) {
		for(index8 j = 0; j < FormArity(form); j++)
			actorIndices[ordering->roleOrder[j]] = j;
		return;
	}

	// CLAUDE: the arity of each term, with terms in the order of the form
	size8 nTerms = 0;
	size8 termArities[FormArity(form)];
	MultisetIterator iterator;
	MultisetIterate(form, AT_ID, &iterator);
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < em.multiple; j++)
			termArities[nTerms++] = TermFormArity(em.element);
	}
	MultisetIteratorEnd(&iterator);
	ASSERT(ordering->nTerms == nTerms)

	// CLAUDE: the entered index of the first actor of each term, with terms in the order entered
	size8 enteredArities[nTerms];
	for(index8 i = 0; i < nTerms; i++)
		enteredArities[ordering->termOrder[i]] = termArities[i];
	index8 enteredOffsets[nTerms];
	index8 enteredOffset = 0;
	for(index8 i = 0; i < nTerms; i++) {
		enteredOffsets[i] = enteredOffset;
		enteredOffset += enteredArities[i];
	}

	index8 actorsOffset = 0;
	for(index8 i = 0; i < nTerms; i++) {
		for(index8 j = 0; j < termArities[i]; j++) {
			index8 actorIndex = actorsOffset + j;
			actorIndices[enteredOffsets[ordering->termOrder[i]] + ordering->roleOrder[actorIndex]] = actorIndex;
		}
		actorsOffset += termArities[i];
	}
}


void PrintForm(Atom form)
{
	if(IsTermForm(form))
		PrintTermForm(form);
	else if(IsClauseForm(form))
		PrintClauseForm(form);
	else if(IsConjunctionForm(form))
		PrintConjunctionForm(form);
	else
		ASSERT(false);
}
