
#include "btree/btree.h"
#include "lang/Variable.h"
#include "kernel/dispatch.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/lookup.h"
#include "kernel/multiset.h"
#include "kernel/Parameter.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/typedtuple.h"
#include "lang/PredicateForm.h"
#include "lang/Form.h"
#include "lang/Formula.h"
#include "memory/paging.h"
#include "memory/allocator.h"
#include "util/hashing.h"
#include "util/ResizingArray.h"
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
 * We store all IFactHeaders in a BTree and perform lookup by their
 * hash value. IFactConjunctions are allocated with Allocate().
 */
static struct {
	BTree * btree;					// B-tree storing IFactHeader structs
	uint32 totalReferenceCount;
	bool flagCreatedIFacts;
} ifactStorage = {0};


static int8 btreeCompareHeaders(void const * item1, void const * item2, size32 itemSize)
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
	    btreeCompareHeaders,
	    0
	);

	ifactStorage.totalReferenceCount = 0;
	ifactStorage.flagCreatedIFacts = false;
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
	IFactHeader * header = peekIFactHeader(ifact.hash);
	ASSERT(header);
	acquireIFact(header);
}


/**
 * Create query tuple to retrieve all facts with the ifact atom in the idColumn (0-based)
 */ 
static void setupQueryTuple(Atom * tuple, size8 nColumns, Atom ifact, index8 idColumn)
{
	SetMemory(tuple, nColumns * sizeof(Atom), 0);
	tuple[idColumn] = ifact;
}

uint32 IFactReferenceCount(Atom ifact)
{
	IFactHeader * header = peekIFactHeader(ifact.hash);
	ASSERT(header);
	uint32 refCount = header->refCount;
	return(refCount);
}


uint32 IFactTotalReferenceCount(void)
{
	return ifactStorage.totalReferenceCount;
}


uint32 IFactTotalCount(void)
{
	return BTreeNItems(ifactStorage.btree);
}

