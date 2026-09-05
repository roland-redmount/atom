
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
	IFactHeader const * ifact1 = item1;
	IFactHeader const * ifact2 = item2;
	if(ifact1->hash < ifact2->hash)
		return -1;
	if(ifact1->hash > ifact2->hash)
		return 1;
	return 0;
}


void InitializeIFacts(void)
{
	// check packed data structures
	ASSERT(sizeof(IFactConjunction) == 12);
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
static void setupQueryTuple(Atom tuple[], size8 nColumns, Atom ifact, index8 idColumn)
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


/**
 * Register a service for the given relation with idColumn as the sole input parameter.
 * Creates a FILTER operator based on an existing service for the relation.
 * Returns 0 if the relation has no suitable service to filter.
 */
static Operator * createIdColumnService(Relation relation, index8 idColumn, IOSignature ioSignature)
{
	// Find a service yielding an output wherever this one does, and at the identified
	// column, which is the one the filter tests.
	Operator * childOperator = 0;
	ServiceIterator iterator;
	ServiceRegistryIterate(relation, &iterator);
	while(!childOperator && ServiceIteratorNext(&iterator)) {
		Service const * service = ServiceIteratorPeekService(&iterator);
		size8 nColumns = service->op->nArguments;
		bool matches = (service->ioSignature.parameterIO[idColumn] == PARAMETER_OUT);
		for(index8 i = 0; matches && (i < nColumns); i++)
			matches = DispatchParameterIOMatch(
				ioSignature.parameterIO[i], service->ioSignature.parameterIO[i],
				DISPATCH_MATCH_RELAXED);
		if(matches) {
			childOperator = service->op;
		}
	}
	ServiceIteratorEnd(&iterator);
	if(!childOperator)
		return 0;
	// Create the FILTER operator
	Operator * op = CreateFilterOperator(childOperator, &idColumn, 1);
	// Register the new service
	CreateService(relation, ioSignature, op, SERVICE_COMPILED);
	return op;
}


/**
 * The operator of the service enumerating the tuples of a conjunction by its identified
 * atom, which sameIFacts() and removeIFactTuples() read the stored tuples with. Every
 * relation table storing ifact tuples must have this service; see
 * RelationTableProvider.registerServices().
 *
 * Looked up on demand rather than cached in the IFactConjunction, since an ifact outlives
 * any particular reading of it and a stored operator would dangle once the service were
 * removed. A counted reference would not do instead: the operator reads a table holding a
 * reference to the term form of its relation, which is an identifying fact itself, so the
 * reference would close a cycle through the core relations.
 */
static Operator const * conjunctionOperator(IFactConjunction const * conjunction)
{
	size8 nColumns = conjunction->table->nColumns;
	byte parameterIO[nColumns];
	for(index8 i = 0; i < nColumns; i++)
		parameterIO[i] = (i == conjunction->idColumn) ? PARAMETER_IN : PARAMETER_OUT;
	IOSignature ioSignature = CreateIOSignature(parameterIO, nColumns);
	Operator const * op = FindService(conjunction->table->relation, ioSignature);
	// If the relation lacks the necessary service yet, build it
	if(!op)
		op = createIdColumnService(conjunction->table->relation, conjunction->idColumn, ioSignature);
	ASSERT(op)
	return op;
}


void IFactBeginConjunction(IFactDraft * draft, RelationTable * table, index8 idColumn)
{
	ASSERT(!draft->hasBegunConjunction);

	// append new conjunction to array
	draft->header.nConjunctions++;
	draft->header.conjunctions = Reallocate(
		draft->header.conjunctions,
		draft->header.nConjunctions * sizeof(IFactConjunction)
	);
	IFactConjunction * conjunction = lastConjunction(&(draft->header));
	conjunction->table = table;
	AcquireRelationTable(table);		// ensure the table is valid until IFactEnd()
	conjunction->idColumn = idColumn;
	// Fail here rather than at the first read if the table cannot be queried on its
	// identified column; see conjunctionOperator()
	ASSERT(conjunctionOperator(conjunction))

	draft->hasBegunConjunction = true;
}

/**
 * Add the given tuple to the temporary tuple storage for the current conjunction.
 */
void IFactAddTuple(IFactDraft * draft, Atom const tuple[])
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
				LookupAddPredicateRoles(conjunction->table->relation, tuple);
			}
			tuple += conjunction->table->nColumns;
		}
		conjunction++;
	}
}


