
#ifndef FORMPERMUTATION_H
#define FORMPERMUTATION_H

/**
 * TODO: form permutations currently use a complicated data structure
 * with heavy use of malloc; this could be simplified.
 * 
 * We might represent a form permutation as a tree. For example, a
 * permutation of the clause form
 * 
 * ((+^2 =)^2 | odd)
 * 
 * can be represented as a root node with two children, of which the first
 * corresponding to (+^2 =)^2 is a 2-permutation and the second is a
 * (trivial) 1-permutation.
 * Each child of the 2-permutation corresponds to the predicate form (+^2 =),
 * which again has two children, one 2-permutation and one 1-permutation.
 * Conjunction forms have one additional level. We can store this tree
 * linearized in breadth-first form as a list. The identity permutation for
 * the above form is the list
 * 
 * 1 2, 1; 1 2, 1, 1 2, 1, 1
 * 
 * Here , indicates end of a node and ; indicates end of a level in the tree.
 * To indicate permuting the two predicates (+^2 =) we write
 * 
 * 2 1, 1; 1 2, 1, 1 2, 1, 1
 * 
 * If we also want premute predicate 1 (in the original formula) we write
 *
 * 2 1, 1; 2 1, 1, 1 2, 1, 1
 * 
 * The single 1's for 1-permutations are uninformative but makes algorithms
 * somewhat easier to implement. The advantage of this "implicit tree"
 * layout is that we don't have to move around "blocks" of permutations
 * corresponding to sub-arrays.
 * 
 * To find a permutation match between two clauses, we proceed recursively.
 * First, we try the clause permutation (1 2, 1), checking for a permutation
 * of each predicate; if this fails we try (2 1, 1); if this also fails there
 * is not match.  To find a permutation between two predicates, we similarly
 * check permutations of roles.
 * 
 * Also, we should "fail fast" by first checking 1-permutations; if these fail, there
 * is no match. So we should proceed in order of increasing multiplicity.
 * 
 * To extract the tuple order from the tree, we simply traverse it in the order
 * indicated by the permutation at each node. The leaves of the tree correspond
 * to actors. The above tree has 7 actors, say (a b c d e f g). The permutation
 * 
 *  2 1, 1; 2 1, 1, 1 2, 1, 1
 *          a b  c  d e  f  g
 * 
 * Gives the traversal (d e f b a c g).
 */

// NOTE: all the below data structures are needed solely to keep track of
// permutation array lengths, number of roles, and arity. Can we 

typedef struct s_Permutation {
	size8 size;
	index8 order[];
} Permutation;


/**
 * Iterator over the possible permutations of a predicate
 */
typedef struct s_PredicateIterator
{
	size8 nUniqueRoles;
	size8 arity;
	Permutation ** rolePerm;			// array of permutations, 1 per unique role
} PredicateIterator;

PredicateIterator* CreatePredicateIterator(Datum predicateForm);
bool NextPredicatePermutation(PredicateIterator * iter);
void FreePredicateIterator(PredicateIterator * iter);


/**
 * Iterator over the possible permutations of a clause
 */
typedef struct s_ClauseIterator
{
	size8 nUniqueTerms;
	size8 arity;
	Permutation ** predFormPerm;		// array of permutations, 1 per unique term
	PredicateIterator *** predIter;		// iterators for each predicate form and multiple
} ClauseIterator;


ClauseIterator * CreateClauseIterator(Datum clauseForm);
bool NextClausePermutation(ClauseIterator * iter);
void FreeClauseIterator(ClauseIterator * iter);


/**
 * Iterator over all possible permutations of a conjunction
 */
typedef struct s_ConjunctionIterator
{
	size8 nClauses;
	Permutation ** clauseFormPerm;		// array permutations for each clause form
	ClauseIterator *** clauseIter;		// iterators for each clause form an multiple
} ConjunctionIterator;


ConjunctionIterator * CreateConjectionIterator(Datum conjunctionForm);
bool NextConjunctionPermutation(ConjunctionIterator * iter);
void FreeConjunctionIterator(ConjunctionIterator * iter);


typedef struct s_FormIterator {
	Datum form;
	void * topIterator;
} FormIterator;

// permute an array of indexes a, a+1., ..., a+n-1
bool PermuteArray(index8 * perm, index8 a, size8 n);

FormIterator * CreateFormIterator(Datum form);
bool NextFormPermutation(FormIterator* iter);
// get corresponding tuple permutation vector
void GetTuplePermutation(const FormIterator * iter, index8 * permutation);
void FreeFormIterator(const FormIterator* iter);


#endif	// FORMPERMUTATION_H
