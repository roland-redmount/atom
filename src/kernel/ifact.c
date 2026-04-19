
#include "btree/btree.h"
#include "datumtypes/Variable.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/lookup.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/tuples.h"
#include "lang/PredicateForm.h"
#include "lang/Form.h"
#include "lang/Formula.h"
#include "memory/paging.h"
#include "memory/allocator.h"
#include "util/hashing.h"
#include "util/sort.h"


 /**
 * An IFactConjunction stores a block of tuples in a RelationTable,
 * representing a conjunction of facts. Each ifact consists of 1 or more
 * IFactConjunction, stored sequentially.
 * 
 * We must always ensure that the defining fact stays constant over time.
 * For example, if we have an atom @cat with ifact
 * 
 *  list @cat element 1 position @c &
 *  list @cat element 2 position @a &
 *  list @cat element 3 position @t &
 *  list @cat length 3
 * 
 * then the ifact conjunctions store the marginal facts
 * 
 *  list @cat element _ position _
 *  list @cat length _
 * 
 * which is the same information stored by lookup.
 * 
 * TODO: can we avoid storing conjunctions and instead use lookup to
 * retrieve identifying facts when needed? This affects
 * IFactRelease() and sameIFacts()
 */

 /*
 * A complication is that the relation table does not necessarily sort
 * tuples so we may need to copy tuples and sort to compute a hash.
 * The b-tree storage _does_ maintain tuples sorted however.
 * 
 * While constructing an IFact, we cannot add tuples to the relation table
 * until the IFact is complete, as we do not know the atom ID (hash value)
 * until that time. Hence, if we don't store tuples in the IFact structure,
 * we must still have a temporary tuple storage during IFact creation.
 * 
 * (In a more mature implementation, it seems overly restrictive that IFacts
 * must necessarily be defined from tables. For example, a triangle can be
 * defined either by sides or by angles, and although the atom hash must be defined
 * consistently from one relation, say
 * 
 *    triangle *t side @s1 &
 *    triangle *t side @s2 &
 *    triangle *t side @s3
 * 
 * there is no reason why this relation cannot be computed. We must still ensure
 * that the relation cannot be altered.)
 * 
 */


static IFactConjunction * lastConjunction(IFactHeader * header)
{
	return &(header->conjunctions[header->nConjunctions-1]);
}


/**
 * Global ifact storage structure
 * 
 * We store all IFactHeaders in a BTree and perform lookup by their
 * hash value. IFactConjunctions are allocated with Allocate().
 * 
 * TODO: move this to persistent memory.
 */
static struct {
	BTree * btree;					// tree storing IFactHeader structs
	uint32 totalReferenceCount;
} ifactStorage = {0};


static int8 btreeCompareByHash(void const * item1, void const * item2, size32 itemSize)
{
	IFactHeader * ifact1 = (IFactHeader *) item1;
	IFactHeader * ifact2 = (IFactHeader *) item2;
	if(ifact1->hash < ifact2->hash)
		return -1;
	if(ifact1->hash > ifact2->hash)
		return 1;
	return 0;
}


void InitializeIFacts(void)
{
	// check packed data structures
	ASSERT(sizeof(IFactConjunction) == 20);
	ASSERT(sizeof(IFactHeader) == 24);

	SetMemory(&ifactStorage, sizeof ifactStorage, 0);

	ifactStorage.btree = BTreeCreate(
	    sizeof(IFactHeader),
	    btreeCompareByHash,
	    0
	);

	ifactStorage.totalReferenceCount = 0;
}


bool IFactsInitialized(void)
{
	return ifactStorage.btree != 0;
}


/**
 * Lookup an IFactHeader given a hash.
 * Returns a pointer to the stored BTree item, or 0 if not found
 */
static IFactHeader * peekIFactHeader(data64 hash)
{
	IFactHeader query;
	query.hash = hash;
	return BTreePeekItem(ifactStorage.btree, &query);
}


