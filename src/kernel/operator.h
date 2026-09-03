
#ifndef OPERATOR_H
#define OPERATOR_H

#include "kernel/Relation.h"

struct s_Releation;

typedef struct s_Operator Operator;
typedef struct s_OperatorContext OperatorContext;

/**
 * A MachineOperatorProvider is an implementation of a particular type of machine
 * operator, such as B-Tree relations or arithmetic functions.
 * One MachineOperatorProvider can provide the machine operators of several relations.
 */
typedef struct s_MachineOperatorProvider {
	/**
	 * Initialize the context data, such as an iterator structure.
	 * This pointer may be 0 if the zeroed context data needs no initialization.
	 */
	void (*setupContext)(OperatorContext * context);

	/**
	 * Call (resume) an executing operator, return true if a tuple was produced,
	 * false if evaluation terminated. The call() function must write to the 
	 * arguments tuple, so the context must keep a pointer to this tuple.
	 * If the various operators provided need different entry points, this function
	 * is responsible for calling the relevant one.
	 */
	bool (*call)(OperatorContext * context);

	/**
	 * Finalize an operator context after termination.
	 * This pointer may be 0 if no finalization is required.
	 */
	void (*finalizeContext)(OperatorContext * context);

	/**
	 * Finalize the machine operator (deallocate data structures, &c), called once when
	 * the last reference to the operator is released.
	 * This pointer may be 0 if no finalization is required.
	 */
	void (*finalizeOperator)(Operator * op);

} MachineOperatorProvider;


/**
 * An operator is either a machine procedure or an operation on relations.
 * The compiler produces a tree of operators for a query, which is what a
 * service in the service registry is evaluated by; see ServiceRegistry.h.
 * A tree may contain any operator of the service registry, and operators are
 * therefore reference counted.
 *
 * An operator is evaluated stepwise, at each call yielding one tuple,
 * similar to a co-routine.
 *
 * Every operator defines an index order, which is a permutation of its arguments
 * such that tuples are lexiographically ordered. For example, an operator
 * with arguments (a b c) and index order {1, 0, 2} orders its tuples lexiographically
 * w.r.t. (b a c). The tuples an operator yields must be distinct and strictly
 * ascending w.r.t. its index order. This lets UNION merge two relations "streaming"
 * (without materializing the entire union relation), and could in the future allow
 * streaming PROJECT or merge JOIN operators.
 *
 * The relational operators compute their index order from their children; only
 * MACHINE operators specify index order explicitly. Correctness of index order declared
 * by a MACHINE operator cannot be verified statically; it is the responsibility of whoever
 * implements the machine provider to produce correctly ordered relations. In DEBUG
 * builds, OperatorCall() verifies the ascent of every operator, which catches a
 * violation at the operator that committed it.
 *
 * An operator yielding at most one tuple satisfies the contract under every index
 * order, and so has no order worth declaring: in this case indexOrder should be set to 0
 * rather than picking an arbitrary order. This matters because an arbitrary choice would
 * propagate through the operators above and be mistaken for a real constraint, so that two
 * relations orderable alike could appear not to be. An operator deriving its order
 * from a child with indexOrder == 0 uses the natural ordering, unless it too yields at
 * most one tuple. DEBUG builds verify the indexOrder == 0 claim by asserting that at most
 * one tuple is produced.
 */

