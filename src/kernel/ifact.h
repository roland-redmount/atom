/**
 * An identifying fact (ifact) is a conjunction of predicates
 * across one or more relations that uniquely identify an atom.
 * To create an identifying fact, we must (1) build the conjunction
 * (2) compute a hash value from the conjunction (3) create the
 * corresponding facts (protected from deletion) across one or more
 * tables. The hash serves as the identified atom ID.
 * 
 * Predicates that are part of an identifying fact cannot be retracted
 * until the identified atom is released.
 */

#ifndef IFACT_H
#define IFACT_H

#include "kernel/TypedRelation.h"
#include "kernel/typedtuple.h"


/**
 * A conjunction is a set of facts from a single relation.
 * 
 * TODO: perhaps rename this IFactRelation, as it corresponds 1:1 to
 * a set of tuples from a specific (typed) relation.
 * The entire IFact is a conjuction ...
 */
typedef struct s_IFactConjunction {
	TypedRelation relation;
	size8 nColumns;
	index8 idColumn;		// position of the identified atom in the tuple
	size16 nRows;			// number of tuples in this conjunction
	// CLAUDE: nRows tuples of nColumns atoms each, in canonical order, ordered by
	// TupleCompare(). The tuples are allocated with Allocate(), and hold a reference to
	// each atom except the identified atom; see IFactAddTuple().
	Atom * tuples;
} IFactConjunction;


/**
 * This header (record) keeps track of references to the identified atom.
 * The ifact hash value is the key for retrieval from the ifact B-tree.
 */
typedef struct s_IFactHeader IFactHeader;

struct s_IFactHeader {
	data64 hash;				// 8 bytes
	size8 nConjunctions;
	data8 flags;
	data8 reserved[2];			// pad to even 4-byte
	uint32 refCount;
	IFactConjunction * conjunctions;	// pointer, 8 bytes
} __attribute__((packed)) ;

#define IFACT_NEW		1
// header reserved by IFactReserve(), defining facts not yet built
#define IFACT_RESERVED	2
// indicates one reference to the ifact created by the IFACT operator; see IFactMarkCached()
#define IFACT_CACHED	4


/**
 * A "draft" ifact, used while constructing a new ifact.
 */
typedef struct s_IFactDraft {
	IFactHeader header;		// IFact being constructed
	bool hasBegunConjunction;
} IFactDraft;

/**
 * Setup ifact storage.
 */
void InitializeIFacts(void);

/**
 * Restore ifact storage from the paging area upon restart.
 */
void RestoreIFacts(void);

/**
 * Check if ifact storage has been initialized.
 */
bool IFactsInitialized(void);

/**
 * Teardown ifact storage.
 */
void FreeIFacts(void);

/**
 * Creating an IFact is done in a series of calls.
 * This function must be called first to begin creating a new IFact
 */
void IFactBegin(IFactDraft * draft);

/**
 * Begin a new conjunction for the IFactDraft, storing tuples in the given TupleStore.
 * The idColumn indicates the actor that is being defined by the IFact.
 * The new conjunction acquires a reference to the given RelationWriter.
 * 
 * NOTE: this function may register a new service for the TupleStore's relation
 * with the idColumn as the single input, if no such service already exists.
 */
void IFactBeginConjunction(IFactDraft * draft, TypedRelation relation, index8 idColumn);

/**
 * Add one tuple, defining one predicate of the current conjunction (predicate form).
 * The atom in the ID column of the conjunction is ignored; it will
 * be computed by IFactEnd()
 */
void IFactAddTuple(IFactDraft * draft, Atom const tuple[]);

/**
 * End the current predicate form. This function must be called before
 * calling IFactEnd(). Returns the number of predicates for this form.
 */
size32 IFactEndConjunction(IFactDraft * draft);

/**
 * Return the number of tupled added so far for the current predicate form.
 * IFactBeginPredicateForm() must be called before calling this function.
 */
size32 IFactDraftCurrentNTuples(IFactDraft * draft);

/**
 * Finalize the IFactDraft to create an AT_ID atom. Computes the AT_ID atom's
 * hash from all identifying facts, asserts all facts and returns the AT_ID atom.
 * The caller obtain a reference to the returned AT_ID atom.
 */
Atom IFactEnd(IFactDraft * draft);

