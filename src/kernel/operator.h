
 #ifndef OPERATOR_H
 #define OPERATOR_H

 #include "lang/Atom.h"


typedef struct s_Operator Operator;
typedef struct s_OperatorContext OperatorContext;

/**
 * A machine provider is an implementation of a particular type of machine
 * operator, such as B-Tree relations or arithmetic functions.
 * One MachineProvider can provide the machine operators of several relations.
 */
 
typedef bool (*MachineProviderCall)(OperatorContext * context);

typedef struct s_MachineProvider {
	/**
	 * Initialize operator-specific context information, such as an iterator structure.
	 * context will point to an allocate block of at least 
	 * This method must return a pointer to its context (or 0 if none).
	 * This context pointer will then be supplied to call() and finalizeContext().
	 */
	void (*setupContext)(OperatorContext * context);

	/**
	 * Call (resume) an executing operator, return true if a tuple was produced,
	 * false if evaluation terminated. The call() function must write to the 
	 * arguments tuple, so the context must keep a pointer to this tuple.
	 * If the various operators provided need different entry points, this function
	 * is responsible for calling the relevant one.
	 */
	MachineProviderCall call;

	/**
	 * Finalize an operator context after termination.
	 * This pointer may be 0 if no finalization is required.
	 */
	void (*finalizeContext)(OperatorContext * context);

	/**
	 * Finalize the machine operator (deallocate data structures, &c).
	 * This pointer may be 0 if no finalization is required.
	 */
	void (*finalizeOperator)(Operator * op);

	size32 contextSize;

} MachineProvider;


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
 * TODO: operators should specify how their tuples are ordered when enumerating.
 * Currently, B-Tree operators order tuples lexiographically, and machine operators
 * do not enforce any particular order. It might be a good idea to provide an
 * array of column numbers specifying the ordering, so that e.g. a relation
 * with form (a b c) might specify ordering = {2, 1, 3} to order lexiographically
 * w.r.t columns (b a c). Knowing the tuple order helps optimize PROJECT: a child
 * ordered on the columns PROJECT keeps lets it drop duplicates by comparing each
 * tuple to its predecessor, instead of materializing the whole child relation.
 *
 */

/**
 * Apart from machine operators, which provide the stored and computed relations
 * at the leaves of an operator tree, each operator is one of the operators of
 * relational algebra, applied to the relations of its child operators:
 *
 *   PERMUTE      rename, and restrict on a constant argument     rho, sigma
 *   CONSTRAIN    restrict on an equality between arguments       sigma
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
	 * PERMUTE is a rename composed with a restriction on constant arguments:
	 * it calls a child operator with its arguments reordered, and may bind
	 * constants to child arguments.
	 * Every child argument is either taken from a parent argument or bound to
	 * a constant, so PERMUTE never drops a child argument and hence never
	 * introduces duplicate tuples.
	 */
	OPERATOR_PERMUTE = 1,
	/**
	 * JOIN is the inner join of the relations of two child operators, on the
	 * arguments they have in common. Each child operator has its own argument map
	 * and so keeps its own arity; an argument occurring in both maps is a join
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
	 * PROJECT is the projection onto the first nArguments arguments of its child
	 * operator: it drops the remaining ones and removes the duplicate tuples that
	 * dropping may produce. Its tuples are yielded in sorted order.
	 */
	OPERATOR_PROJECT = 4,
	/**
	 * Call a machine code function. Machine operators are the leaves of an operator
	 * tree, providing the relations that the operators above are applied to.
	 */
	OPERATOR_MACHINE = 5,
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
	OPERATOR_CONSTRAIN = 6,
};

struct s_Operator {
	enum OperatorType type;
	// Number of arguments for this operator
	size8 nArguments;
	// Context size, in addition to sizeof(Context)
	size32 contextSize;
	size32 referenceCount;
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
		} project;
		// for OPERATOR_CONSTRAIN
		struct {
			Operator * childOperator;
			// Index of each child argument into the parent arguments. Unlike the
			// other argument maps this one is not injective: child arguments
			// sharing an index are the ones constrained to be equal.
			index8 * argumentMap;
		} constrain;
		// for OPERATOR_MACHINE
		struct {
			MachineProvider * provider;
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
 */
Operator * CreatePermuteOperator(
	size8 nArguments, Atom const * constants, byte const * constantTypes, size8 nConstants,
	index8 const * argumentMap, Operator * childOperator);

/**
 * Create a machine code operator
 */
Operator * CreateMachineOperator(size8 nArguments, MachineProvider * provider, void * providerData);

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
 */
Operator * CreateJoinOperator(
	size8 nArguments,
	Operator * leftChild, index8 const * leftMap,
	Operator * rightChild, index8 const * rightMap);

/**
 * Setup a UNION operator, returning the union of two relations.
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
 */
Operator * CreateConstrainOperator(
	size8 nArguments, index8 const * argumentMap, Operator * childOperator);

/**
 * Create a PROJECT operator with the given number of arguments, which must be
 * less than the number of arguments of the child operator. The project operator
 * keeps the first nArguments arguments of the child operator, drops the remaining
 * ones, and removes any duplicate tuples resulting from this.
 */
Operator * CreateProjectOperator(Operator * childOperator, size8 nArguments);

/**
 * Acquire a reference to an operator.
 */
void AcquireOperator(Operator * op);

/**
 * Remove one reference to the given operator, deallocate if references reach zero.
 */
void ReleaseOperator(Operator * op);


/**
 * Executing an operator consists of setting up an operator execution context,
 * performing one or more calls against that context, and finalizing the context.
 * Sub-operators will have their own execution contexts, which are initialized
 * as necessary.
 */
struct s_OperatorContext {
	Operator const * op;
	Atom * arguments;
	byte data[];
};

 /**
  * Create and return an execution context for evaluating an operator
  * with the given arguments tuple. Each OperatorCall() to this context
  * will write its result into the arguments tuple.
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