/**
 * Apart from machine operators, which provide the stored and computed relations
 * at the leaves of an operator tree, each operator is one of the operators of
 * relational algebra, applied to the relations of its child operators:
 *
 *   PERMUTE      rename, and restrict on a constant argument     rho, sigma
 *   CONSTRAIN    restrict on an equality between arguments       sigma
 *   FILTER       restrict on an argument the caller bound        sigma
 *   JOIN         inner join                                      join
 *   PROJECT      projection                                      pi
 *   UNION        set union                                       union
 *
 * The three operators taking an argument map differ in what their map may do:
 * a permute operator may take a child argument from a constant, a constrain
 * operator may take several child arguments from one argument, and a join
 * operator does neither.
 *
 * Every operator provides all of its arguments and yields distinct tuples, and
 * so yields a valid relation. An operator can therefore be applied to any other
 * without regard for how that one was composed.
 */
 enum OperatorType {
	/**
	 * PERMUTE calls a child operator with its arguments reordered, and may bind
	 * constants to child arguments. Every child argument is either taken from a parent
	 * argument or a constant, so PERMUTE never drops a child argument and hence never
	 * introduces duplicate tuples.
	 */
	OPERATOR_PERMUTE = 1,
	
	/**
	 * JOIN is the inner join of the relations of two child operators, with equality
	 * constraint on the arguments they have in common. Each child operator has its own
	 * argument map and so keeps its own arity; an argument occurring in both maps is a join
	 * argument, whose value the left child determines and the right child is then
	 * constrained by.
	 */
	OPERATOR_JOIN = 2,

	/**
	 * UNION gives the set union of the tuple sets from two child operators.
	 * It is assumed that each child operator produces tuples in sorted order.
	 *
	 * NOTE: if operators are required to be distinct (using preconditions)
	 * then we should never have duplicate tuples in a UNION.
	 */
	OPERATOR_UNION = 3,

	/**
	 * PROJECT is the projection onto the child arguments named by its argument map:
	 * it drops the remaining ones and removes the duplicate tuples that dropping may
	 * produce. Its tuples are yielded in sorted order. If all child arguments are kept,
	 * PROJECT merely sorts the child tuples according to the new index order;
	 * see CreateProjectOperator().
	 */
	OPERATOR_PROJECT = 4,

	/**
	 * CONSTRAIN is a restriction on an equality between arguments: it yields those
	 * tuples of its child operator in which all child arguments taken from the same
	 * argument of this operator are equal. This expresses the equality constraint of
	 * a variable occurring more than once in a query, such as (edge e from x to x)
	 * asking for the self edges of a graph.
	 *
	 * NOTE: this is the only operator whose call may consume several child tuples,
	 * as it can only test the constraint once the child operator has produced a tuple.
	 */
	OPERATOR_CONSTRAIN = 5,

	/**
	 * FIXPOINT evaluates a recursive clause. Its child operator must contains a RECURSE
	 * operator as a descendant. It repeatedly applies its child operator to the tuples
	 * derived so far and accumulates the results in a B-tree, until no new tuples are produced.
	 * 
	 * until nothing new
	 * tuple. The tuples are accumulated in a
	 * B-tree, which the RECURSE operators in the child subtree read.
	 *
	 * Recursion is therefore a loop rather than a cycle in the operator tree, and terminates
	 * whenever the derived relation is finite.
	 *
	 * The child derives the whole relation and so runs with every argument unbound.
	 * The arguments the caller binds restrict the tuples this operator yields, not
	 * the ones it derives, which is why one derived relation can serve every
	 * signature over it.
	 *
	 * NOTE: nothing here guarantees termination. A relation over an infinite domain
	 * has no finite fixpoint, and needs the recursive rule guarded by a precondition
	 * to terminate; see the notes on termination in compiler.md.
	 * 
	 * NOTE: this is a naive iteration scheme, which typically re-evaluates the same call
	 * many times over. Semi-naive iteration is an optimization used in Datalog that
	 * would improve upon this. See for example
	 * https://stackoverflow.com/questions/47043937/what-is-the-difference-between-naive-and-semi-naive-evaluation
	 */
	OPERATOR_FIXPOINT = 6,

	/**
	 * RECURSE is the recursive occurrence of the relation that an enclosing FIXPOINT
	 * operator is deriving: it enumerates the tuples derived by the rounds so far.
	 * It is always a leaf. The enclosing FIXPOINT operator is found via the
	 * chain of parent pointers in the operator context when the recursion is evaluated.
	 */
	OPERATOR_RECURSE = 7,

	/**
	 * FILTER yields those tuples of its child operator that agree with the arguments the
	 * caller bound, at the arguments named by inputArguments. Its child produces those
	 * arguments as outputs, which is what distinguishes FILTER from every other operator:
	 * an argument the caller binds is normally passed down to be sought.
	 *
	 * This is how a service is built for an IO pattern no service provides. A relation is
	 * read by the services its storage registered, and a B-tree registers one per prefix
	 * of its index column order, so a pattern binding a column out of that order has no
	 * service; see compileFilterVariants() in compiler.c.
	 *
	 * NOTE: filtering reads every tuple the child yields, so it is a scan over whatever
	 * the child does bind. The child binding the most is therefore the one to read; see
	 * DispatchFilterableQuery().
	 */
	OPERATOR_FILTER = 8,

	/**
	 * Call a machine code function. Leaf of the operator tree
	 */
	OPERATOR_MACHINE = 9,
};

