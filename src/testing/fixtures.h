/**
 * Relations and rules that several test suites need, kept here so that one graph is
 * described once. A fixture registers a relation and asserts its facts, and must be torn
 * down again by the test that set it up, leaving the registries as it found them.
 */

#ifndef FIXTURES_H
#define FIXTURES_H

#include "kernel/dictionary.h"
#include "kernel/RelationTable.h"
#include "kernel/typedtuple.h"


// A B-tree relation registers one service per prefix of its index columns, and one
// more enumerating the whole relation; see CreateRelationBTreeWithServices()
#define RELATION_FIXTURE_N_SERVICES(nColumns)	((nColumns) + 1)

// Upper bounds on a fixture relation, large enough for the fixtures here
#define FIXTURE_MAX_COLUMNS		4
#define FIXTURE_MAX_TUPLES		8


/**
 * A relation whose atoms are all identified by name (AT_ID), stored in a B-tree relation
 * table with the services of RelationBTree.
 *
 * The roles of a predicate form are held in canonical order, which is not the order the
 * role names were given in, so a fixture keeps the column index of each role; see
 * RelationFixtureRoleIndex(). The columns are indexed in the order the role names were
 * given, so a lookup on the first of them is the fast one.
 */
typedef struct {
	Atom termForm;
	RelationTable const * table;
	size8 nColumns;
	// Column index of each role, in the order the role names were given
	index8 roleIndex[FIXTURE_MAX_COLUMNS];
	size8 nTuples;
	TypedTuple * tuples[FIXTURE_MAX_TUPLES];
} RelationFixture;

/**
 * Create the positive term form of the given role names, and register a relation table
 * for it holding one AT_ID column per role. The fixture holds no facts until
 * RelationFixtureAssertFact() adds them.
 */
void SetupRelationFixture(
	RelationFixture * fixture, char const * const roleNames[], size8 nColumns);

/**
 * Assert one fact of the fixture relation, naming the atom of each role in the order the
 * role names were given to SetupRelationFixture(). The fixture keeps the tuple, so that
 * a test can compare an answer against it, and retracts it on teardown.
 */
void RelationFixtureAssertFact(RelationFixture * fixture, char const * const atomNames[]);

/**
 * Column index of the given role of the fixture relation, which is also the index of that
 * role in an actors tuple of the form.
 */
index8 RelationFixtureRoleIndex(RelationFixture const * fixture, char const * roleName);

/**
 * Retract every fact of the fixture and remove its services, relation and form.
 */
void TeardownRelationFixture(RelationFixture * fixture);


/**
 * The directed graph (prec succ)
 *
 *   a -> b -> c -> d,  c -> b,  e -> f
 *
 * whose one cycle is b -> c -> b, and whose component e -> f is reachable from nothing
 * else. Its transitive closure holds three tuples from each of a, b and c, and the single
 * edge of the other component; b and c come after themselves.
 *
 * The nodes are strings, and so are written quoted in a query: an actor must be a
 * literal, and a bare word is a role name to the parser.
 */
#define PREC_SUCC_N_EDGES			5
#define PREC_SUCC_N_SERVICES		RELATION_FIXTURE_N_SERVICES(2)
#define PREC_SUCC_N_CLOSURE_TUPLES	10

void SetupPrecSuccFixture(RelationFixture * fixture);

/**
 * The rules defining (before after) as the transitive closure of (prec succ). The caller
 * removes both clauses with DictionaryRemoveClause().
 */
void AddTransitiveClosureRules(DictionaryEntry * base, DictionaryEntry * recursive);


/**
 * The directed graph (edge from to) with the edges named ep..es
 *
 *   a -> b,  a -> a,  b -> b,  b -> c
 *
 * so that eq and er are the self edges. Strings are lists of letters, so the edges are
 * named ep..es rather than e1..e4, and their letters are kept clear of the node names.
 */
#define EDGE_N_EDGES				4

void SetupEdgeFixture(RelationFixture * fixture);


/**
 * Create the term form of the given role names, releasing the names and the predicate
 * form again. The caller releases the returned term form with IFactRelease().
 */
Atom CreateTermFormFromRoleNames(char const * const roleNames[], size8 nRoles, bool sign);


#endif	// FIXTURES_H
