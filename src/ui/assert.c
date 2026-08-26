
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/Relation.h"
#include "kernel/RelationRegistry.h"
#include "kernel/RelationTable.h"
#include "kernel/RelationTableRegistry.h"
#include "lang/ClauseForm.h"
#include "lang/TermForm.h"
#include "ui/assert.h"
#include "ui/query.h"
#include "storage/RelationBTree.h"


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

	if(TypedTupleContainsAtom(fact.actors, generatorAtom)) {
		// TODO: create an ifact
		ASSERT(false)
	}
	Atom const * actorsArray = TypedTuplePeekAtoms(fact.actors);

	if(checkContradiction(fact))
		return ASSERT_FAIL;

	// find existing relation table, or create new
	TypeSignature typeSignature = CreateTypeSignature(
		TypedTuplePeekAtomTypes(fact.actors), fact.actors->nAtoms);
	Relation const * relation = FindOrCreateRelation(fact.form, fact.actors->nAtoms, typeSignature);
	RelationTable * table = RelationTableRegistryFind(relation);
	if(!table) {
		table = CreateRelationTable(relation, provider ? provider : &btreeTableProvider, 0);
	}
	ReleaseRelation(relation);
	// Attempt to add the tuple
	if(RelationTableAddTuple(table, actorsArray, 0) == TUPLE_EXISTS)
		return ASSERT_EXISTED;
	// else tuple was added
	LookupAddPredicateRoles(relation, actorsArray);
	return ASSERT_OK;
}


/**
 * Add a clause to the rule dictionary.
 */
static int assertRule(Atom clause, FormulaView view)
{
	// A rule must have at least two terms
	if(ClauseFormNTerms(view.form) < 2)
		return ASSERT_CLAUSE_ONE_TERM;
	//  A clause with no variable is a disjunction of facts, not a rule
	if(!TypedTupleContainsVariable(view.actors))
		return ASSERT_CLAUSE_NO_VARIABLE;

	if(DictionaryContainsClause(clause))
		return ASSERT_EXISTED;
	DictionaryAddClause(clause);
	return ASSERT_OK;
}


int AssertFormula(Atom formula)
{
	FormulaView view = FormulaGetView(formula);

	if(FormulaIsTerm(formula)) {
		if(TypedTupleContainsVariable(view.actors))
			return ASSERT_TERM_VARIABLE;
		// use default storage provider
		return AssertFact(view, 0);
	}

	if(FormulaIsClause(formula))
		return assertRule(formula, view);

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
	// this will not remove defining facts
	RelationTableRemoveTuple(table, actorsArray, 0);
	if(RelationTableNRows(table) == 0) {
		DropRelationTable(table);
	}
}
