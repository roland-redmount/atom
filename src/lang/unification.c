/**
 * Unification methods
 */

#include "kernel/Parameter.h"
#include "lang/unification.h"
#include "memory/allocator.h"


// currently the variable index has range 0 .. 255
#define MAX_NUM_VARIABLES	256

enum EdgeSide {
	EDGE_LEFT_NODE = 0,
	EDGE_RIGHT_NODE = 1
};


/**
 * We represent an undirected graph as an array of 2 * capacity TypedAtoms
 * such that the pair {2k, 2k+1} holds edge k.
 */
typedef struct t_UnificationGraph {
	size32 capacity;
	size32 nEdges;
	TypedAtom edges[];
} UnificationGraph;


static TypedAtom graphGetNode(UnificationGraph const * graph, index8 edgeIndex, enum EdgeSide side)
{
	return graph->edges[2*edgeIndex + side];
}


static void graphSetNode(UnificationGraph * graph, index8 edgeIndex, enum EdgeSide  side, TypedAtom newNode)
{
	graph->edges[2*edgeIndex + side] = newNode;
}


/**
 * While graph edges are undirected, we order an edge (left, right) by atom type
 * so that x can be substituted with y. Ordered edges are (variable, parameter),
 * (variable, constant), and (parameter, constant). 
 */
static bool edgeIsOrdered(TypedAtom left, TypedAtom right)
{
	if(left.type == AT_VARIABLE)
		return true;
	if(left.type == AT_PARAMETER && right.type != AT_VARIABLE)
		return true;
	return false;
}

/**
 * Add an edge to the graph, reversing if necessary to keep it ordered
 */
static void graphAddEdge(UnificationGraph * graph, TypedAtom left, TypedAtom right)
{
	ASSERT(graph->nEdges < graph->capacity)
	if(edgeIsOrdered(left, right)) {
		graphSetNode(graph, graph->nEdges, EDGE_LEFT_NODE, left);
		graphSetNode(graph, graph->nEdges, EDGE_RIGHT_NODE, right);
	}
	else {
		graphSetNode(graph, graph->nEdges, EDGE_LEFT_NODE, right);
		graphSetNode(graph, graph->nEdges, EDGE_RIGHT_NODE, left);
	}
	graph->nEdges++;
}


/**
 * Replace all occurences of atom <key> with atom <value> in the given graph,
 * in either position in each edge, starting from a given edge.
 */
static void graphSubstitute(UnificationGraph * graph, TypedAtom key, TypedAtom value, uint8 startEdge)
{
	for(index8 i = startEdge; i < graph->nEdges; i++) {
		TypedAtom * left = &(graph->edges[2*i]);
		TypedAtom * right = &(graph->edges[2*i + 1]);
		if(SameTypedAtoms(*left, key))
			*left = value;
		if(SameTypedAtoms(*right, key))
			*right = value;
		// If substitution caused the edge to become unordered, reverse it
		if(!edgeIsOrdered(*left, *right)) {
			TypedAtom temp = *left;
			*left = *right;
			*right = temp;
		}
	}
}

/**
 * Check if an undirected edge {a1, a2} exists in the graph given by the edges list.
 */
static bool findInUGraph(UnificationGraph const * graph, TypedAtom a1, TypedAtom a2)
{
	for(index8 j = 0; j < graph->nEdges; j++) {
		TypedAtom left = graphGetNode(graph, j, EDGE_LEFT_NODE);
		TypedAtom right = graphGetNode(graph, j, EDGE_RIGHT_NODE);
		// check for match in either direction
		if(SameTypedAtoms(left, a1) && SameTypedAtoms(right, a2))
			return true;
		if(SameTypedAtoms(left, a2) && SameTypedAtoms(right, a1))
			return true;
	}
	return false;
}


/**
 * Returns true if atoms (x, y) unify, else false.
 */
static bool atomsUnify(TypedAtom x, TypedAtom y)
{
	if(x.type == AT_VARIABLE || y.type == AT_VARIABLE) {
		// A variable unifies with any other atom
		return true;
	}
	if(x.type == AT_PARAMETER && y.type == AT_PARAMETER) {
		// Two parameters unify if they have the same type and direction
		return SameParameters(x.atom, y.atom);
	}
	if(x.type == AT_PARAMETER && y.type != AT_PARAMETER) {
		return !x.atom.parameter.atomType || x.atom.parameter.atomType == y.type;
	}
	if(x.type != AT_PARAMETER && y.type == AT_PARAMETER) {
		return !y.atom.parameter.atomType || x.type == y.atom.parameter.atomType;
	}
	// Two constants that are neither variables nor parameters must be equal
	return SameTypedAtoms(x, y);
}

/**
 * Given two tuples of length n, create the graph consisting of the unique edges
 * (tuple1[i], tuple2[i]) for i = 1, ..., n, except for any self-edges where tuple1[i] = tuple2[i].
 * An edge (x, y) is not added if the graph already contains (y, x).
 * No edge is added between two constants.
 * Returns the graph, or 0 if a edge between two distinct constants was found,
 * in which case unification fails.
 */
static UnificationGraph * createUnificationGraph(TypedTuple const * tuple1, TypedTuple const * tuple2)
{
	ASSERT(tuple1->nAtoms == tuple2->nAtoms)

	size32 capacity = tuple1->nAtoms;
	UnificationGraph * graph = Allocate(sizeof(UnificationGraph) + 2 * capacity * sizeof(TypedAtom));
	graph->capacity = capacity;
	graph->nEdges = 0;
	
	// Traverse tuples
	for(index8 i = 0; i < tuple1->nAtoms; i++) {
		TypedAtom left = TypedTupleGetElement(tuple1, i);
		TypedAtom right = TypedTupleGetElement(tuple2, i);
		if(SameTypedAtoms(left, right)) {
			// Skip self-edge
			continue;
		}
		// If the two atoms of this edge fail to unify, unification to fails.
		if(!atomsUnify(left ,right)) {
			Free(graph);
			return 0;
		}
		if(!findInUGraph(graph, left, right)) {
			graphAddEdge(graph, left, right);
		}
	}
	return graph;
}


bool UnifyTuples(TypedTuple const * tuple1, TypedTuple const * tuple2, Substitution * subst)
{
	// Setup an empty substitution list
	SetupSubstitution(subst, tuple1->nAtoms);

	// Create the initial unification graph
	UnificationGraph * graph = createUnificationGraph(tuple1, tuple2);
	if(!graph)
		return false;

	// iterate over graph edges (in arbitrary order) and create substitutions
	for(index8 i = 0; i < graph->nEdges; i++) {
		TypedAtom left = graphGetNode(graph, i, EDGE_LEFT_NODE);
		TypedAtom right = graphGetNode(graph, i, EDGE_RIGHT_NODE);
		if(SameTypedAtoms(left, right)) {
			// Edge to self, nothing to substitute.
			// Self-edges can be created by substitutions during graph traversal.
			continue;
		}
		if(!atomsUnify(left, right)) {
			Free(graph);
			return false;
		}
		// Substitute all edge after edge i in the graph
		graphSubstitute(graph, left, right, i+1);
		SubstitutionAdd(subst, left, right);
	}
	// PrintSubstitution(subst);
	// PrintChar('\n');
	Free(graph);
	return true;
}