static void acquireIFact(IFactHeader * header)
{
	header->refCount++;
	ifactStorage.totalReferenceCount++;
}


void IFactAcquire(Atom ifact)
{
	IFactHeader * header = peekIFactHeader(ifact);
	ASSERT(header);
	acquireIFact(header);
}


/**
 * When releasing an IFact atom we must retract the identifying facts.
 * Since there is no way to identify (reach/name/construct) the IFact atom anymore,
 * the atom is effectively "deleted" (or rather "forgotten"), and therefore
 * we cannot represent the facts involving the atom.
 */

// create query tuple to match all tuples
static void createQueryTuple(TypedAtom * tuple, Atom ifact, size8 nColumns, index8 idColumn)
{
	for(index8 j = 0; j < nColumns; j++) {
		if(j == idColumn) {
			// query with the ATOM_PROTECTED flag set
			// to indicate a defining fact should be removed
			tuple[j] = (TypedAtom) {.type = AT_ID, .flags = ATOM_PROTECTED, .atom = ifact};
		}
		else
			tuple[j] = anonymousVariable;
	}
}

uint32 IFactReferenceCount(Atom ifact)
{
	IFactHeader * header = peekIFactHeader(ifact);
	ASSERT(header);
	uint32 refCount = header->refCount;
	return(refCount);
}


uint32 TotalIFactReferenceCount(void)
{
	return ifactStorage.totalReferenceCount;
}


uint32 TotalIFactCount(void)
{
	return BTreeNItems(ifactStorage.btree);
}

void FreeIFacts(void)
{
	ASSERT(TotalIFactCount() == 0);
	BTreeFree(ifactStorage.btree);
}


/**
 * Begin constructing a new ifact.
 */
void IFactBegin(IFactDraft * draft)
{
	SetMemory(draft, sizeof(IFactDraft), 0);
	draft->tupleStorage = AllocatePage();
	draft->currentTuple = draft->tupleStorage;
}


void IFactBeginConjunction(IFactDraft * draft, Atom form, BTree * btree, index8 idColumn)
{
	ASSERT(!draft->hasBegunConjunction);

	// append new conjunction to array
	draft->header.nConjunctions++;
	draft->header.conjunctions = Reallocate(
		draft->header.conjunctions,
		draft->header.nConjunctions * sizeof(IFactConjunction)
	);
	IFactConjunction * conjunction = lastConjunction(&(draft->header));
	conjunction->form = form;
	conjunction->btree = btree;
	conjunction->idColumn = idColumn;
	conjunction->nRows = 0;
	conjunction->nColumns = RelationBTreeNColumns(btree);

	draft->hasBegunConjunction = true;
}


void IFactAddClause(IFactDraft * draft, TypedAtom const * tuple)
{
	ASSERT(draft->hasBegunConjunction);
	IFactConjunction * conjunction = lastConjunction(&(draft->header));
	
	// check for overrun
	size32 tuplesSize = ((addr64) draft->currentTuple) - ((addr64) draft->tupleStorage);
	ASSERT(tuplesSize + conjunction->nColumns * sizeof(TypedAtom) <= MEMORY_PAGE_SIZE);

	CopyMemory(tuple, draft->currentTuple, conjunction->nColumns * sizeof(TypedAtom));
	// ensure the identifying column is zero, to not affect hashCurrentIFact()
	draft->currentTuple[conjunction->idColumn] = invalidAtom;

	draft->currentTuple += conjunction->nColumns;
	conjunction->nRows++;
}


size32 IFactEndConjunction(IFactDraft * draft)
{
	ASSERT(draft->hasBegunConjunction);
	
	IFactConjunction * conjunction = lastConjunction(&(draft->header));
	ASSERT(conjunction->nRows > 0);
	
	draft->hasBegunConjunction = false;
	return conjunction->nRows;
}


