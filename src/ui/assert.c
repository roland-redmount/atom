
#include "kernel/lookup.h"
#include "kernel/RelationRegistry.h"
#include "kernel/RelationTable.h"
#include "kernel/RelationTableRegistry.h"
#include "lang/TermForm.h"
#include "ui/assert.h"


// TODO: this should return a status code indicating whether the fact was created,
// already existed, or if the assert failed due to logical inconsistency
void AssertFact(Atom termForm, TypedTuple const * actors, uint8 idPosition)
{
	ASSERT(IsTermForm(termForm));
	Atom const * actorsArray = TypedTuplePeekAtoms(actors);
	Relation const * relation = RelationRegistryFind(
		termForm, actors->nAtoms, TypedTuplePeekAtomTypes(actors));
	RelationTable * table = relation ? RelationTableRegistryFind(relation) : 0;
	if(table)
		RelationTableAddTuple(table, actorsArray, idPosition);
	else {
		// TODO: create a relation table if not exists? Default to B-tree?
		ASSERT(false);
	}
	LookupAddPredicateRoles(relation, actorsArray);
}


void RetractFact(Atom termForm, TypedTuple * actors)
{
	Relation const * relation = RelationRegistryFind(
		termForm, actors->nAtoms, TypedTuplePeekAtomTypes(actors));
	RelationTable * table = RelationTableRegistryFind(relation);
	Atom const * actorsArray = TypedTuplePeekAtoms(actors);
	// Remove the lookup entries before the tuple: removing the tuple releases the
	// relation's reference to each of its atoms, and releasing the last reference
	// to an atom takes all of its lookup entries with it.
	// NOTE: the below does not accept variables in the actors tuple,
	// so we can only retract 1 fact at a time.
	LookupRemovePredicateRoles(relation, actorsArray);
	// this will not remove defining facts
	RelationTableRemoveTuple(table, actorsArray, 0);

	// TODO: remove service if empty?
}
