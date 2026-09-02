
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/multiset.h"
#include "kernel/Relation.h"
#include "kernel/RelationRegistry.h"
#include "kernel/RelationTable.h"
#include "lang/ClauseForm.h"
#include "lang/ConjunctionForm.h"
#include "lang/TermForm.h"
#include "storage/RelationBTree.h"
#include "ui/assert.h"
#include "ui/query.h"
#include "util/ResizingArray.h"

/**
 * Test whether the given term interpreted as a fact contradicts the current knowledgebase,
 * that is, whether the negation of the term is entailed by the knowledgebase.
 * The actors tuple cannot contain a variable. Returns true if there is a contradiction.
 */
static bool checkContradiction(FormulaView fact)
{
	// Run a query for the negated term.
	Atom negatedTermForm = CreateTermForm(
		TermFormGetPredicateForm(fact.form), !TermFormGetSign(fact.form));
	Atom negatedTerm = CreateFormula(negatedTermForm, fact.actors);

	MixedTypeRelation * negatedRelation = UserQuery(negatedTerm);
	bool foundTuple = MixedTypeRelationNext(negatedRelation);
#ifdef DEBUG
	if(foundTuple) {
		// There was a matching tuple.  Since no actor is a variable,
		// there can be at most one maching tuple, which is the negated term itself.
		TypedTuple const * negatedTuple = MixedTypeRelationPeekTuple(negatedRelation);
		ASSERT(TypedTupleEqual(negatedTuple, fact.actors))
		ASSERT(!MixedTypeRelationNext(negatedRelation))
	}
#endif
	FreeMixedTypeRelation(negatedRelation);
	ReleaseFormula(negatedTerm);
	IFactRelease(negatedTermForm);
	return foundTuple;
}


int AssertFact(FormulaView fact, RelationTableProvider const * provider)
{
	ASSERT(IsTermForm(fact.form));
	ASSERT(!TypedTupleContainsVariable(fact.actors));
	Atom const * actorsArray = TypedTuplePeekAtoms(fact.actors);

	if(checkContradiction(fact))
		return ASSERT_FAIL;

	// find existing relation table, or create new
	TypeSignature typeSignature = CreateTypeSignature(
		TypedTuplePeekAtomTypes(fact.actors), fact.actors->nAtoms);
	Relation const * relation = FindOrCreateRelation(fact.form, fact.actors->nAtoms, typeSignature);
	RelationTable * table = RelationTableRegistryFind(relation);
	bool tableWasCreated = false;
	if(!table) {
		table = CreateRelationTable(relation, provider ? provider : &btreeTableProvider, 0);
		tableWasCreated = true;
	}
	ReleaseRelation(relation);
	// Attempt to add the tuple
	if(RelationTableAddTuple(table, actorsArray, 0) == TUPLE_EXISTS) {
		return ASSERT_EXISTED;
	}
	// Else a new tuple was added.
	LookupAddPredicateRoles(relation, actorsArray);
	// If we created the table above, we drop our reference to it,
	// so that it is deallocated when all tuples are removed.
	if(tableWasCreated)
		ReleaseRelationTable(table);
	return ASSERT_OK;
}


/**
 * Add a clause to the rule dictionary.
 */
static int assertRule(Atom clause, FormulaView clauseView)
{
	// A rule must have at least two terms
	if(ClauseFormNTerms(clauseView.form) < 2)
		return ASSERT_CLAUSE_ONE_TERM;
	//  A clause with no variable is a disjunction of facts, not a rule
	if(!TypedTupleContainsVariable(clauseView.actors))
		return ASSERT_CLAUSE_NO_VARIABLE;

	if(DictionaryContainsClause(clause))
		return ASSERT_EXISTED;
	DictionaryAddClause(clause);
	return ASSERT_OK;
}


int AssertFormula(Atom formula)
{
	FormulaView formulaView = FormulaGetView(formula);

	// Any formula that contains a generator (*) is a defining fact
	if(TypedTupleContainsAtom(formulaView.actors, generatorAtom)) {
		Atom idAtom = CreateIFact(formulaView);
		if(idAtom.hash) {
			return ASSERT_OK;
			// TODO: decide how to manage the reference to the new idAtom.
			// Somehow the UI "owns it" ...
		}
		else
			return ASSERT_INVALID_IFACT;
	}

	if(FormulaIsTerm(formula)) {
		// A term is interpreted as a fact, and may not contain variables
		if(TypedTupleContainsVariable(formulaView.actors))
			return ASSERT_TERM_VARIABLE;
		// Assert the fact, use default storage provider
		return AssertFact(formulaView, 0);
	}

	if(FormulaIsClause(formula))
		return assertRule(formula, formulaView);

	return ASSERT_NOT_CLAUSE;
}


