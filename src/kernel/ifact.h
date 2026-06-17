/**
 * An identifying fact (ifact) is a conjunction across one or more relations
 * that uniquely identify an atom. To create an identifying fact,
 * we must process a formula (conjunction), locate or create the
 * corresponding tuples across one or more tables, mark them as
 * protected from deletion, and compute a hash value of the formula.
 * This hash will serve as the identified atom ID.
 * 
 * Tuples that are part of an identifying fact cannot be retracted
 * until the identified atom is released.
 */

#ifndef IFACT_H
#define IFACT_H

#include "kernel/RelationBTree.h"
#include "kernel/typedtuple.h"

/**
 * A conjunction with a given form &'d together n times,
 * 
 * form & form & ... & form   (n times)
 * 
 * Corresponding tuples are stored in the BTree.
 */
typedef struct s_IFactConjunction {
	Atom form;				// clause or predicate form for the relation
	BTree * btree;			// B-tree storing the relation
	index8 idColumn;		// these 3 fields total 4 bytes
	size8 nColumns;
	size16 nRows;
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


/**
 * A "draft" ifact, used while building a ifact
 */
typedef struct s_IFactDraft {
	byte * tupleStorage;
	byte * currentTuple;
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
 * Begin a new predicate form for the IFactDraft. Each tuple added by
 * IFactAddTuple() have this form.
 * The idColumn identifies the actor that is being defined by the IFact.
 * TODO: factor out the BTree * 
 */
void IFactBeginPredicateForm(IFactDraft * draft, Atom predicateForm, BTree * btree, index8 idColumn);

/**
 * Add one tuple, defining one predicate of the current predicate form.
 * The atom in the ID column of the identified fact is ignored; it will
 * be computed by IFactEnd()
 */
void IFactAddTuple(IFactDraft * draft, TypedTuple const * tuple);

/**
 * End the current predicate form. This function must be called before
 * calling IFactEnd(). Returns the number of predicates for this form.
 */
size32 IFactEndPredicateForm(IFactDraft * draft);

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
Atom IFactEndBootstrap(IFactDraft * draft, data64 hash, void (* assertFact)(Atom, TypedTuple const *));

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
 * Check if adding the given tuple to the BTree would violate an ifact definition.
 * This occurs if the tuple contains an IFact atom whose identifying fact also contains
 * a tuple with that same atom in the same column.
 * 
 * For example, if the (list postion element) relation contains the tuple
 * 
 * (@cat 1 'c')
 * 
 * where @cat is a IFact (with the ATOM_PROTECTED flag set), it is illegal to add a tuple
 * of the form (@cat _ _), but it is legal to add a tuple like where @cat is in a different column,
 * like (@my-cats 1 @cat).
 * 
 * This check does not apply to atoms with the ATOM_PROTECTED flag set, which are themselves
 * part of IFact tuples.
 */

// TODO: this should take a form atom, not a tree pointer
bool IFactCheckTuple(BTree const * tree, TypedTuple const * tuple);

void IFactPrint(Atom ifact);

void IFactDump(void);

/**
 * Flagging of newly created ifacts, for debugging.
 */
void IFactsEnableFlagging(void);

void IFactDumpFlagged(void);

void IFactDisableFlagging(void);



#endif  // IFACT_H
