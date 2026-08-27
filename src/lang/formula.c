#include "btree/btree.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/typedtuple.h"
#include "lang/formula.h"
#include "lang/name.h"
#include "lang/ClauseForm.h"
#include "lang/ConjunctionForm.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "memory/allocator.h"
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


static struct {
	BTree * tree;
	uint32 nReferencesTotal;
} formulaStorage;


static size8 FormArity(Atom form)
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


static FormulaRecord * peekFormulaRecord(data64 hash)
{
	FormulaRecord keyRecord;
	keyRecord.hash = hash;
	return (FormulaRecord *) BTreePeekItem(formulaStorage.tree, &keyRecord);
}


void InitializeFormulaStorage(void)
{
	// Create the B-tree. No freeItem() callback used here; a FormulaRecord is taken apart by
	// ReleaseFormula() before it is deleted; see ReleaseFormula().
	formulaStorage.tree = BTreeCreate(sizeof(FormulaRecord), btreeCompareFormulaRecords, 0);
	formulaStorage.nReferencesTotal = 0;
}


void FreeFormulaStorage(void)
{
	ASSERT(NumberOfFormulas() == 0)
	ASSERT(formulaStorage.nReferencesTotal == 0)
	BTreeFree(formulaStorage.tree);
}


size32 NumberOfFormulas(void)
{
	return BTreeNItems(formulaStorage.tree);
}


uint32 FormulaTotalReferenceCount(void)
{
	return formulaStorage.nReferencesTotal;
}


static bool sameFormula(FormulaRecord const * record, Atom form, TypedTuple const * actors)
{
	return SameAtoms(record->form, form) && TypedTupleEqual(record->actors, actors);
}


/**
 * Find or create the formula with the given form and actors. The actors tuple
 * is adopted by the registry if the formula is new, and freed if it is not.
 */
