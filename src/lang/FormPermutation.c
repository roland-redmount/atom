
#include <stdlib.h>

#include "kernel/multiset.h"
#include "lang/ClauseForm.h"
#include "lang/DatumType.h"
#include "lang/FullForm.h"
#include "lang/Form.h"
#include "lang/FormPermutation.h"
#include "lang/PredicateForm.h"
#include "util/utilities.h"


/**
 * Reverse the subsequence array[a] ... array[b] of array
 */
static void reverseRange(index8* array, index8 a, index8 b)
{
	// iterate inwards from bounds and swap each pair
	// if the range length is odd, we don't touch the middle element
	while(a < b) {
		SwapBytes(&array[a], &array[b]);
		a++;
		b--;
	}
}

/**
 * Enumerate all possible n! permutations of an n-vector
 * Assuming perm[a] ... perm[a+n-1] is a permutation
 * of consecutive numbers a, a+1, ... a+n-1, this
 * sets perm to the next permutation in the enumeration
 * Returns true if a new permutation was found. If there are
 * no more permutations, returns false and resets perm vector
 * to the identity permutatíon
 */

bool PermuteArray(index8 * perm, index8 a, size8 n)
{
	if(n <= 1) {
		// trivial case {a}, no permutations
		return false;
	}
	// find largest index i such that perm[i] < perm[i+1]
	index8 i = a + n - 2;
	while(i >= a) {
		if(perm[i] < perm[i+1])
			break;
		if(i == a) {
			// no such i exists, array is last permutation {a+n-1, a+n-2, ..., a}, 
			// no more permutations
			// reset to identity permutation {a, a+1, ... a+n-1}
			for(index8 k = a; k < a+n; k++)
				perm[k] = k;
			return false;
		}
		i--;
	}
	// find largest index j > i such that perm[i] < perm[j]
	// (this always exists, since j = i+1 is a possibility)
	index8 j = a + n - 1;
	while(j > i) {
		if(perm[i] < perm[j])
			break;
		j--;
	}
	// swap elements i, j
	SwapBytes(&perm[i], &perm[j]);
	// reverse elements i+1, ..., n-1
	reverseRange(perm, i+1, n-1);
	// done
	return true;
}


/**
 * Create an identity permutation of given size
 */
static Permutation * CreatePermutation(size8 size)
{
	Permutation * permutation = malloc(sizeof(Permutation) + sizeof(index8) * size);
	permutation->size = size;
	// set identity permutation 1...m 
	for(index8 i = 0; i < size; i++)
		permutation->order[i] = i;
	return permutation;
}


static bool NextPermutation(Permutation * permutation)
{
	return PermuteArray(permutation->order, 0, permutation->size);
}


/**
 * Create a new predicate iterator, starting from the identity permutation
 */
