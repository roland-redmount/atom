/**
 * Unification methods
 */

#include "kernel/list.h"
#include "lang/unification.h"


// currently the variable index has range 0 .. 255
#define MAX_NUM_VARIABLES	256

#define EDGE_LEFT_NODE		0
#define EDGE_RIGHT_NODE		1


/**
 * We represent an undirected graph as a list of 2*n atom pointers
 * such that the pair {2k, 2k+1} holds edge k.
 */
static TypedAtom graphGetNode(TypedAtom const * edges, index8 edge, index8 node)
{
	return edges[2*edge + node];
}


static void graphSetNode(TypedAtom * edges, index8 edge, index8 node, TypedAtom newNode)
{
	edges[2*edge + node] = newNode;
}


/**
 * Replace all occurences of source with dest in the given graph,
 * starting from a given edge
 */
static void graphSubstitute(TypedAtom source, TypedAtom dest, TypedAtom * edges, uint8 nEdges, uint8 startEdge)
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
static bool findInUGraph(TypedAtom a1, TypedAtom a2, TypedAtom const * edges, uint8 nEdges)
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
 * Initialize the unification graph by adding the
 * undirected edges {list1[i] list2[i]} for 0 = 1, ..., n-1
 * while keeping nodes (atoms) and edges unique
 * 
 * Returns the number of edges added
 */
static uint8 CreateUnificationGraph(Atom list1, Atom list2, TypedAtom * edges, size8 nAtoms)
{
	PrintF("CreateUnificationGraph() nAtoms = %u\n", nAtoms);
	uint8 nEdges = 0;
	// traverse tuples
	// we assume both tuples are of the same length
	for(index8 i = 0; i < nAtoms; i++) {
		// TODO: would be more efficient to traverse the graph once
		TypedAtom a1 = ListGetElement(list1, i+1);
		TypedAtom a2 = ListGetElement(list2, i+1);
		PrintF("Pair %u\n", i);
		if(SameTypedAtoms(a1, a2)) {
			// same atom, don't add self-edge
			continue;
		}
		// check if (unndirected) edge exists
		if(!findInUGraph(a1, a2, edges, nEdges)) {
			PrintF("Adding edge\n");
			// new edge, add
			graphSetNode(edges, i, EDGE_LEFT_NODE, a1);
			graphSetNode(edges, i, EDGE_RIGHT_NODE, a2);
			nEdges++;
		}
	}
	return nEdges;
}


/**
 * Find a unifying substitution (unifier) for two tuples. If a unifier exists,
 * it is always unique. Generates one substitution list for each tuple,
 * such that applying these substitutions results in the same (unified) tuple
 * in both cases.
 * If the tuples do not unify, returns false.
 */
bool UnifyTuples(Atom list1, Atom list2, SubstitutionList * subst1, SubstitutionList * subst2)
{
	size32 nAtoms = ListLength(list1);
	ASSERT(nAtoms <= 255);
	ASSERT(ListLength(list2) == nAtoms);
	
	// initialize substitutions list from unique variables in each tuple
	*subst1 = CreateSubstFromVars(list1);
	*subst2 = CreateSubstFromVars(list2);
	PrintF("Found %u unique variables\n", subst1->nPairs);
	
	// create the unification graph with edges between pairs from t1, t2
	TypedAtom edges[2 * nAtoms];
	uint8 nEdges = CreateUnificationGraph(list1, list2, edges, nAtoms);
	PrintF("U Graph has %u edges\n", nEdges);
	
	// iterate over edges (in arbitrary order) and create substitutions
	for(index8 i = 0; i < nEdges; i++) {
		PrintF("Checking edge %u\n", i);
		// next edge
		TypedAtom a1 = graphGetNode(edges, i, EDGE_LEFT_NODE);
		TypedAtom a2 = graphGetNode(edges, i, EDGE_RIGHT_NODE);
		if(SameTypedAtoms(a1, a2)) {
			// edge to self, nothing to substitute
			// (these can be created by substitutions during graph traversal)
		}
		
	/* reflections not handled yet
		else if(a1.getDatumType() == DT_REFLECTION && a2.getDatumType() == DT_REFLECTION) {
			// two distinct reflections
			if(Reflection::getProform(a1) == Reflection::getProform(a2)) {
				// reflection have same proform, add pairs from the argument tuples to edge list
				// note that any nested reflection will then be processed later
				addUGraphEdges(Reflection::getArguments(a1), Reflection::getArguments(a2), edges);
				// no substitution at this step
				continue;
			}
			else {
				// distinct proforms, so reflections do not match; unification fails
				return false;
			}	
		} 
		if(a1.getDatumType() == DT_RVARIABLE || a2.getDatumType() == DT_RVARIABLE) {
			// one variable and one reflection variable
			// variables do not unify with reflection variables
			return false;
		} */

		else if(a1.type == DT_VARIABLE) {
			// always replace variable from t1 with atom *or* variable from t2
			// this ensures all variables in result will derive from t2
			// (this choice is arbitrary)
			PrintCString("Replace ");
			PrintTypedAtom(a1);
			PrintCString(" with ");
			PrintTypedAtom(a2);
			PrintChar('\n');
			graphSubstitute(a1, a2, edges, nEdges, i+1);
			// set a1 -> a2 in each substitution list
			SetSubstValue(*subst1, a1, a2);
			SetSubstValue(*subst2, a1, a2);
		}
		else {
			if(a2.type == DT_VARIABLE) {
				// replace variable from t2 with atom from t1
				PrintCString("Replace ");
				PrintTypedAtom(a2);
				PrintCString(" with ");
				PrintTypedAtom(a1);
				PrintChar('\n');
				graphSubstitute(a2, a1, edges, nEdges, i+1);
				SetSubstValue(*subst1, a2, a1);
				SetSubstValue(*subst2, a2, a1);
			}
			else {
				// two distinct atoms, unification fails
				return false;
			}
		}
	}

	// renumber variables to be consecutive 1,2,... at most n
//	renumberVariables(sub1);
//	renumberVariables(sub2);
	return true;
}