size32 IFactDraftCurrentNClauses(IFactDraft * draft)
{
	ASSERT(draft->hasBegunConjunction);
	IFactConjunction * conjunction = lastConjunction(&(draft->header));
	return conjunction->nRows;
}


static void sortIFactDraft(IFactDraft * draft)
{
	IFactHeader * ifact = &(draft->header);
	// first sort the tuples for each conjunction
	Atom forms[ifact->nConjunctions];
	size32 tupleBlockSizes[ifact->nConjunctions];
	TypedAtom * tuples = draft->tupleStorage;
	for(index8 i = 0; i < ifact->nConjunctions; i++) {
		IFactConjunction * conjunction = &(ifact->conjunctions[i]);
		size32 nAtoms = conjunction->nRows * conjunction->nColumns;
		tupleBlockSizes[i] = nAtoms * sizeof(TypedAtom);
		forms[i] = conjunction->form;

		SortTuples(tuples, conjunction->nRows, conjunction->nColumns);
		tuples += nAtoms;
	}

	// then sort the conjunctions by form
	// NOTE: this ordering depends on the form atom (ifact) and so is system-dependent
	index8 conjunctionOrder[ifact->nConjunctions];
	FindArrayOrdering((byte *) &forms, ifact->nConjunctions, sizeof(Atom), conjunctionOrder, 0);
	ReorderArray(ifact->conjunctions, conjunctionOrder, ifact->nConjunctions, sizeof(IFactConjunction));
	// reorder the tuple blocks accordingly
	ReorderRaggedArray(draft->tupleStorage, conjunctionOrder, tupleBlockSizes, ifact->nConjunctions);
}


/**
 * Create the defining facts represented by a draft ifact.
 * The assertFact() function is typically AssertFact()
 * but an alternative version is used during bootstrap.
 */
static void createFacts(IFactDraft * draft, void (* assertFact)(Atom predicateForm, TypedAtom * actors))
{
	TypedAtom ifactAtom = CreateTypedAtom(AT_ID, draft->header.hash);
	// NOTE: this should be handled by RelationBTree internally?
	ifactAtom.flags |= ATOM_PROTECTED;

	// walk through conjunctions and add to tuples corresponding relation table
	IFactConjunction const * conjunction = draft->header.conjunctions;
	TypedAtom * tuple = draft->tupleStorage;
	for(index8 i = 0; i < draft->header.nConjunctions; i++) {
		for(index32 j = 0; j < conjunction->nRows; j++) {
			// set the ifact atom
			tuple[conjunction->idColumn] = ifactAtom;
			// TODO: we must verify that asserting the fact succeeds
			assertFact(conjunction->form, tuple);
			tuple += conjunction->nColumns;
		}
		conjunction++;
	}
}


static data64 hashConjunction(IFactConjunction const * conjunction, TypedAtom const * tuples, data64 initialHash)
{
	data64 hash = initialHash;
	for(index32 i = 0, t = 0; i < conjunction->nRows; i++) {
		// hash one formula, corresponding to one row of the conjunction
		hash = FormulaHashFormActors(conjunction->form, &(tuples[t]), conjunction->nColumns, hash);
		t += conjunction->nColumns;
	}
	return hash;
}


/**
 * Compute the hash of the entire current IFact, including all tuples,
 * and store in the header->hash field.
 */
static data64 hashIFact(IFactDraft * draft)
{
	data64 hash = djb2InitialHash;
	TypedAtom * tuple = draft->tupleStorage;
	for(index32 i = 0; i < draft->header.nConjunctions; i++) {
		IFactConjunction * conjunction = &(draft->header.conjunctions[i]);
		hash = hashConjunction(conjunction, tuple, hash);
		tuple += conjunction->nRows * conjunction->nColumns;
	}
	return hash;
}


/**
 * Test whether the draft IFact is the same as the given IFact
 */