PredicateIterator * CreatePredicateIterator(Atom predForm)
{
	PredicateIterator* iter = malloc(sizeof(PredicateIterator));
	iter->nUniqueRoles = PredicateNRoles(predForm);

	iter->rolePerm = malloc(iter->nUniqueRoles * sizeof(Permutation *));
	// create iterators for each unique role
	MultisetIterator iterator;
	MultisetIterate(predForm, &iterator);
	iter->arity = 0;
	for(index8 i = 0; i < iter->nUniqueRoles; i++) {
		ASSERT(MultisetIteratorHasNext(&iterator));
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		iter->rolePerm[i] = CreatePermutation(em.multiple);
		iter->arity += em.multiple;
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
	return iter;
}


/**
 * Advance predicate iterator to next permutation
*/
bool NextPredicatePermutation(PredicateIterator* iter)
{
	// permute roles
	for(index8 i = 0; i < iter->nUniqueRoles; i++) {
		// check for new permutation
		if(NextPermutation(iter->rolePerm[i])) {
			// permutation found
			return true;
		}
	}
	// no more permutations
	return false;
}

/**
 * Get tuple permutation vector from predicate iterator
 * Writes permutation + offset into the tuplePerm vector
 */
static void getPredicatePermutation(PredicateIterator const * iter, index8 * tuplePerm, index8 offset)
{
//	printf("    getPredicatePermutation() offset = %u\n", offset);
	// for each role
	for(index8 i = 0, k = 0; i < iter->nUniqueRoles; i++) {
		size8 multiplicity = iter->rolePerm[i]->size;

		for(index8 j = 0; j < multiplicity; j++) {
			// copy role permutations vectors
			tuplePerm[k + iter->rolePerm[i]->order[j]] = offset + k + j;
		}
		k += multiplicity;
	}
}


/**
 * Deallocate predicate iterator
 */
void FreePredicateIterator(PredicateIterator * iter)
{
	// free permutations
	for(index8 i = 0; i < iter->nUniqueRoles; i++) {
		free(iter->rolePerm[i]);
	}
	free(iter->rolePerm);
	free(iter);
}


/**
 * Create a new clause iterator, starting from the identity permutation
 */
ClauseIterator * CreateClauseIterator(Atom clauseForm)
{
	ClauseIterator* iter = malloc(sizeof(ClauseIterator));
	iter->nUniqueTerms = ClauseNUniqueTerms(clauseForm);
	
	// allocate arrays
	iter->predFormPerm = malloc(iter->nUniqueTerms * sizeof(Permutation *));
	iter->predIter = malloc(iter->nUniqueTerms * sizeof(PredicateIterator **));
	
	// create permutations and corresponding iterators
	MultisetIterator iterator;
	MultisetIterate(clauseForm, &iterator);
	iter->arity = 0;
	for(index8 i = 0; i < iter->nUniqueTerms; i++) {
		ASSERT(MultisetIteratorHasNext(&iterator));
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
	
		iter->predFormPerm[i] = CreatePermutation(em.multiple);

		// create a predicate iterator for each multiple of each term form
		iter->predIter[i] = malloc(em.multiple * sizeof(PredicateIterator *));
		Atom predForm = GetPredicateForm(em.element);
		for(index8 j = 0; j < em.multiple; j++) {
			iter->predIter[i][j] = CreatePredicateIterator(predForm);
		}
		iter->arity += PredicateArity(predForm);
	}
	MultisetIteratorEnd(&iterator);
	return iter;
}

/**
 * Advance clause iterator to next permutation
 */
bool NextClausePermutation(ClauseIterator * iter)
{
	// first try all predicate iterators
	for(index8 i = 0; i < iter->nUniqueTerms; i++) {
		size8 multiplicity = iter->predFormPerm[i]->size;
		for(index8 j = 0; j < multiplicity; j++) {
			// try permuting this predicate
			if(NextPredicatePermutation(iter->predIter[i][j]))
				return true;
		}
	}
	// no more permutations from iterators
	// permute predicate forms
	for(index8 i = 0; i < iter->nUniqueTerms; i++) {
		// check for new permutation
		if(NextPermutation(iter->predFormPerm[i]))
			return true;
	}
	// no more permutations
	return false;
}


/**
 * Get tuple permutation vector from clause iterator
 * Writes permutation + offset into the tuplePerm vector 
 * Returns the number of elements written
 */
static void getClausePermutation(ClauseIterator const * iter, index8 * tuplePerm, index8 offset)
{
	for(index8 i = 0, termOffset = 0; i < iter->nUniqueTerms; i++) {
		size8 multiplicity = iter->predFormPerm[i]->size;

		size8 termArity = iter->predIter[i][0]->arity;

		for(index8 j = 0; j < multiplicity; j++) {
			// get permuted predicate index from predicate iterator
			index8 jperm = iter->predFormPerm[i]->order[j];
			index8 predOffset = termOffset + jperm * termArity;
			getPredicatePermutation(
				iter->predIter[i][jperm],
				&tuplePerm[predOffset],
				offset + predOffset
			);
		}
		termOffset += multiplicity * termArity; 
	}
}


/**
 * Deallocate clause iterator
 */
void FreeClauseIterator(ClauseIterator * iter)
{
	// free predicate iterators
	for(index8 i = 0; i < iter->predFormPerm[i]->size; i++) {
		size8 multiplicity = iter->predFormPerm[i]->size;
		for(index8 j = 0; j < multiplicity; j++) {
			FreePredicateIterator(iter->predIter[i][j]);
		}
		free(iter->predIter[i]);
	}
	free(iter->predIter);
	// free permutations
	for(index8 i = 0; i < iter->predFormPerm[i]->size; i++) {
		free(iter->predFormPerm[i]);
	}
	free(iter->predFormPerm);
	free(iter);
}


/**
 * Create a new form iterator, starting from the identity permutation
 */
ConjunctionIterator * CreateConjunctionIterator(Atom form)
{
	ConjunctionIterator* iter = malloc(sizeof(ConjunctionIterator));
	iter->nClauses = FullFormNUniqueClauseForms(form);

	// allocate arrays
	iter->clauseFormPerm = malloc(iter->nClauses * sizeof(Permutation *));
	iter->clauseIter = malloc(iter->nClauses * sizeof(ClauseIterator **));

	MultisetIterator iterator;
	MultisetIterate(form, &iterator);
	for(index8 i = 0; i < iter->nClauses; i++) {
		ASSERT(MultisetIteratorHasNext(&iterator));
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
	
		iter->clauseFormPerm[i] = CreatePermutation(em.multiple);

		// create a clause iterator for each multiple of each clause form
		iter->clauseIter[i] = malloc(em.multiple * sizeof(ClauseIterator *));
		for(index8 j = 0; j < em.multiple; j++) {
			iter->clauseIter[i][j] = CreateClauseIterator(em.element);
		}
	}
	MultisetIteratorEnd(&iterator);
	return iter;
}


/**
* Advance conjunction form iterator to next permutation
*/
bool NextConjunctionPermutation(ConjunctionIterator * iter)
{
	// first try all clause iterators
	for(index8 i = 0; i < iter->nClauses; i++) {
		uint8 multiplicity = iter->clauseFormPerm[i]->size;
		for(index8 j = 0; j < multiplicity; j++) {
			// try permuting this clause
			if(NextClausePermutation(iter->clauseIter[i][j])) {
				return true;
			}
		}
	}
	// permute over clause forms
	for(index8 i = 0; i < iter->nClauses; i++) {
		if(NextPermutation(iter->clauseFormPerm[i]))
			return true;
	}
	// no more permutations
	return false;
}


void FreeConjunctionIterator(ConjunctionIterator* iter)
{
	// free clause iterators
	for(index8 i = 0; i < iter->nClauses; i++) {
		uint8 multiplicity = iter->clauseFormPerm[i]->size;
		for(index8 j = 0; j < multiplicity; j++) {
			FreeClauseIterator(iter->clauseIter[i][j]);
		}
		free(iter->clauseIter[i]);
	}
	free(iter->clauseIter);
	// free permutations
	for(index8 i = 0; i < iter->nClauses; i++) {
		free(iter->clauseFormPerm[i]);
	}
	free(iter->clauseFormPerm);
	free(iter);
}


static void getConjunctionPermutation(const ConjunctionIterator * iter, index8 * tuplePerm)
{
	for(index8 i = 0, t = 0; i < iter->nClauses; i++) {
		uint8 multiplicity = iter->clauseFormPerm[i]->size;
		size8 clauseArity = iter->clauseIter[i][0]->arity;
		
		for(index8 j = 0; j < multiplicity; j++) {
			// get permuted clause from clause iterator
			index8 jperm = iter->clauseFormPerm[i]->order[j];

			getClausePermutation(
				iter->clauseIter[i][jperm],
				&tuplePerm[t + j*clauseArity],
				t + jperm*clauseArity);
		}
		t += multiplicity * clauseArity;
	}
}


// we now need interface functions here since form can take several types
// poor mans polymorphism ...

FormIterator* CreateFormIterator(Atom form)
{
	FormIterator * iter = malloc(sizeof(FormIterator));
	iter->form = form;
	if(IsPredicateForm(form))
		iter->topIterator = CreatePredicateIterator(form);
	else if(IsClauseForm(form))
		iter->topIterator = CreateClauseIterator(form);
	else if(IsConjunctionForm(form))
		iter->topIterator = CreateConjunctionIterator(form);
	else {
		ASSERT(false);
	}
	return iter;
}


bool NextFormPermutation(FormIterator * iter)
{
	if(IsPredicateForm(iter->form))
		return NextPredicatePermutation(iter->topIterator);
	else if(IsClauseForm(iter->form))
		return NextClausePermutation(iter->topIterator);
	else if(IsConjunctionForm(iter->form))
		return NextConjunctionPermutation(iter->topIterator);
	else {
		ASSERT(false);
		return false;
	}
}


void FreeFormIterator(const FormIterator* iter)
{
	if(IsPredicateForm(iter->form))
		FreePredicateIterator(iter->topIterator);
	else if(IsClauseForm(iter->form))
		FreeClauseIterator(iter->topIterator);
	else if(IsConjunctionForm(iter->form))
		FreeConjunctionIterator(iter->topIterator);
	else
		ASSERT(false);
	free((void *) iter);
}


/**
 * Get tuple permutation vector from form permutation iterator.
 * Writes to the supplied permutation array, which must be at least n bytes
 * for a form with arity n.
 */
void GetTuplePermutation(const FormIterator * iter, index8 * permutation)
{
	// TODO: the permutation vector should probably be supplied by caller
	if(IsPredicateForm(iter->form))
		getPredicatePermutation(iter->topIterator, permutation, 0);
	else if(IsClauseForm(iter->form))
		getClausePermutation(iter->topIterator, permutation, 0);
	else if(IsConjunctionForm(iter->form))
		getConjunctionPermutation(iter->topIterator, permutation);
	else
		ASSERT(false);
}