static Atom internFormula(Atom form, TypedTuple * actors)
{
	data64 hash = formulaHash(form, actors, djb2InitialHash);
	FormulaRecord * existingRecord = peekFormulaRecord(hash);
	if(existingRecord) {
		// A formula with the same hash exists.
		// Check for hash collision
		if(!sameFormula(existingRecord, form, actors)) {
			PrintCString("Hash collision between formulas ");
			PrintFormActorsAsFormula(form, actors);
			PrintCString(" and ");
			PrintFormActorsAsFormula(existingRecord->form, existingRecord->actors);
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
		TypedTupleAcquireElements(actors);
		ASSERT(BTreeInsert(formulaStorage.tree, &record) == BTREE_INSERTED)
	}
	formulaStorage.nReferencesTotal++;
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
	formulaStorage.nReferencesTotal++;
}


void ReleaseFormula(Atom formula)
{
	FormulaRecord * record = peekFormulaRecord(formula.hash);
	ASSERT(record)
	ASSERT(record->nReferences > 0)
	ASSERT(formulaStorage.nReferencesTotal > 0)

	record->nReferences--;
	formulaStorage.nReferencesTotal--;

	if(record->nReferences == 0) {
		// Copy the record and remove it from the registry before taking it apart.
		// Releasing the form retracts its defining facts, and releasing an actor
		// that is itself a formula re-enters this function, so neither may find
		// the formula being released still in the B-tree.
		FormulaRecord recordCopy = *record;
		ASSERT(BTreeDelete(formulaStorage.tree, &recordCopy, 0) == BTREE_DELETED)
		IFactRelease(recordCopy.form);
		TypedTupleReleaseElements(recordCopy.actors);
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


void FormulaDump(void)
{
	PrintF("Formula table %u formulas:\n", NumberOfFormulas());

	BTreeIterator iterator;
	BTreeIterate(&iterator, formulaStorage.tree);
	while(BTreeIteratorNext(&iterator)) {
		FormulaRecord const * record = BTreeIteratorPeekItem(&iterator);
		PrintF("%llx (%llu) ", record->hash, record->hash);
		PrintFormActorsAsFormula(record->form, record->actors);
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


/**
 * Returhs true if the formulas array contains repeated formula atoms.
 */
static bool FormulasRepeat(Atom const formulas[], size8 nFormulas)
{
	for(index8 i = 1; i < nFormulas; i++)
		for(index8 j = 0; j < i; j++)
			if(SameAtoms(formulas[i], formulas[j]))
				return true;
	return false;
}


Atom CreateClause(Atom const terms[], size8 nTerms)
{
	// a clause without terms is meaningless, and would give zero length arrays below
	ASSERT(nTerms > 0);
	// All terms must be unique
	ASSERT(!FormulasRepeat(terms, nTerms))

	// Take a view of every term before building the clause, so that the terms
	// are read with one registry lookup each
	FormulaView termViews[nTerms];
	for(index8 i = 0; i < nTerms; i++)
		termViews[i] = FormulaGetView(terms[i]);

	// collect term forms and their arities
	Atom termForms[nTerms];
	size8 termArities[nTerms];
	size8 clauseArity = 0;
	for(index8 i = 0; i < nTerms; i++) {
		termForms[i] = termViews[i].form;
		termArities[i] = termViews[i].actors->nAtoms;
		ASSERT(clauseArity < 255 - termArities[i]);
		clauseArity += termArities[i];
	}
	Atom clauseForm = CreateClauseForm(termForms, nTerms);

	// Collect actors from terms into a single array
	TypedAtom actors[clauseArity];
	for(index8 i = 0, k = 0; i < nTerms; i++) {
		for(index8 j = 0; j < termArities[i]; j++)
			actors[k++] = TypedTupleGetElement(termViews[i].actors, j);
	}

	// reorder actors to match the name order of clauseForm
	index8 termOrder[nTerms]; 
	// find ordering
	MultisetIterationOrder(clauseForm, AT_ID, termForms, termOrder, nTerms);
	// reorder actors
	size32 blockSizes[nTerms];
	for(index8 i = 0; i < nTerms; i++)
		blockSizes[i] = termArities[i] * sizeof(TypedAtom);
	ReorderRaggedArray(actors, termOrder, blockSizes, nTerms);

	Atom clause = CreateFormulaFromArray(clauseForm, actors);
	IFactRelease(clauseForm);
	return clause;
}


// NOTE: this is very similar to CreateClause, could be refactored
Atom CreateConjunction(Atom const clauses[], size8 nClauses)
{
	// as in CreateClause(), a conjunction without clauses is meaningless
	ASSERT(nClauses > 0);
	// All clauses must be unique
	ASSERT(!FormulasRepeat(clauses, nClauses))

	// Take a view of every clause before building the conjunction, so that the
	// clauses are read with one registry lookup each
	FormulaView clauseViews[nClauses];
	for(index8 i = 0; i < nClauses; i++)
		clauseViews[i] = FormulaGetView(clauses[i]);

	// collect clause forms and their arities
	Atom clauseForms[nClauses];
	size8 clauseArities[nClauses];
	size8 conjunctionArity = 0;
	for(index8 i = 0; i < nClauses; i++) {
		clauseForms[i] = clauseViews[i].form;
		clauseArities[i] = clauseViews[i].actors->nAtoms;
		ASSERT(conjunctionArity < 255 - clauseArities[i]);
		conjunctionArity += clauseArities[i];
	}
	Atom conjunctionForm = CreateConjunctionForm(clauseForms, nClauses);

	// collect actors from terms into a single array
	TypedAtom actors[conjunctionArity];
	for(index8 i = 0, k = 0; i < nClauses; i++) {
		for(index8 j = 0; j < clauseArities[i]; j++)
			actors[k++] = TypedTupleGetElement(clauseViews[i].actors, j);
	}

	// reorder actors to match the name order of clauseForm
	index8 clauseOrder[nClauses]; 
	// find ordering
	MultisetIterationOrder(conjunctionForm, AT_ID, clauseForms, clauseOrder, nClauses);
	// reorder actors
	size32 blockSizes[nClauses];
	for(index8 i = 0; i < nClauses; i++)
		blockSizes[i] = clauseArities[i] * sizeof(TypedAtom);
	ReorderRaggedArray(actors, clauseOrder, blockSizes, nClauses);

	Atom conjunction = CreateFormulaFromArray(conjunctionForm, actors);
	IFactRelease(conjunctionForm);
	return conjunction;
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


void ClauseGetTermActorsIndices(Atom clauseForm, index8 termActorsIndices[])
{
	// iterate over terms in the clause and compute indices
	MultisetIterator iterator;
	MultisetIterate(clauseForm, AT_ID, &iterator);

	index8 k = 0;
	termActorsIndices[k] = 0;
	ElementMultiple elementMultiple;
	while(MultisetIteratorNext(&iterator)) {
		elementMultiple = MultisetIteratorGetElement(&iterator);
		size8 termArity = TermFormArity(elementMultiple.element);
		for(index8 i = 0; i < elementMultiple.multiple; i++) {
			termActorsIndices[k + 1] = termActorsIndices[k] + termArity;
			k++;
		}
	}
	MultisetIteratorEnd(&iterator);
}


uint8 FormulaArity(Atom formula)
{
	return FormArity(FormulaGetForm(formula));
}


/**
 * Print a predicate with actors in the order given by atomIndex
 */
static void printPredicate(Atom predicateForm, TypedTuple const * actors, index8 * atomIndex)
{	
	MultisetIterator iterator;
	MultisetIterate(predicateForm, AT_NAME, &iterator);

	size8 nRoles = PredicateNRoles(predicateForm);
	for(index8 i = 0; i < nRoles; i++) {	
		ASSERT(MultisetIteratorNext(&iterator))
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			PrintName(em.element);
			PrintChar(' ');
			PrintTypedAtom(TypedTupleGetElement(actors, *atomIndex));
			if((i < nRoles - 1) || (j < em.multiple - 1))
				PrintChar(' ');
			(*atomIndex)++;
		}
	}
	MultisetIteratorEnd(&iterator);
}


static void printTerm(Atom termForm, TypedTuple const * actors, index8 * atomIndex)
{
	bool sign = TermFormGetSign(termForm);
	if(!sign) {
		PrintChar('!');
		PrintChar(' ');
	}
	printPredicate(TermFormGetPredicateForm(termForm), actors, atomIndex);
}


static void printClause(Atom clauseForm, TypedTuple const * actors, index8 * atomIndex)
{	
	MultisetIterator iterator;
	MultisetIterate(clauseForm, AT_ID, &iterator);

	size8 nTermForms = ClauseFormNTermForms(clauseForm);
	for(index8 i = 0; i < nTermForms; i++) {	
		ASSERT(MultisetIteratorNext(&iterator))
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			printTerm(em.element, actors, atomIndex);
			if((j < em.multiple - 1) || (i < nTermForms - 1))
				PrintCString(" | ");
		}
	}
	MultisetIteratorEnd(&iterator);
}


static void printConjunction(Atom conjunctionForm, TypedTuple const * actors, index8* atomIndex)
{
	MultisetIterator iterator;
	MultisetIterate(conjunctionForm, AT_ID, &iterator);

	size8 nClauseForms = ConjunctionFormNUniqueClauseForms(conjunctionForm);
	for(index8 i = 0; i < nClauseForms; i++) {	
		ASSERT(MultisetIteratorNext(&iterator))
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			printClause(em.element, actors, atomIndex);
			if((j < em.multiple - 1) || (i < nClauseForms - 1))
				PrintCString(" & ");
		}
	}
	MultisetIteratorEnd(&iterator);
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
	PrintFormActorsAsFormula(formulaView.form, formulaView.actors);
}


void PrintFormActorsAsFormula(Atom form, TypedTuple const * actors)
{
	index8 atomIndex = 0;
	if(IsPredicateForm(form))
		printPredicate(form, actors, &atomIndex);
	else if(IsTermForm(form))
		printTerm(form, actors, &atomIndex);
	else if(IsClauseForm(form))
		printClause(form, actors, &atomIndex);
	else if(IsConjunctionForm(form))
		printConjunction(form, actors, &atomIndex);
	else
		ASSERT(false);
}