static bool sameIFact(IFactDraft * draft, IFactHeader * existingIFact)
{
	// Check for hash collision by comparing the actual defining facts.
	// We need to iterate over tuples from each relation involved,
	// and compare to the (sorted) tuples in our temporary storage

	if(draft->header.nConjunctions != existingIFact->nConjunctions)
		return false;

	TypedAtom const * currentTuple = draft->tupleStorage;	
	for(index32 i = 0; i < existingIFact->nConjunctions; i++) {
		IFactConjunction * conjunction = &(draft->header.conjunctions[i]);
		IFactConjunction * existingConjunction = &(existingIFact->conjunctions[i]);
		// check conjunctions headers are identical
		if(CompareMemory(conjunction, existingConjunction, sizeof(IFactConjunction)))
			return false;

		// fetch tuples and compare
		RelationBTreeIterator iterator;
		// create query tuple
		TypedAtom queryTuple[conjunction->nColumns];
		// the identified atom must be identical in current and existing
		TypedAtom idAtom = CreateTypedAtom(AT_ID, draft->header.hash);
		for(index8 j = 0; j < conjunction->nColumns; j++) {
			if(j == conjunction->idColumn)
				queryTuple[j] = idAtom;
			else
				queryTuple[j] = anonymousVariable;
		}
		RelationBTreeIterate(conjunction->btree, queryTuple, &iterator);
		TypedAtom resultTuple[conjunction->nColumns];
		while(RelationBTreeIteratorHasTuple(&iterator)) {
			RelationBTreeIteratorGetTuple(&iterator, resultTuple);
			// the id column must not affect the comparison
			resultTuple[conjunction->idColumn] = currentTuple[conjunction->idColumn];
			if(CompareTuples(resultTuple, currentTuple, conjunction->nColumns) != 0) {
				return false;
			}
			currentTuple += conjunction->nColumns;
			RelationBTreeIteratorNext(&iterator);
		}
		RelationBTreeIteratorEnd(&iterator);
		// currentTuple now points to the first tuple of the next conjuction
	}
	return true;
}


Atom IFactEndBootstrap(IFactDraft * draft, data64 hash, void (* assertFact)(Atom predicateForm, TypedAtom * actors))
{
	ASSERT(!draft->hasBegunConjunction);
	ASSERT(draft->header.conjunctions);

	sortIFactDraft(draft);
	if(hash == 0)
		draft->header.hash = hashIFact(draft);
	else
		draft->header.hash = hash;

	// check for existing IFact with the same hash value
	IFactHeader * existingIFact = peekIFactHeader(draft->header.hash);
	if(existingIFact) {
		if(sameIFact(draft, existingIFact)) {
			// reuse existing ifact
			acquireIFact(existingIFact);
			Free(draft->header.conjunctions);
		}
		else {
			// we have a hash collision
			// this is possible but should be highly unusual, crash it to investigate
			ASSERT(false);
		}
	}
	else {
		// new ifact
		createFacts(draft, assertFact);
		acquireIFact(&(draft->header));
		ASSERT(BTreeInsert(ifactStorage.btree, &(draft->header)) == BTREE_INSERTED)
	}
	FreePage(draft->tupleStorage);

	return (Atom) draft->header.hash;
}


Atom IFactEnd(IFactDraft * draft)
{
	return IFactEndBootstrap(draft, 0, AssertFact);
}


