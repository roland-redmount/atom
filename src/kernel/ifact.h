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

#include "kernel/ServiceRegistry.h"
#include "kernel/typedtuple.h"


struct s_Service;

/**
 * A conjunction is a set of facts from a single relation table.
 * 
 * TODO: perhaps rename this IFactRelation, as it corresponds 1:1 to
 * a set of tuples from a specific (typed) relation.
 * The entire IFact is a conjuction ...
 */
typedef struct s_IFactConjunction {
	// relation table storing tuples for this conjunction
	RelationTable const * table;
	// operator for retrieving existing tuples
	Operator const * op;
	index8 idColumn;		// position of the identified atom in the tuple
	byte pad;
	size16 nRows;			// number of tuples in this conjunction
} __attribute__((packed)) IFactConjunction;


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


/**
 * A "draft" ifact, used while constructin a new ifact.
 */
typedef struct s_IFactDraft {
	Atom * tupleStorage;
	Atom * currentTuple;
	IFactHeader header;		// IFact being constructed
	bool hasBegunConjunction;
} IFactDraft;

/**
 * Setup ifact storage.
 */
void InitializeIFacts(void);

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
 * Begin a new conjunction for the IFactDraft, storing tuples in the given table.
 * The idColumn indicates the actor that is being defined by the IFact.
 */
void IFactBeginConjunction(IFactDraft * draft, RelationTable const * table, index8 idColumn);

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
 * dependency between the core predicate forms and the relation tables that
 * store their defining facts: a table created by CreateRelationTable()
 * acquires a reference to its form, but that form's IFact cannot be built
 * until the table itself exists.
 *
 * A reserved header holds no conjunctions and can only be acquired and
 * released; it must be finalized by a matching IFactEndBootstrap() call with
 * the same hash before the identified atom is used for anything else.
 */
void IFactReserve(data64 hash);

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
 * Dump all created IFacts, for debugging.
 */
void IFactDump(void);

/**
 * Flagging of newly created ifacts, for debugging.
 */
void IFactsEnableFlagging(void);

void IFactDumpFlagged(void);

void IFactDisableFlagging(void);



#endif  // IFACT_H
