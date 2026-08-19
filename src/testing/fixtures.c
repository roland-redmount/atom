
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/RelationRegistry.h"
#include "kernel/RelationTable.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/string.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "storage/RelationBTree.h"
#include "parser/ClauseBuilder.h"
#include "testing/fixtures.h"


Atom CreateTermFormFromRoleNames(char const * const roleNames[], size8 nRoles, bool sign)
{
	ASSERT(nRoles > 0)
	// zeroed first so that the array is initialized whatever nRoles is: an optimizing
	// build cannot otherwise see that the loop below writes all of it
	Atom roles[nRoles];
	SetMemory(roles, nRoles * sizeof(Atom), 0);
	for(index8 i = 0; i < nRoles; i++)
		roles[i] = CreateNameFromCString(roleNames[i]);

	Atom predicateForm = CreatePredicateForm(roles, nRoles);
	Atom termForm = CreateTermForm(predicateForm, sign);

	IFactRelease(predicateForm);
	for(index8 i = 0; i < nRoles; i++)
		NameRelease(roles[i]);
	return termForm;
}


void SetupRelationFixture(
	RelationFixture * fixture, char const * const roleNames[], size8 nColumns)
{
	ASSERT(nColumns <= FIXTURE_MAX_COLUMNS)
	fixture->termForm = CreateTermFormFromRoleNames(roleNames, nColumns, true);
	fixture->nColumns = nColumns;
	fixture->nTuples = 0;

	byte atomTypes[nColumns];
	for(index8 i = 0; i < nColumns; i++) {
		fixture->roleIndex[i] = RelationFixtureRoleIndex(fixture, roleNames[i]);
		atomTypes[i] = AT_ID;
	}
	Relation const * relation = CreateRelation(fixture->termForm, nColumns, atomTypes);
	fixture->table = CreateRelationTable(
		relation, &btreeTableProvider, fixture->roleIndex);
	// the table holds its own reference to the relation
	ReleaseRelation(relation);
}


void RelationFixtureAddTuple(RelationFixture * fixture, char const * const atomNames[])
{
	ASSERT(fixture->nTuples < FIXTURE_MAX_TUPLES)
	TypedAtom actors[fixture->nColumns];
	for(index8 i = 0; i < fixture->nColumns; i++)
		actors[fixture->roleIndex[i]] =
			CreateTypedAtom(AT_ID, CreateStringFromCString(atomNames[i]));

	TypedTuple * tuple = CreateTypedTupleFromArray(actors, fixture->nColumns);
	// the relation table now holds a reference to each atom
	RelationTableAddTuple(fixture->table, TypedTuplePeekAtoms(tuple), 0);
	for(index8 i = 0; i < fixture->nColumns; i++)
		ReleaseTypedAtom(actors[i]);

	fixture->tuples[fixture->nTuples++] = tuple;
}


index8 RelationFixtureRoleIndex(RelationFixture const * fixture, char const * roleName)
{
	Atom role = CreateNameFromCString(roleName);
	index8 index = PredicateRoleIndex(TermFormGetPredicateForm(fixture->termForm), role);
	NameRelease(role);
	return index;
}


void TeardownRelationFixture(RelationFixture * fixture)
{
	// the relation table must be empty before it can be removed
	for(index8 i = 0; i < fixture->nTuples; i++) {
		RelationTableRemoveTuple(
			fixture->table, TypedTuplePeekAtoms(fixture->tuples[i]), 0);
		FreeTypedTuple(fixture->tuples[i]);
	}
	DropRelationTable(fixture->table);
	IFactRelease(fixture->termForm);
	SetMemory(fixture, sizeof(RelationFixture), 0);
}


void SetupPrecSuccFixture(RelationFixture * fixture)
{
	SetupRelationFixture(fixture, (char const * []) {"prec", "succ"}, 2);

	char const * precNames[PREC_SUCC_N_EDGES] = {"a", "b", "c", "c", "e"};
	char const * succNames[PREC_SUCC_N_EDGES] = {"b", "c", "d", "b", "f"};
	for(index8 i = 0; i < PREC_SUCC_N_EDGES; i++)
		RelationFixtureAddTuple(
			fixture, (char const * []) {precNames[i], succNames[i]});
}


void AddTransitiveClosureRules(DictionaryEntry * base, DictionaryEntry * recursive)
{
	*base = DictionaryAddClauseFromCString(
		"before _x after _y | ! prec _x succ _y");
	*recursive = DictionaryAddClauseFromCString(
		"before _x after _y | ! prec _x succ _z | ! before _z after _y");
}


void SetupEdgeFixture(RelationFixture * fixture)
{
	SetupRelationFixture(fixture, (char const * []) {"edge", "from", "to"}, 3);

	char const * edgeNames[EDGE_N_EDGES] = {"ep", "eq", "er", "es"};
	char const * fromNames[EDGE_N_EDGES] = {"a", "a", "b", "b"};
	char const * toNames[EDGE_N_EDGES] = {"b", "a", "b", "c"};
	for(index8 i = 0; i < EDGE_N_EDGES; i++)
		RelationFixtureAddTuple(
			fixture, (char const * []) {edgeNames[i], fromNames[i], toNames[i]});
}
