/**
 * Unification methods
 */

#include "kernel/list.h"
#include "lang/unification.h"


// currently the variable index has range 0 .. 255
#define MAX_NUM_VARIABLES	256

enum EdgeSide {
	EDGE_LEFT_NODE = 0,
	EDGE_RIGHT_NODE = 1
};


/**
 * We represent an undirected graph as an array of 2*n atom pointers
 * such that the pair {2k, 2k+1} holds edge k.
 */
static TypedAtom graphGetNode(TypedAtom const * edges, index8 edgeIndex, enum EdgeSide side)
{
	return edges[2*edgeIndex + side];
}


static void graphSetNode(TypedAtom * edges, index8 edgeIndex, enum EdgeSide  side, TypedAtom newNode)
{
	edges[2*edgeIndex + side] = newNode;
}


/**
 * Replace all occurences of source with dest in the given graph,
 * starting from a given edge
 */
static void graphSubstitute(TypedAtom source, TypedAtom dest, TypedAtom edges[], uint8 nEdges, uint8 startEdge)
{
	for(index8 i = startEdge; i < nEdges; i++) {
		if(SameTypedAtoms(edges[2*i], source))
			edges[2*i] = dest;
		if(SameTypedAtoms(edges[2*i + 1], source))
			edges[2*i + 1] = dest;
	}
}

/**
 * Check if an undirected edge {a1, a2} exists in the graph given by the edges list.
 */
static bool findInUGraph(TypedAtom a1, TypedAtom a2, TypedAtom const edges[], uint8 nEdges)
{
	for(index8 j = 0; j < nEdges; j++) {
		TypedAtom left = graphGetNode(edges, j, EDGE_LEFT_NODE);
		TypedAtom right = graphGetNode(edges, j, EDGE_RIGHT_NODE);
		// check for match in either direction
		if(SameTypedAtoms(left, a1) && SameTypedAtoms(right, a2))
			return true;
		if(SameTypedAtoms(left, a2) && SameTypedAtoms(right, a1))
			return true;
	}
	return false;
}

/**
 * Given two tuples of length n, create the undirected graph consisting of the
 * *unique* edges {list1[i], list2[i]} for i = 1, ..., n, minus self-edges
 * where list1[i] = list2[i]. Returns the number of edges added
 */
static uint8 setupUnificationGraph(TypedTuple const * tuple1, TypedTuple const * tuple2, TypedAtom * edges)
{
	uint8 nEdges = 0;
	// traverse tuples
	// we assume both tuples are of the same length
	for(index8 i = 0; i < tuple1->nAtoms; i++) {
		TypedAtom a1 = TypedTupleGetElement(tuple1, i);
		TypedAtom a2 = TypedTupleGetElement(tuple2, i);
		if(SameTypedAtoms(a1, a2)) {
			// Skip self-edge
			continue;
		}
		// check if undirected edge already exists in graph
		if(!findInUGraph(a1, a2, edges, nEdges)) {
			// new edge, add
			graphSetNode(edges, nEdges, EDGE_LEFT_NODE, a1);
			graphSetNode(edges, nEdges, EDGE_RIGHT_NODE, a2);
			nEdges++;
		}
	}
	return nEdges;
}


bool UnifyTuples(TypedTuple const * tuple1, TypedTuple const * tuple2, Substitution * subst1, Substitution * subst2)
{
	ASSERT(tuple1->nAtoms == tuple2->nAtoms)
	
	// setup empty substitution lists
	SetupSubstitution(subst1, tuple1->nAtoms);
	SetupSubstitution(subst2, tuple2->nAtoms);
	
	// create the initial unification graph
	TypedAtom edges[2 * tuple1->nAtoms];
	uint8 nEdges = setupUnificationGraph(tuple1, tuple2, edges);
	
	// iterate over graph edges (in arbitrary order) and create substitutions
	for(index8 i = 0; i < nEdges; i++) {
		TypedAtom left = graphGetNode(edges, i, EDGE_LEFT_NODE);
		TypedAtom right = graphGetNode(edges, i, EDGE_RIGHT_NODE);
		if(SameTypedAtoms(left, right)) {
			// edge to self, nothing to substitute
			// (these can be created by substitutions during graph traversal)
		}
		else if(left.type == AT_VARIABLE) {
			/*
			 * Always replace a variable from list1 with atom *or* variable from list2.
			 * This ensures only subst1 will substitute with a variable, all of which will derive
			 * from list 2, while subst1 will only substitute with atoms.
			 * (This choice is arbitrary)
			 */
			graphSubstitute(left, right, edges, nEdges, i+1);
			// set a1 -> a2 in each substitution list
			SubstitutionSetValue(subst1, left, right);
			SubstitutionSetValue(subst2, left, right);
		}
		else {
			if(right.type == AT_VARIABLE) {
				// replace variable from t2 with atom from t1
				graphSubstitute(right, left, edges, nEdges, i+1);
				SubstitutionSetValue(subst1, right, left);
				SubstitutionSetValue(subst2, right, left);
			}
			else {
				// two distinct atoms, unification fails
				return false;
			}
		}
	}
	return true;
}