/**
 * This variant of IFactEnd() is only used during bootstrapping.
 * Here, a precomputed AT_ID hash must be provided, and the given assertFact()
 * is used to assert identifying facts instead of the standard AssertFact().
 */
Atom IFactEndBootstrap(IFactDraft * draft, data64 hash);

/**
 * Reserve an IFact header with a predefined hash, before its defining facts
 * exist. This is only used during bootstrapping, to break the circular
 * dependency between the core predicate forms and the relations that
 * store their defining facts: a relation created by AcquireRelation()
 * acquires a reference to its form, but that form's IFact cannot be built
 * until the relation itself exists.
 *
 * A reserved header holds no conjunctions and can only be acquired and
 * released; it must be finalized by a matching IFactEndBootstrap() call with
 * the same hash before the identified atom is used for anything else.
 */
void IFactReserve(data64 hash);

/**
 * Mark an ifact as cached, taking over the reference the caller holds.
 * A cached ifact holds exactly one reference owned by the cache. If the ifact is
 * already cached, the caller's reference is released instead.
 * This is required for ifacts created by the IFACT operator. See OPERATOR_IFACT in operator.h.
 */
void IFactMarkCached(Atom ifact);

/**
 * Release the cache reference of every cached ifact consisting of a single
 * conjunction with the given ID column, on a relation of the given form.
 * Clear the IFACT_CACHED flag of each such ifact.
 * An ifact with no other reference is removed, together with its tuple.
 */
void IFactReleaseCached(Atom form, index8 idColumn);

/**
 * Return the number of tuples of all ifacts on the given relation.
 * 
 * NOTE: this scans all ifacts in the system.
 */
size32 IFactRelationNTuples(TypedRelation relation);

/**
 * Register a primitive service reading the ifacts on the given relation, with the
 * identified atom in idColumn. The service has idColumn as its only input, and yields the
 * tuples of the given ifact on the relation. The service holds the relation, and the
 * relation has no TupleStore. A relation with such a service holds ifacts only;
 * see AssertFact(). The service must not already exist.
 */
void IFactCreateReaderService(TypedRelation relation, index8 idColumn);

/**
 * Test if the relation has a service created by IFactCreateReaderService().
 */
bool IFactRelationHasReader(TypedRelation relation);

/**
 * Prepare the relation to hold ifacts with the identified atom in idColumn, and
 * return true if the relation can hold them. A relation with a TupleStore cannot hold
 * ifacts, since the TupleStore holds asserted facts. If the relation has no reader for
 * idColumn, the reader is created with IFactCreateReaderService(), and *createdReader is
 * set to true.
 */
bool IFactSetupRelation(TypedRelation relation, index8 idColumn, bool * createdReader);

/**
 * Acquire a reference to an AT_ID atom.
 */
void IFactAcquire(Atom ifact);

/**
 * Release a reference to an AT_ID atom. When the last reference is released,
 * the underlying identifying facts are retracted.
 */
void IFactRelease(Atom ifact);

/**
 * Test if an AT_ID atom with the hash of the given atom is stored.
 */
bool IFactExists(Atom ifact);

/**
 * Reference counts
 */
uint32 IFactReferenceCount(Atom ifact);

uint32 IFactTotalReferenceCount(void);

/**
 * Total number of IFact headers stored.
 */
uint32 IFactTotalCount(void);

/**
 * Print an AT_ID atom
 */
void IFactPrint(Atom ifact);

/**
 * A callbavk used to modify the default output of IFactPrint(). If this function
 * cannot print the given atom, it must returns false; IFactPrint() then prints the default output.
 */
typedef bool (*IFactPrinter)(Atom ifact);

/**
 * Set the print function used by IFactPrint() for AT_ID atoms.
 * Passing printer = 0 restores the default output of IFactPrint().
 */
void IFactSetPrinter(IFactPrinter printer);

/**
 * Dump all created IFacts, for debugging.
 */
void IFactDump(void);

/**
 * Flagging of newly created ifacts, for debugging.
 */
void IFactsEnableFlagging(void);

void IFactDumpFlagged(void);

void IFactDisableFlagging(void);

/**
 * Register functions referred to from persistent memory.
 */
void RegisterIFactFunctions(void);


#endif  // IFACT_H
