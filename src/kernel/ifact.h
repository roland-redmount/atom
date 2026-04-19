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
	data8 reserved[3];			// pad to even 4-byte
	uint32 refCount;
	IFactConjunction * conjunctions;	// pointer, 8 bytes
} __attribute__((packed)) ;


/**
 * A "draft" ifact, used while building a ifact
 */
typedef struct s_IFactDraft {
	TypedAtom * tupleStorage;
	TypedAtom * currentTuple;
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
 * Begin a new conjunction for the IFact currently being created.
 * Each clause (row, fact) in the conjunction will have the given form.
 */
void IFactBeginConjunction(IFactDraft * draft, Atom form, BTree * btree, index8 idColumn);

/**
 * Add a tuple that defines one clause of the current conjunction fact.
 * The atom in the column of the identified fact is ignored; it will
 * be computed by calling IFactEnd()
 */
void IFactAddClause(IFactDraft * draft, TypedAtom const * tuple);

/**
 * End the current conjunction. This function must be called before
 * calling IFactEnd(). Returns the number of clauses created.
 */
size32 IFactEndConjunction(IFactDraft * draft);

/**
 * Return the number of clauses in the draft's current conjunction.
 * If IFactBeginConjunction() has not been called, this call is invalid.
 */
size32 IFactDraftCurrentNClauses(IFactDraft * draft);

/**
 * Finish the IFact currently being created. Computes the IFact AT_ID atom's
 * atom as the hash of the identifying facts, creates the facts and
 * returns the AT_ID atom.
 */
Atom IFactEnd(IFactDraft * draft);

// This variant is only used during bootstrapping.
Atom IFactEndBootstrap(IFactDraft * draft, data64 hash, void (* assertFact)(Atom predicateForm, TypedAtom * actors));

void IFactAcquire(Atom ifact);
void IFactRelease(Atom ifact);

uint32 IFactReferenceCount(Atom ifact);
uint32 TotalIFactReferenceCount(void);
uint32 TotalIFactCount(void);


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
bool IFactCheckTuple(BTree const * tree, TypedAtom const * tuple);

void PrintIFact(Atom ifact);

void DumpIFacts(void);


#endif  // IFACT_H