/**
 * Check whether a tuple can be added to a relation without violating an
 * IFact definition.
 * 
 * It is illegal to delete tuples from a relation that are part of an ifact.
 * It also makes sense that for each conjunction in the ifact is "complete"
 * in the sense that we cannot add tuples like
 * 
 *  list @cat element 4 position @s
 * 
 * once @cat has been defined. The only possible use for this might be partially
 * defined structures like a "prefix list" without the length constraint, where we
 * would define 
 * 
 * prefix-list *cat element 1 position @c &
 * prefix-list *cat element 2 position @a &
 * prefix-list *cat element 3 position @t &
 * 
 * so that @cat is defined as "any list that begins with c,a,t". But then it does
 * not make sense to add (list @cat element 4 position @s) because this contradicts
 * the logical definition (we have now extended the prefix). I think all cases are
 * analogous to such a prefex list. So it seems this should not be allowed.
 *
 * Hence, whenever the user attempts to either remove or add a tuple containing a
 * AT_ID from a table, we must check:
 * 
 *   IF the ifact corresponding to the AT_ID refers to the table as a conjuction
 *   AND the AT_ID is in the defining position
 *   THEN the tuple cannot be added/removed
 * 
 * Note that adding a tuple like (list @animals element 12 position @cat) is valid
 * since @cat is not in the definiting position.
 * 
 * For the (list element position) case, enforcing logical consistency would also 
 * forbid adding a fourth tuple, as it would violate (list @cat length 3),
 * but this is not yet implemented and likely cannot be invoked for core tables.
 * Conversely, the above checks render the explicit fact (list @cat length 3)
 * unnecessary as part of the ifact, as it can be inferred when the list elements
 * are fixed.
 */

bool IFactCheckTuple(BTree const * tree, TypedAtom const * tuple)
{
	size8 nColumns = RelationBTreeNColumns(tree);
	for(index8 i = 0; i < nColumns; i++) {
		if(tuple[i].type != AT_ID)
			continue;
		if(tuple[i].flags & ATOM_PROTECTED) // skip check for atoms being identified
			continue;
		IFactHeader * header = peekIFactHeader(tuple[i].atom);
		size8 nConjunctions = header->nConjunctions;
		IFactConjunction * conjunctions = header->conjunctions;
		ASSERT(header);
		for(index32 j = 0; j < nConjunctions; j++) {
			if((conjunctions[j].btree == tree) && (conjunctions[j].idColumn == i))
				return false;
		}
 	}
 	return true;
}


void IFactRelease(Atom ifact)
{
	IFactHeader * header = peekIFactHeader(ifact);
	ASSERT(header);
	ASSERT(header->refCount > 0);
	ASSERT(ifactStorage.totalReferenceCount > 0);

	header->refCount--;
	ifactStorage.totalReferenceCount--;

	if(header->refCount == 0) {
		// We make a copy of the IFactHeader since the below operations
		// may move items in the btree and thereby invalidate the pointer.
		IFactHeader headerCopy = *header;

		// Remove defining facts.
		// TODO: can we achieve this using lookup instead, so that we
		// don't actually need to store the conjunctions after IFactEnd()
		for(index8 i = 0; i < headerCopy.nConjunctions; i++) {
			IFactConjunction * conjunction = &(headerCopy.conjunctions[i]);
			TypedAtom queryTuple[conjunction->nColumns];
			createQueryTuple(queryTuple, ifact, conjunction->nColumns, conjunction->idColumn);

			// Delete protected tuples.
			// This may result in recursive calls to IFactRelease()
			// on atoms in the deleted tuple. The underlying relation table
			// delete() method must therefore be re-entrant.
			// Unfortunately btree_delete() as currently used is not.

			// NOTE: this does not remove entries from the lookup table
			RelationBTreeRemoveTuples(conjunction->btree, queryTuple, REMOVE_PROTECTED);
		}
		LookupRemoveAllRoles(ifact);

		// remove IFact
		Free(headerCopy.conjunctions);
		ASSERT(BTreeDelete(ifactStorage.btree, &headerCopy) == BTREE_DELETED);
	}
}


void PrintIFact(Atom atom)
{
	PrintF("ID %llx", atom);
}


void DumpIFacts(void)
{
	BTreeIterator iterator;
	BTreeIterate(&iterator, ifactStorage.btree, 0, 0);
	while(BTreeIteratorHasItem(&iterator)) {
		IFactHeader const * header = BTreeIteratorPeekItem(&iterator);
		PrintIFact((Atom) header->hash);
		PrintChar('\n');
		BTreeIteratorNext(&iterator);
	}
	BTreeIteratorEnd(&iterator);
}