struct s_Operator {
	enum OperatorType type;
	// Number of arguments for this operator
	size8 nArguments;
	// Permutation of the argument indices giving the order in which this operator
	// yields its tuples; see the ordering contract above. Length nArguments,
	// or null if this operator yields at most one tuple and so declares no order.
	index8 * indexOrder;
	// Context size, in addition to sizeof(Context)
	size32 contextSize;
	size32 nParents;		// number of parent operators
	// This pointer is nonzero only for a service's root operator,
	// and is used only to locate that service.
	const struct s_Relation * relation;
	union {
		// for OPERATOR_PERMUTE
		struct {
			Operator * childOperator;
			// Stored constants and their atom types, addressed by the argument map
			Atom * constants;
			byte * constantTypes;
			size8 nConstants;
			// Source of each child argument: an index below nArguments is a parent
			// argument, an index of nArguments or above is constants[index - nArguments].
			// Each parent argument occurs once; taking several child arguments from
			// one argument is what a constrain operator expresses.
			index8 * argumentMap;
		} permute;
		// for OPERATOR_JOIN
		struct {
			Operator * left;
			Operator * right;
			// Indices of each child argument into the parent arguments.
			// Unlike a permute operator, every child argument maps to a parent
			// argument: a join operator neither binds constants nor drops arguments.
			index8 * leftMap;
			index8 * rightMap;
		} join;
		// for OPERATOR_UNION
		struct {
			Operator * first;
			Operator * second;
		} _union;
		// for OPERATOR_PROJECT
		struct {
			Operator * childOperator;
			// Index of the child argument kept by each argument of this operator.
			// The child arguments it does not name are the dropped ones.
			index8 * argumentMap;
		} project;
		// for OPERATOR_CONSTRAIN
		struct {
			Operator * childOperator;
			// Index of each child argument into the parent arguments. Unlike the
			// other argument maps this one is not injective: child arguments
			// sharing an index are the ones constrained to be equal.
			index8 * argumentMap;
		} constrain;
		// for OPERATOR_FIXPOINT
		struct {
			Operator * childOperator;
			// Indices of the arguments the caller binds, restricting the tuples yielded
			index8 * inputArguments;
			size8 nInputs;
		} fixpoint;
		// for OPERATOR_RECURSE
		struct {
			// Indices of the arguments the caller binds, as for a fixpoint operator
			index8 * inputArguments;
			size8 nInputs;
		} recurse;
		// for OPERATOR_FILTER
		struct {
			Operator * childOperator;
			// Indices of the arguments the caller binds, which the child operator
			// produces as outputs and this operator tests against the bound value
			index8 * inputArguments;
			size8 nInputs;
		} filter;
		// for OPERATOR_MACHINE
		struct {
			MachineOperatorProvider * provider;
			void * providerData;
		} machine;
	} impl;
};

/**
 * Create a permute operator with the specified number of arguments.
 * The argumentMap array has length equal to childOperator->nArguments and gives the
 * source of each child argument: an index below nArguments is the index of a parent
 * argument, an index of nArguments or above refers to constants[index - nArguments].
 *
 * The constants and constantTypes arrays have length nConstants, and may be 0 if
 * there are none. The operator acquires a reference to each constant.
 *
 * The child operator must provide every parent argument exactly once, so that every
 * parent argument occurs in argumentMap, and none occurs twice. An argument this
 * operator does not write would be left at whatever the caller had in the arguments
 * tuple, and so would not be part of a relation; taking several child arguments from
 * one argument is what a constrain operator expresses.
 *
 * The index order is the child's, relabeled through the argument map; child arguments
 * bound to a constant drop out of it, being equal in every tuple.
 */
Operator * CreatePermuteOperator(
	size8 nArguments, Atom const constants[], byte const constantTypes[], size8 nConstants,
	index8 const argumentMap[], Operator * childOperator);

/**
 * Create a machine code operator. The indexOrder array has length nArguments and gives
 * the order in which the provider yields its tuples; see the ordering contract above.
 * A provider yielding at most one tuple declares no order and passes 0.
 * The context size is the size of the context data allocated by OperatorCreateContext().
 * The returned operator has zero references.
 */
