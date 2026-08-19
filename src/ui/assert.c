
#include "kernel/lookup.h"
#include "kernel/Relation.h"
#include "kernel/RelationRegistry.h"
#include "kernel/RelationTable.h"
#include "kernel/RelationTableRegistry.h"
#include "lang/TermForm.h"
#include "ui/assert.h"
#include "storage/RelationBTree.h"


int AssertFact(Atom termForm, TypedTuple const * actors, RelationTableProvider const * provider)
{
	ASSERT(IsTermForm(termForm));
	Atom const * actorsArray = TypedTuplePeekAtoms(actors);

	// TODO: logic consistency check

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


void RetractFact(Atom termForm, TypedTuple * actors)
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