void RetractFact(FormulaView fact)
{
	TypeSignature typeSignature = CreateTypeSignature(
		TypedTuplePeekAtomTypes(fact.actors), fact.actors->nAtoms);
	Relation const * relation = RelationRegistryFind(fact.form, fact.actors->nAtoms, typeSignature);
	if(!relation)
		return;
	RelationTable * table = RelationTableRegistryFind(relation);
	if(!table)
		return;
	Atom const * actorsArray = TypedTuplePeekAtoms(fact.actors);
	// Remove the lookup entries before the tuple: removing the tuple releases the
	// relation's reference to each of its atoms, and releasing the last reference
	// to an atom takes all of its lookup entries with it.
	LookupRemovePredicateRoles(relation, actorsArray);
	// Remove the tuple. This will not remove defining facts
	RelationTableRemoveTuple(table, actorsArray, 0);
	// The RelationTable will be dropped if it was created by AssertFact(),
	// which retains no reference to it
}


/**
 * Structure for temporary storage of tuples for IFactCreate()
 */
typedef struct s_IFactTuple {
	Relation const * relation;
	index8 idColumn;
	Atom tuple[RELATION_MAX_ARITY];
} IFactTuple;


static int8 compareIFactTuples(void const * item1, void const * item2, size32 itemSize)
{
	IFactTuple const * tuple1 = item1;
	IFactTuple const * tuple2 = item2;
	int8 relationOrder = CompareRelations(tuple1->relation, tuple2->relation);
	if(relationOrder != 0)
		return relationOrder;
	else {
		if(tuple1->idColumn < tuple2->idColumn)
			return -1;
		else if(tuple1->idColumn > tuple2->idColumn)
			return 1;
		else
			return 0;
	}
}


/**
 * Gather ifact tuples from a term. Returns true iff the termis valid.
 * Set *termActorIndex to the index of the first actor in the clause, or 0
 * if the entire tuple is a clause. If nonzero, *termActorIndex is incremented
 * with the term arity.
 */
static bool collectTermIFactTuples(
	Atom termForm, TypedTuple const * actors, index8 * termActorIndex, ResizingArray * ifactTupleArray)
{
	size8 termArity = TermFormArity(termForm);
	ASSERT(termArity <= RELATION_MAX_ARITY)

	// Each term must contain exactly one generator, marking the identified atom.
	IFactTuple ifactTuple;
	TypeSignature termSignature;

	bool hasGenerator = false;
	index8 i0 = termActorIndex ? * termActorIndex : 0;
	for(index8 i = 0; i < termArity; i++) {
		TypedAtom actor = TypedTupleGetElement(actors, i0 + i);
		if(SameTypedAtoms(actor, generatorAtom)) {
			if(hasGenerator) {
				// term contains more than one generator
				return false;
			}
			hasGenerator = true;
			ifactTuple.idColumn = i;
			termSignature.atomTypes[i] = AT_ID;
			ifactTuple.tuple[i] = (Atom) {0};
		}
		else {
			termSignature.atomTypes[i] = actor.type;
			ifactTuple.tuple[i] = actor.atom;
		}
	}
	// the term is valid if it had a generator
	if(!hasGenerator)
		return false;

	ifactTuple.relation = FindOrCreateRelation(termForm, termArity, termSignature);
	ResizingArrayAppend(ifactTupleArray, &ifactTuple);

	if(termActorIndex)
		*termActorIndex += termArity;
	return true;
}


/**
 * Gather ifact tuples from a clause. See collectTermIFactTuples() for details.
 */
static bool collectClauseIFactTuples(
	Atom clauseForm, TypedTuple const * actors, index8 * clauseActorIndex, ResizingArray * ifactTupleArray)
{
	if(ClauseFormNTerms(clauseForm) != 1)
		return false;
	Atom termForm = MultisetFindElement(clauseForm, AT_ID, 1);
	return collectTermIFactTuples(termForm, actors, clauseActorIndex, ifactTupleArray);
}


