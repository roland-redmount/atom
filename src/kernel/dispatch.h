
/**
 * The dispatcher accepts a query (formula) and finds a matching service
 * within the current process.
 */

#ifndef DISPATCH_H
#define DISPATCH_H

#include "kernel/ServiceRegistry.h"


/**
 * Dispatch a query (formula), return the matching service, if any.
 */
bool DispatchQuery(Atom query, ServiceRecord * record);



// generic tuple matching across formula permutations

// TODO: review & refactor this!
// For conceptual simplicity, it might be a good idea to
// use the general unification algorithm for signature matching,
// even though this problem is a special case, so we don't have
// to maintain two algorithms and ensure they are consistent ...


/**
 * Test all possible form permutations p of formula2 and check
 * tupleMatch(formula1, p). If a match is found, the 
 * permutation is written to the perm array.
 * 
 * NOTE: typically we would want to use the permutation to assign
 * to an arguments list for a bytecode context on the VM stack.
 */

// bool PermutationMatch(Atom formula1, Atom formula2, Atom * args, index8 * perm, Signature const * signature);

/**
 * Test whether a query formula matches a signature, accounting for permutations
 * Writes matching arguments extracted from from query to the matches array,
 * and writes a permutation to the array perm
 * such that PermuteTuple(query, perm) matches signature->formula->tuple
 *
 * TODO: this does not handle unification properly! Generally, there may be
 *   more than one matching tuple, and we must select the most general unifier.
 *   We should probably replace this with unification followed by a atom type check
 */

// bool SignatureQueryMatch(const Signature* signature, Atom query, Atom * inputs, index8 * perm);


#endif	// DISPATCH_H
