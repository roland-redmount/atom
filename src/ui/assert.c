
#include "kernel/dispatch.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/Relation.h"
#include "kernel/RelationRegistry.h"
#include "kernel/RelationTable.h"
#include "kernel/RelationTableRegistry.h"
#include "lang/TermForm.h"
#include "ui/assert.h"
#include "ui/query.h"
#include "storage/RelationBTree.h"


/**
 * Test whether the given term interpreted as a fact contradicts the current knowledgebase,
 * that is, whether the negation of the term is  entailed by the knowledgebase.
 * The actors tuple cannot contain a variable. Returns true if there is a contradiction.
 */
static bool checkContradiction(Atom termForm, TypedTuple const * actors)
{
	// Run a query for the negated term.
	Atom negatedTermForm = CreateTermForm(
		TermFormGetPredicateForm(termForm), !TermFormGetSign(termForm));
	Atom negatedTerm = CreateFormula(negatedTermForm, actors);

	MixedTypeRelation * negatedRelation = UserQuery(negatedTerm);
	bool foundTuple = MixedTypeRelationNext(negatedRelation);
#ifdef DEBUG
	if(foundTuple) {
		// There was a matching tuple.  Since no actor is a variable,
		// there can be at most one maching tuple, which is the negated term itself.
		TypedTuple const * negatedTuple = MixedTypeRelationPeekTuple(negatedRelation);
		ASSERT(TypedTupleEqual(negatedTuple, actors))
		ASSERT(!MixedTypeRelationNext(negatedRelation))
	}
#endif
	FreeMixedTypeRelation(negatedRelation);
	ReleaseFormula(negatedTerm);
	IFactRelease(negatedTermForm);
	return foundTuple;
}


int AssertFact(Atom termForm, TypedTuple const * actors, RelationTableProvider const * provider)
{
	ASSERT(IsTermForm(termForm));
	Atom const * actorsArray = TypedTuplePeekAtoms(actors);

	// TODO: verify there is no variable among the actors

	if(checkContradiction(termForm, actors))
		return ASSERT_FAIL;

	// find existing relation table, or create new
	Relation const * relation = FindOrCreateRelation(
		termForm, actors->nAtoms, TypedTuplePeekAtomTypes(actors));
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


void RetractFact(Atom termForm, TypedTuple const * actors)
{
	Relation const * relation = RelationRegistryFind(
		termForm, actors->nAtoms, TypedTuplePeekAtomTypes(actors));
	if(!relation)
		return;
	RelationTable * table = RelationTableRegistryFind(relation);
	if(!table)
		return;
	Atom const * actorsArray = TypedTuplePeekAtoms(actors);
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