/**
 * Iterate over all clauses in a conjunction and gather ifact tuples.
 * Returns true iff the conjunction is a valid ifact.
 */
static bool collectConjunctionIFactTuples(
	Atom conjunctionForm, TypedTuple const * actors, ResizingArray * ifactTupleArray)
{
	MultisetIterator iterator;
	MultisetIterate(conjunctionForm, AT_ID, &iterator);
	index8 termActorIndex = 0;
	bool formulaIsValid = true;
	// iterate over clause forms
	while(formulaIsValid && MultisetIteratorNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		// iterate over clauses
		for(index8 i = 0; i < elementMultiple.multiple; i++) {
			// Each clause must have a single term.
			Atom clauseForm = elementMultiple.element;
			if(!collectClauseIFactTuples(clauseForm, actors, &termActorIndex, ifactTupleArray)) {
				formulaIsValid = false;
				break;
			}
		}
	}
	MultisetIteratorEnd(&iterator);
	return formulaIsValid;
}


static void freeIFactTuples(ResizingArray * ifactTupleArray)
{
	IFactTuple const * gatheredTuples = ResizingArrayGetMemory(ifactTupleArray);
	for(index32 i = 0; i < ifactTupleArray->nElements; i++)
		ReleaseRelation(gatheredTuples[i].relation);
	FreeResizingArray(ifactTupleArray);
}


Atom CreateIFact(FormulaView formula)
{
	Atom idAtom = {0};

	// collect tuples from the formula
	ResizingArray ifactTupleArray;
	CreateResizingArray(&ifactTupleArray, sizeof(IFactTuple), 10);
	bool formulaIsValid;
	if(IsConjunctionForm(formula.form))
		formulaIsValid = collectConjunctionIFactTuples(formula.form, formula.actors, &ifactTupleArray);
	else if(IsClauseForm(formula.form))
		formulaIsValid = collectClauseIFactTuples(formula.form, formula.actors, 0, &ifactTupleArray);
	else {
		// else we must have a term form (predicates are not allowed)
		ASSERT(IsTermForm(formula.form))
		formulaIsValid = collectTermIFactTuples(formula.form, formula.actors, 0, &ifactTupleArray);
	}
	if(!formulaIsValid) {
		freeIFactTuples(&ifactTupleArray);
		return (Atom) {0};
	}

	// Sort tuples by relation before creating conjunctions
	IFactTuple * ifactTuples = ResizingArrayGetMemory(&ifactTupleArray);
	size8 nTuples = ifactTupleArray.nElements;
	QuickSort(ifactTuples, nTuples, sizeof(IFactTuple), &compareIFactTuples);

	// Create the ifact
	IFactDraft draft;
	IFactBegin(&draft);
	RelationTable * previousTable = 0;
	bool previousTableWasCreated = false;
	for(index32 i = 0; i < nTuples; i++) {
		if(i == 0 || compareIFactTuples(&ifactTuples[i], &ifactTuples[i-1], sizeof(IFactTuple)) != 0) {
			// Begin new conjunction, from a new RelationTable
			if(i > 0) {
				IFactEndConjunction(&draft);
				// Release created tables, so that they deallocate once no tuples remain
				if(previousTableWasCreated)
					ReleaseRelationTable(previousTable);
			}
			RelationTable * table = RelationTableRegistryFind(ifactTuples[i].relation);
			if(!table) {
				// Create new relation table, use B-tree provider as default
				table = CreateRelationTable(ifactTuples[i].relation, &btreeTableProvider, 0);
				// keep track of the table so we can release it once all tuples have been added
				previousTableWasCreated = true;
				previousTable = table;
			}
			else
				previousTableWasCreated = false;
			
			IFactBeginConjunction(&draft, table, ifactTuples[i].idColumn);
		}
		IFactAddTuple(&draft, ifactTuples[i].tuple);
	}
	// End the last conjunction
	IFactEndConjunction(&draft);
	if(previousTableWasCreated)
		ReleaseRelationTable(previousTable);
	
	idAtom = IFactEnd(&draft);

	freeIFactTuples(&ifactTupleArray);
	return idAtom;
}