Operator * CreateMachineOperator(
	size8 nArguments, index8 const indexOrder[], MachineOperatorProvider * provider,
	void * providerData, size32 contextSize);

/**
 * Setup a JOIN operator with the specified number of arguments, from two existing
 * child operators. The left child operator will execute first, and may determine
 * input arguments for the right child operator.
 *
 * The leftMap and rightMap arrays have length equal to the number of arguments of
 * the respective child operator, and give for each child argument its index
 * into the parent arguments tuple. An argument occurring in both maps is a join
 * argument: the left child operator determines its value, which then constrains
 * the right child operator.
 *
 * The two child operators must together provide every parent argument, so that every
 * parent argument occurs in leftMap or rightMap: an argument neither child writes
 * would be left at whatever the caller had in the arguments tuple. Neither map may
 * contain the same argument twice; taking several child arguments from one argument
 * is what a constrain operator expresses.
 *
 * The index order is the left child's order, followed by the right child's order minus
 * the join arguments: the left child determines the major key, as it yields ascending
 * and the join drops none of its arguments, while within one left tuple the join
 * arguments are constant and the right child orders the remainder.
 */
Operator * CreateJoinOperator(
	size8 nArguments,
	Operator * leftChild, index8 const leftMap[],
	Operator * rightChild, index8 const rightMap[]);

/**
 * Setup a UNION operator, returning the union of two relations.
 * Merging two ordered relations requires that they are ordered alike, so the two child
 * operators must have the same index order, which this operator adopts. A child
 * declaring no order is ordered alike with any other, and takes the order of its sibling.
 */
Operator * CreateUnionOperator(Operator * first, Operator * second);

/**
 * Create a CONSTRAIN operator with the given number of arguments.
 * The argumentMap array has length equal to childOperator->nArguments and gives the
 * index of each child argument into the arguments of this operator. Child arguments
 * sharing an index are constrained to be equal: only those tuples of the child
 * operator in which they are equal are yielded.
 *
 * The child operator must provide every argument, as for a permute operator.
 *
 * The index order is the child's, with the arguments collapsed by the constraint
 * appearing once: they are equal in every yielded tuple.
 */
Operator * CreateConstrainOperator(
	size8 nArguments, index8 const argumentMap[], Operator * childOperator);

/**
 * Create a FILTER operator over the given child operator, yielding those of its tuples that
 * agree with the arguments the caller bound; see OPERATOR_FILTER.
 *
 * The inputArguments array holds the indices of the nInputs arguments the caller binds and
 * this operator tests, which the child operator produces as outputs. The child must produce
 * every one of them, or the tuples yielded would not be restricted as the caller asked.
 *
 * A filter keeps the arity of its child, dropping tuples rather than arguments, and yields
 * them in the child's index order: dropping tuples from a strictly ascending sequence leaves
 * it strictly ascending.
 */
Operator * CreateFilterOperator(
	Operator * childOperator, index8 const inputArguments[], size8 nInputs);

/**
 * Create a PROJECT operator with the given number of arguments, which may not exceed the
 * number of arguments of the child operator. The argumentMap array has length nArguments,
 * and argument i of the returned PROJECT operator will map to argumentMap[i] of childOperator. 
 * Child arguments that are not in the argumentMap will be dropped, and any resulting duplicate
 * tuples will be dropped as well. The index order of the PROJECT operator is always identity.
 * If argumentMap contains all child arguments, PROJECT merely sorts the child tuples w.r.t.
 * the identity index order.
 *
 * NOTE: dropping an argument reorders the ones the child ordered below it, so a projection
 * cannot in general be streamed. This operator therefore materializes its child into a
 * B-tree, which both removes the duplicates and sorts tuples.
 */
Operator * CreateProjectOperator(
	Operator * childOperator, size8 nArguments, index8 const argumentMap[]);

/**
 * Create a FIXPOINT operator deriving a recursive relation from the given child
 * operator, which computes the rule bodies and must have the arity of the relation.
 * The child is applied to the tuples derived so far until a round derives nothing new;
 * the RECURSE operators in its subtree read those tuples.
 *
 * The inputArguments array holds the indices of the input arguments, and may be 0 if
 * nInputs = 0. The input arguments are used to filter the tuples yielded by the child operator.
 * 
 * QUESTION: could this be handled by a surrounding FILTER operator to simplify? Or would
 * this incur additional overhead by filtering later?
 *
 * The derived tuples are accumulated in a B-tree, so this operator yields them
 * distinct and in the identity index order.
 */