void FreeIFacts(void)
{
	ASSERT(IFactTotalCount() == 0);
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


void IFactReserve(data64 hash)
{
	ASSERT(!peekIFactHeader(hash));
	IFactHeader header;
	SetMemory(&header, sizeof(IFactHeader), 0);
	header.hash = hash;
	header.flags = IFACT_RESERVED;
	ASSERT(BTreeInsert(ifactStorage.btree, &header) == BTREE_INSERTED)
}


void IFactBeginConjunction(IFactDraft * draft, RelationTable const * relation, index8 idColumn)
{
	ASSERT(!draft->hasBegunConjunction);

	// append new conjunction to array
	draft->header.nConjunctions++;
	draft->header.conjunctions = Reallocate(
		draft->header.conjunctions,
		draft->header.nConjunctions * sizeof(IFactConjunction)
	);
	IFactConjunction * conjunction = lastConjunction(&(draft->header));
	SetMemory(conjunction, sizeof(IFactConjunction), 0);
	conjunction->table = relation;
	conjunction->idColumn = idColumn;

	// Locate a service for querying tuple based on the id column.
	// This is required by sameIFacts() to retrieve existing ifact tuples.
	byte parameterIO[conjunction->table->nColumns];
	for(index8 i = 0; i < conjunction->table->nColumns; i++) {
		parameterIO[i] = (i == idColumn) ? PARAMETER_IN : PARAMETER_OUT;
	}
	conjunction->service = RegistryFindService(relation, parameterIO);
	ASSERT(conjunction->service)

	draft->hasBegunConjunction = true;
}

/**
 * Conjunctions are ordered first by form, then by column types.
 * 
 * TODO: we currently use CompareMemory on the conjunction struct, but this ends
 * up comparing relation table pointers which could be brittle. Review this.
 */

// static int8 compareConjunctions(void const * item1, void const * item2, size32 itemSize)
// {
// 	IFactConjunction const * conjunction1 = item1;
// 	IFactConjunction const * conjunction2 = item2;
// 	ASSERT(conjunction1->nColumns == conjunction2->nColumns)
// 	if(conjunction1->predicateForm.hash < conjunction2->predicateForm.hash)
// 		return -1;
// 	else if(conjunction1->predicateForm.hash > conjunction2->predicateForm.hash)
// 		return 1;
// 	else {
// 		return CompareMemory(conjunction1->columnTypes, conjunction2->columnTypes, conjunction1->nColumns);
// 	}
// }


/**
 * Add the given tuple to the temporary tuple storage for the current conjunction.
 */
void IFactAddTuple(IFactDraft * draft, Atom const * tuple)
{
	ASSERT(draft->hasBegunConjunction);
	IFactConjunction * conjunction = lastConjunction(&(draft->header));
	
	// check for page overrun
	size32 storageBytesUsed = ((addr64) draft->currentTuple) - ((addr64) draft->tupleStorage);
	size32 tupleNBytes = conjunction->table->nColumns * sizeof(Atom);
	ASSERT(storageBytesUsed + tupleNBytes <= MEMORY_PAGE_SIZE);

	CopyMemory(tuple, draft->currentTuple, tupleNBytes);
	// ensure the identifying column is zero, to not affect hashCurrentIFact()
	draft->currentTuple[conjunction->idColumn] = (Atom) {0};
	draft->currentTuple += conjunction->table->nColumns;
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


size32 IFactDraftCurrentNTuples(IFactDraft * draft)
{
	ASSERT(draft->hasBegunConjunction);
	IFactConjunction * conjunction = lastConjunction(&(draft->header));
	return conjunction->nRows;
}


static void sortIFactDraft(IFactDraft * draft)
{
	IFactHeader * ifact = &(draft->header);
	// First sort the block of tuples belonging to for each conjunction
	size32 tupleBlockSizes[ifact->nConjunctions];
	Atom * tuples = draft->tupleStorage;
	for(index8 i = 0; i < ifact->nConjunctions; i++) {
		IFactConjunction * conjunction = &(ifact->conjunctions[i]);
		tupleBlockSizes[i] = conjunction->nRows * conjunction->table->nColumns * sizeof(Atom);
		QuickSort(tuples, conjunction->nRows, conjunction->table->nColumns * sizeof(Atom), CompareMemory);
		tuples += conjunction->nRows * conjunction->table->nColumns;
	}

	// Then sort the conjunctions by form and parameters
	index8 conjunctionOrder[ifact->nConjunctions];
	FindArrayOrdering(
		ifact->conjunctions, ifact->nConjunctions, sizeof(IFactConjunction), conjunctionOrder, CompareMemory);
	ReorderArray(ifact->conjunctions, conjunctionOrder, ifact->nConjunctions, sizeof(IFactConjunction));
	// reorder the tuple blocks accordingly
	ReorderRaggedArray(draft->tupleStorage, conjunctionOrder, tupleBlockSizes, ifact->nConjunctions);
}


/**
 * Create the defining facts represented by a draft ifact.
 * The assertFact() function is typically AssertFact()
 * but an alternative version is used during bootstrap.
 */
static void createFacts(IFactDraft * draft, bool bootstrap)
	// void (* assertFact)(Atom predicateForm, TypedTuple const * actors, uint8 idPosition))
{
	Atom idAtom = (Atom) {.hash = draft->header.hash};

	// walk through conjunctions and add to tuples corresponding relation table
	IFactConjunction const * conjunction = draft->header.conjunctions;
	Atom * tuple = draft->tupleStorage;
	for(index8 i = 0; i < draft->header.nConjunctions; i++) {
		for(index32 j = 0; j < conjunction->nRows; j++) {
			// set the identified atom
			tuple[conjunction->idColumn] = idAtom;
			// store the tuple
			ASSERT(RelationTableAddTuple(conjunction->table, tuple, conjunction->idColumn + 1) == TUPLE_ADDED)
			// add lookup
			if(!bootstrap) {
				LookupAddPredicateRoles(conjunction->table, tuple);
			}
			tuple += conjunction->table->nColumns;
		}
		conjunction++;
	}
}


static data64 hashConjunction(IFactConjunction const * conjunction, Atom const * tuples, data64 initialHash)
{
	data64 hash = initialHash;
	// hash the form and types
	hash = DJB2DoubleHashAdd(&conjunction->table->form.hash, sizeof(data64), initialHash);
	hash = DJB2DoubleHashAdd(conjunction->table->atomTypes, conjunction->table->nColumns, hash);
	// hash all tuples (sorted)
	return DJB2DoubleHashAdd(tuples, conjunction->nRows * conjunction->table->nColumns * sizeof(Atom), hash);
}


/**
 * Compute the hash of the entire current IFact, including all tuples,
 * and store in the header->hash field.
 */
static data64 hashIFact(IFactDraft * draft)
{
	data64 hash = djb2InitialHash;
	Atom const * tuplePtr = draft->tupleStorage;
	for(index32 i = 0; i < draft->header.nConjunctions; i++) {
		IFactConjunction * conjunction = &(draft->header.conjunctions[i]);
		hash = hashConjunction(conjunction, tuplePtr, hash);
		tuplePtr += conjunction->nRows * conjunction->table->nColumns;
	}
	return hash;
}


/**
 * Test whether the draft IFact is identical to an existing IFact
 * with the same hash value, by comparing the actual defining facts.
 * This is needed to ensure we do not have a hash collision.
 */
static bool sameIFact(IFactDraft * draft, IFactHeader * existingIFact)
{
	// We need to iterate over tuples from each relation involved,
	// and compare to the sorted tuples in our temporary storage

	if(draft->header.nConjunctions != existingIFact->nConjunctions)
		return false;

	Atom const * draftTuples = draft->tupleStorage;	
	for(index32 i = 0; i < existingIFact->nConjunctions; i++) {
		IFactConjunction * conjunction = &(draft->header.conjunctions[i]);
		size8 nRows = conjunction->nRows;
		size8 nColumns = conjunction->table->nColumns;
		IFactConjunction * existingConjunction = &(existingIFact->conjunctions[i]);
		// check conjunctions headers are identical
		if(CompareMemory(conjunction, existingConjunction, sizeof(IFactConjunction)))
			return false;

		// Fetch identifying facts for the existing IFact.
		// NOTE: The service may return tuples a different order than the tupleStorage array.
		Atom arguments[nColumns];
		setupQueryTuple(arguments, nColumns, (Atom) {.hash = draft->header.hash}, conjunction->idColumn);
		ServiceContext * context = ServiceCreateContext(conjunction->service, arguments);
		while(ServiceCall(context)) {
			// The id column is still zero in the draft tuple and must not affect the comparison.
			arguments[conjunction->idColumn] = draftTuples[conjunction->idColumn];
			// Find the tuple in the draft tuple storage for this conjunction
			if(!BinarySearch(arguments, draftTuples, nRows, nColumns * sizeof(Atom), CompareMemory))
				return false;
		}
		ServiceFreeContext(context);
		draftTuples += conjunction->nRows * nColumns;
	}
	return true;
}


Atom IFactEndBootstrap(IFactDraft * draft, data64 hash) // , void (* assertFact)(Atom, TypedTuple const *, uint8))
{
	ASSERT(!draft->hasBegunConjunction);
	ASSERT(draft->header.conjunctions);

	sortIFactDraft(draft);
	if(hash == 0) {
		draft->header.hash = hashIFact(draft);
	}
	else
		draft->header.hash = hash;

	// check for existing IFact with the same hash value
	IFactHeader * existingIFact = peekIFactHeader(draft->header.hash);
	if(existingIFact && (existingIFact->flags & IFACT_RESERVED)) {
		// Finalize a header reserved by IFactReserve(): adopt the draft's
		// conjunctions, retaining references already acquired against the
		// reserved header (typically by the relation tables storing its
		// defining facts).
		ASSERT(existingIFact->nConjunctions == 0);
		existingIFact->nConjunctions = draft->header.nConjunctions;
		existingIFact->conjunctions = draft->header.conjunctions;
		existingIFact->flags &= ~((data8) IFACT_RESERVED);
		createFacts(draft, hash != 0);
		acquireIFact(existingIFact);
		if(ifactStorage.flagCreatedIFacts)
			existingIFact->flags |= IFACT_NEW;
	}
	else if(existingIFact) {
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
		createFacts(draft, hash != 0);
		acquireIFact(&(draft->header));
		if(ifactStorage.flagCreatedIFacts)
			draft->header.flags |= IFACT_NEW;
		ASSERT(BTreeInsert(ifactStorage.btree, &(draft->header)) == BTREE_INSERTED)
			
	}
	FreePage(draft->tupleStorage);

	return (Atom) {.hash = draft->header.hash};
}


Atom IFactEnd(IFactDraft * draft)
{
	return IFactEndBootstrap(draft, 0);
}


/**
 * NOTE: I am not convinced the below is needed at all. Commented away for now.
 * It seems that we are fine as long as tuples storing a protected (identified)
 * atom cannot be deleted, except when removing the identified atom. In the "cat"
 * example, it is true that adding (list @cat element 4 position @s) should
 * cause a logical contradiction with (list @cat length 3), but that must be
 * inferred by logical reasononing. Also, we certainly cannot assert new identifying
 * facts for an already existing AT_ID atom, but that cannot happen since IFactEnd()
 * prevents creating duplicate AT_ID atoms.
 * 
 * Check whether a tuple can be added to a relation without violating an
 * IFact definition.
 * 
 * It is illegal to delete tuples from a relation that are part of an ifact.
 * Also, an ifact must be "complete" in the sense that we cannot add tuples like
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

 /*
bool IFactCheckTuple(BTree const * tree, TypedTuple const * tuple)
{
	ASSERT(tuple->nAtoms == RelationBTreeNColumns(tree))
	// ASSERT(tuple->protectedAtom)	// otherwise we have nothing to check
	index8 protectedIndex = tuple->protectedAtom - 1;
	for(index8 i = 0; i < tuple->nAtoms; i++) {
		if(i == protectedIndex) {
			// atom is defined by an IFact currently being created, skip check
			continue;
		}
		TypedAtom typedAtom = TypedTupleGetElement(tuple, i);
		if(typedAtom.type != AT_ID)
			continue;
		// check this atom
		IFactHeader * header = peekIFactHeader(typedAtom.atom.hash);
		ASSERT(header);
		size8 nConjunctions = header->nConjunctions;
		IFactConjunction * conjunctions = header->conjunctions;
		for(index32 j = 0; j < nConjunctions; j++) {
			if((conjunctions[j].btree == tree) && (conjunctions[j].idColumn == i))
				return false;
		}
	}
 	return true;
}
*/

void removeIFactTuples(IFactConjunction * conjunction, Atom idAtom)
{
	RelationTable const * table = conjunction->table;
	size8 nColumns = table->nColumns;

	// Retrieve all tuples having idAtom in the idColumn.
	// NOTE: although it may be possible to have tuples where idAtom is
	// present in idColumn _without_ being an identifying fact,
	// such tuples cannot occur here because they would have to reference idAtom,
	// and we already know that its reference count is zero.
	ResizingArray tuplesArray;
	CreateResizingArray(&tuplesArray, nColumns * sizeof(Atom), 10);
	Atom arguments[nColumns];
	setupQueryTuple(arguments, nColumns, idAtom, conjunction->idColumn);
	ServiceContext * context = ServiceCreateContext(conjunction->service, arguments);
	while(ServiceCall(context))
		ResizingArrayAppend(&tuplesArray, arguments);
	ServiceFreeContext(context);

	// Release all atoms referenced by tuples.
	// NOTE: in general, this cannot be done while iterating over service,
	// since iteration typically write-locks the storage.
	// Note that IFactRelease() may call this function recursively.

	// NOTE: we now do this in RelationTableRemoveTuple()
	// for(index32 i = 0; i < tuplesArray.nElements; i++) {
	// 	Atom * tuple = ResizingArrayGetElement(&tuplesArray, i);
	// 	for(index32 j = 0; j < nColumns; j++) {
	// 		if(j != conjunction->idColumn)
	// 			ReleaseTypedAtom(CreateTypedAtom(table->atomTypes[j], tuple[j]));
	// 	}
	// }
	// Delete tuples
	for(index32 i = 0; i < tuplesArray.nElements; i++) {
		Atom * tuple = ResizingArrayGetElement(&tuplesArray, i);
		ASSERT(RelationTableRemoveTuple(table, tuple, conjunction->idColumn + 1) == BTREE_DELETED);
	}
	FreeResizingArray(&tuplesArray);
}


void IFactRelease(Atom idAtom)
{
	IFactHeader * header = peekIFactHeader(idAtom.hash);
	ASSERT(header);
	ASSERT(header->refCount > 0);
	ASSERT(ifactStorage.totalReferenceCount > 0);

	header->refCount--;
	ifactStorage.totalReferenceCount--;

	if(header->refCount == 0) {
		// We make a copy of the IFactHeader since the below operations
		// may move items in the btree and thereby invalidate the pointer.
		IFactHeader headerCopy = *header;

		// Retract defining facts.
		// NOTE: can we locate the facts using lookup instead, so that we
		// don't actually need to store the conjunctions after IFactEnd() ?
		// We only need to know the predicate form (to identify the relation/service)
		// and the role in which the AT_ID atom participates.
		for(index8 i = 0; i < headerCopy.nConjunctions; i++) {
			IFactConjunction * conjunction = &(headerCopy.conjunctions[i]);
			removeIFactTuples(conjunction, idAtom);
		}
		LookupRemoveAllRoles(idAtom);

		// remove IFact
		Free(headerCopy.conjunctions);
		ASSERT(BTreeDelete(ifactStorage.btree, &headerCopy, 0) == BTREE_DELETED);
	}
}


void IFactPrint(Atom atom)
{
	IFactHeader * header = peekIFactHeader(atom.hash);
	ASSERT(header)
	PrintF("ID %llx (%llu) refCount = %u", atom.hash, atom.hash, header->refCount);
}


void IFactDump(void)
{
	BTreeIterator iterator;
	BTreeIterate(&iterator, ifactStorage.btree);
	while(BTreeIteratorNext(&iterator)) {
		IFactHeader const * header = BTreeIteratorPeekItem(&iterator);
		IFactPrint((Atom) {.hash = header->hash});
		PrintChar('\n');
	}
	BTreeIteratorEnd(&iterator);
}


void IFactsEnableFlagging(void)
{
	ifactStorage.flagCreatedIFacts = true;
}

void IFactDumpFlagged(void)
{
	BTreeIterator iterator;
	BTreeIterate(&iterator, ifactStorage.btree);
	while(BTreeIteratorNext(&iterator)) {
		IFactHeader const * header = BTreeIteratorPeekItem(&iterator);
		if(header->flags & IFACT_NEW) {
			PrintTypedAtom(CreateTypedAtom(AT_ID, (Atom) {.hash = header->hash}));
			PrintChar('\n');
		}
	}
	BTreeIteratorEnd(&iterator);
}

void IFactDisableFlagging(void)
{
	ifactStorage.flagCreatedIFacts = false;
	// clear all flags
	byte mask = ~((byte) IFACT_NEW);
	BTreeIterator iterator;
	BTreeIterate(&iterator, ifactStorage.btree);
	while(BTreeIteratorNext(&iterator)) {
		IFactHeader * header = BTreeIteratorPeekItem(&iterator);
		header->flags &= mask;
	}
	BTreeIteratorEnd(&iterator);
}