static data64 hashConjunction(IFactConjunction const * conjunction, Atom const * tuples, data64 initialHash)
{
	data64 hash = initialHash;
	hash = RelationHash(conjunction->table->relation, initialHash);
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
		OperatorContext * context = OperatorCreateContext(conjunctionOperator(conjunction), arguments);
		while(OperatorCall(context)) {
			// The id column is still zero in the draft tuple and must not affect the comparison.
			arguments[conjunction->idColumn] = draftTuples[conjunction->idColumn];
			// Find the tuple in the draft tuple storage for this conjunction
			if(!BinarySearch(arguments, draftTuples, nRows, nColumns * sizeof(Atom), CompareMemory))
				return false;
		}
		OperatorFreeContext(context);
		draftTuples += conjunction->nRows * nColumns;
	}
	return true;
}


Atom IFactEndBootstrap(IFactDraft * draft, data64 hash) // , void (* assertFact)(Atom, TypedTuple const *, uint8))
{
	ASSERT(!draft->hasBegunConjunction);
	ASSERT(draft->header.conjunctions);

	sortIFactDraft(draft);
	// Use the provided hash if present
	if(hash == 0) {
		draft->header.hash = hashIFact(draft);
	}
	else
		draft->header.hash = hash;

	// check for an existing IFactHeader with the same hash value
	IFactHeader * existingHeader = peekIFactHeader(draft->header.hash);
	bool keepConjunctions;
	if(existingHeader) {
		if(existingHeader->flags & IFACT_RESERVED) {
			// Finalize an IFactHeader previously registered by IFactReserve()
			// Copy the draft ifact contents to the existing header
			ASSERT(existingHeader->nConjunctions == 0);
			existingHeader->nConjunctions = draft->header.nConjunctions;
			existingHeader->conjunctions = draft->header.conjunctions;
			existingHeader->flags &= ~((data8) IFACT_RESERVED);	// no longer reserved
			createFacts(draft, hash != 0);
			acquireIFact(existingHeader);
			if(ifactStorage.flagCreatedIFacts)
				existingHeader->flags |= IFACT_NEW;
			keepConjunctions = true;
		}
		else {
			// A previously existed ifact with the same hash
			if(sameIFact(draft, existingHeader)) {
				// reuse existing ifact
				acquireIFact(existingHeader);
				keepConjunctions = false;
			}
			else {
				// We have a hash collision. Possible, but should be highly unusual.
				Panic("Hash collision for ifact hash = %llu", draft->header.hash);
			}
		}
	}
	else {
		// new ifact
		createFacts(draft, hash != 0);
		acquireIFact(&(draft->header));
		if(ifactStorage.flagCreatedIFacts)
			draft->header.flags |= IFACT_NEW;
		ASSERT(BTreeInsert(ifactStorage.btree, &(draft->header)) == BTREE_INSERTED)
		keepConjunctions = true;
	}
	// Release acquired table references
	for(index8 i = 0; i < draft->header.nConjunctions; i++)
		ReleaseRelationTable(draft->header.conjunctions[i].table);
	if(!keepConjunctions)
		Free(draft->header.conjunctions);
	FreePage(draft->tupleStorage);

	return (Atom) {.hash = draft->header.hash};
}


Atom IFactEnd(IFactDraft * draft)
{
	return IFactEndBootstrap(draft, 0);
}


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
	OperatorContext * context = OperatorCreateContext(conjunctionOperator(conjunction), arguments);
	while(OperatorCall(context))
		ResizingArrayAppend(&tuplesArray, arguments);
	OperatorFreeContext(context);

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
	PrintF("ID(..%x)", atom.hash & 0xFFFF);
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