Operator * CreateFixpointOperator(
	Operator * childOperator, index8 const inputArguments[], size8 nInputs);

/**
 * Create a RECURSE operator enumerating the tuples derived so far by the nearest FIXPOINT
 * operator enclosing it, which must have the same arity.
 *
 * The inputArguments array holds the indices of the nInputs arguments the caller binds,
 * as for a fixpoint operator; a recursive term with a bound argument, which is the usual
 * case below a join, takes those indices here.
 *
 * NOTE: taking the nearest enclosing fixpoint is what makes one recursive relation work.
 * Relations recursive through one another will need this operator to name which fixpoint
 * it belongs to.
 *
 * NOTE: inputArguments is here written as a pointer rather than as an array since
 * compileRecursiveTerm() passes a variable length array, and array syntax causes an optimized
 * build to read that as a zero length region; see -Wstringop-overread.
 */
Operator * CreateRecurseOperator(
	size8 nArguments, index8 const * inputArguments, size8 nInputs);

/**
 * Number of tuples a fixpoint operator context has derived, which is how much of the
 * relation the query it is answering depended on. The context must not have been freed.
 *
 * This is what distinguishes a derivation driven by the call bindings from one that
 * derives the whole relation and filters, as the two yield the same tuples; it is
 * otherwise not observable from outside. Intended for tests.
 */
size32 FixpointNDerivedTuples(OperatorContext const * context);

/**
 * Number of child operators of the given operator, which is 0 for a machine or recurse
 * operator, those being the leaves of an operator tree.
 */
size8 OperatorNChildren(Operator const * op);

/**
 * The child operator at the given index, which must be below OperatorNChildren().
 * The children are given in evaluation order, so the left child of a join operator
 * comes before its right child.
 */
Operator * OperatorGetChild(Operator const * op, index8 index);

/**
 * Attach an operator to a service, specified by its Relation
 */
void AttachOperator(Operator * op, Relation const * relation);

/**
 * Detach an operator from its a service. This may deallocate the operator.
 */
void DetachOperator(Operator * op);

/**
 * Deallocate an operator if it has no parents and no associated Service.
 */
void CheckOperator(Operator * op);


/**
 * Executing an operator consists of setting up an operator execution context,
 * performing one or more calls against that context, and finalizing the context.
 * Sub-operators will have their own execution contexts, which are initialized
 * as necessary.
 */
struct s_OperatorContext {
	Operator const * op;
	Atom * arguments;
	// The context that created this one, or null for a context created by a caller
	// outside the operator tree. A RECURSE operator follows this chain to reach the
	// FIXPOINT operator deriving the relation it enumerates.
	// NOTE: this is redundant with the parent-operator B-tree, but pointer chasing is faster
	OperatorContext * parent;
#ifdef DEBUG
	// The previously yielded tuple, kept to verify that this operator upholds the
	// ordering contract; see OperatorCall(). Null until the first tuple is yielded.
	Atom * previousTuple;
#endif
	byte data[];
};

 /**
  * Create and return an execution context for evaluating an operator
  * with the given arguments tuple. Each OperatorCall() to this context
  * will write its result into the arguments tuple.
  * The context data is zeroed.
  */
OperatorContext * OperatorCreateContext(Operator const * op, Atom arguments[]);

/**
 * Execute an operator with a given context. Returns true if a tuple was produced,
 * or false if no more tuples are available. Once this function has returned false,
 * it must not be called again.
 */
bool OperatorCall(OperatorContext * context);

/**
 * Finalize an operator context, releasing any allocated resources.
 */
void OperatorFreeContext(OperatorContext * context);

/**
 * Initialize an operator context, perform one call, and terminate.
 * The operator must produce at most one tuple for the given arguments.
 * Return true if a tuple was produced.
 */
bool OperatorCallOnce(Operator const * op, Atom arguments[]);

/**
 * Print operator information.
 */
void PrintOperator(Operator const * op);


#endif	// OPERATOR_H
