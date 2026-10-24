
#include "btree/btree.h"
#include "lang/Variable.h"
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/operator.h"
#include "kernel/Parameter.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/tuple.h"
#include "kernel/typedtuple.h"
#include "lang/PredicateForm.h"
#include "memory/allocator.h"
#include "memory/paging.h"
#include "memory/pool.h"
#include "memory/references.h"
#include "util/hashing.h"
#include "util/ResizingArray.h"
#include "util/sort.h"
#include "storage/StorageProvider.h"


 /**
 * An IFactConjunction stores a block of tuples in a RelationWriter,
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
typedef struct s_IFactStorage {
	// B-tree storing IFactHeader structs
	BTree * headers;
	uint32 totalReferenceCount;
	bool flagCreatedIFacts;
} IFactStorage;


static IFactStorage * ifactStorage = 0;


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


static void readerSetupState(void * state, Atom arguments[], void * readerData, void * storage);
static bool readerCall(void * state, Atom arguments[], void * readerData, void * storage);
static void readerFinalizeReader(void * readerData, void * storage);


static NamedFunction const ifactFunctions[] = {
	{"ifact.btreeCompareHeaders", (AnyFunction) btreeCompareHeaders},
	{"ifact.readerSetupState", (AnyFunction) readerSetupState},
	{"ifact.readerCall", (AnyFunction) readerCall},
	{"ifact.readerFinalizeReader", (AnyFunction) readerFinalizeReader},
};


void RegisterIFactFunctions(void)
{
	RegisterFunctions(ifactFunctions, sizeof(ifactFunctions) / sizeof(NamedFunction));
}


void InitializeIFacts(void)
{
	// Verify that struct packing left no padding.
	ASSERT(sizeof(IFactHeader) == 16 + sizeof(IFactConjunction *));

	ifactStorage = Allocate(sizeof(IFactStorage));
	SetPersistentState(STATE_KEY_IFACTS, ifactStorage);

	ifactStorage->headers = BTreeCreate(
	    sizeof(IFactHeader),
	    btreeCompareHeaders,
	    0
	);
	ifactStorage->totalReferenceCount = 0;
	ifactStorage->flagCreatedIFacts = false;
}


void RestoreIFacts(void)
{
	ifactStorage = GetPersistentState(STATE_KEY_IFACTS);
	ASSERT(ifactStorage)
}


bool IFactsInitialized(void)
{
	return (ifactStorage != 0) && (ifactStorage->headers != 0);
}


/**
 * Lookup an IFactHeader given a hash.
 * Returns a pointer to the stored BTree item, or 0 if not found
 */
static IFactHeader * peekIFactHeader(data64 hash)
{
	IFactHeader query;
	query.hash = hash;
	return BTreePeekItem(ifactStorage->headers, &query);
}


static void acquireIFact(IFactHeader * header)
{
	header->refCount++;
	ifactStorage->totalReferenceCount++;
}


void IFactAcquire(Atom ifact)
{
	IFactHeader * header = peekIFactHeader(ifact.hash);
	ASSERT(header);
	acquireIFact(header);
}


bool IFactExists(Atom ifact)
{
	IFactHeader const * header = peekIFactHeader(ifact.hash);
	return header && !(header->flags & IFACT_RESERVED);
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
	return ifactStorage->totalReferenceCount;
}


uint32 IFactTotalCount(void)
{
	return BTreeNItems(ifactStorage->headers);
}

void FreeIFacts(void)
{
	ASSERT(IFactTotalCount() == 0);
	BTreeFree(ifactStorage->headers);
	Free(ifactStorage);
	SetPersistentState(STATE_KEY_IFACTS, 0);
	ifactStorage = 0;
}


/**
 * Begin constructing a new ifact.
 */
void IFactBegin(IFactDraft * draft)
{
	SetMemory(draft, sizeof(IFactDraft), 0);
}


void IFactReserve(data64 hash)
{
	ASSERT(!peekIFactHeader(hash));
	IFactHeader header;
	SetMemory(&header, sizeof(IFactHeader), 0);
	header.hash = hash;
	header.flags = IFACT_RESERVED;
	ASSERT(BTreeInsert(ifactStorage->headers, &header) == BTREE_INSERTED)
}


void IFactBeginConjunction(IFactDraft * draft, TypedRelation relation, index8 idColumn)
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
	conjunction->relation = relation;
	conjunction->nColumns = TypeSignatureNAtomTypes(relation.typeSignature);
	conjunction->idColumn = idColumn;
	ASSERT(idColumn < conjunction->nColumns)

	draft->hasBegunConjunction = true;
}


/**
 * Add the given tuple to the temporary tuple storage for the current conjunction.
 */
void IFactAddTuple(IFactDraft * draft, Atom const tuple[])
{
	ASSERT(draft->hasBegunConjunction);
	IFactConjunction * conjunction = lastConjunction(&(draft->header));

	conjunction->nRows++;
	size32 tupleNBytes = conjunction->nColumns * sizeof(Atom);
	conjunction->tuples = Reallocate(conjunction->tuples, conjunction->nRows * tupleNBytes);
	Atom * newTuple = conjunction->tuples + (conjunction->nRows - 1) * conjunction->nColumns;
	CopyMemory(tuple, newTuple, tupleNBytes);
	// ensure the identifying column is zero, to not affect hashCurrentIFact()
	newTuple[conjunction->idColumn] = (Atom) {0};
	// acquire atoms
	for(index8 j = 0; j < conjunction->nColumns; j++) {
		if(j != conjunction->idColumn)
			AcquireAtom(newTuple[j], conjunction->relation.typeSignature.atomTypes[j]);
	}
}


/**
 * CLAUDE: Release the atoms of the tuples of each conjunction, except the identified atom,
 * and free the tuples and the conjunctions array; see IFactAddTuple().
 */
static void freeConjunctions(IFactConjunction * conjunctions, size8 nConjunctions)
{
	for(index8 i = 0; i < nConjunctions; i++) {
		IFactConjunction const * conjunction = &(conjunctions[i]);
		Atom const * tuple = conjunction->tuples;
		for(index32 j = 0; j < conjunction->nRows; j++) {
			for(index8 k = 0; k < conjunction->nColumns; k++) {
				if(k != conjunction->idColumn)
					ReleaseTypedAtom(CreateTypedAtom(conjunction->relation.typeSignature.atomTypes[k], tuple[k]));
			}
			tuple += conjunction->nColumns;
		}
		Free(conjunction->tuples);
	}
	Free(conjunctions);
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


static int8 compareTuples(void const * tuple1, void const * tuple2, size32 tupleNBytes)
{
	return TupleCompare(tuple1, tuple2, tupleNBytes / sizeof(Atom));
}


/**
 * CLAUDE: Order conjunctions by relation, then by ID column.
 */
static int8 compareConjunctions(void const * item1, void const * item2, size32 itemSize)
{
	IFactConjunction const * conjunction1 = item1;
	IFactConjunction const * conjunction2 = item2;
	int8 relationOrder = CompareRelations(conjunction1->relation, conjunction2->relation);
	if(relationOrder != 0)
		return relationOrder;
	return CompareUInt(conjunction1->idColumn, conjunction2->idColumn);
}


static void sortIFactDraft(IFactDraft * draft)
{
	IFactHeader * ifact = &(draft->header);
	// First sort the tuples of each conjunction
	for(index8 i = 0; i < ifact->nConjunctions; i++) {
		IFactConjunction * conjunction = &(ifact->conjunctions[i]);
		size32 tupleNBytes = conjunction->nColumns * sizeof(Atom);
		QuickSort(conjunction->tuples, conjunction->nRows, tupleNBytes, compareTuples);
		// CLAUDE: an ifact holds a set of tuples
		for(index32 j = 1; j < conjunction->nRows; j++) {
			ASSERT(TupleCompare(
				conjunction->tuples + (j - 1) * conjunction->nColumns,
				conjunction->tuples + j * conjunction->nColumns, conjunction->nColumns) < 0)
		}
	}
	// Then sort the conjunctions by relation and ID column
	QuickSort(ifact->conjunctions, ifact->nConjunctions, sizeof(IFactConjunction), compareConjunctions);
	// CLAUDE: findConjunction() assumes at most one conjunction per relation and ID column
	for(index8 i = 1; i < ifact->nConjunctions; i++) {
		ASSERT(compareConjunctions(
			&(ifact->conjunctions[i - 1]), &(ifact->conjunctions[i]), sizeof(IFactConjunction)) != 0)
	}
}


/**
 * Set the ID atom for all tuples in the draft ifact
 */
static void setIFactIDAtoms(IFactDraft * draft)
{
	Atom idAtom = (Atom) {.hash = draft->header.hash};
	// walk through all conjunctions
	IFactConjunction const * conjunction = draft->header.conjunctions;
	for(index8 i = 0; i < draft->header.nConjunctions; i++) {
		Atom * tuple = conjunction->tuples;
		for(index32 j = 0; j < conjunction->nRows; j++) {
			// set the identified atom
			tuple[conjunction->idColumn] = idAtom;
			tuple += conjunction->nColumns;
		}
		conjunction++;
	}
}


static data64 hashConjunction(IFactConjunction const * conjunction, data64 initialHash)
{
	data64 hash = initialHash;
	hash = RelationHash(conjunction->relation, initialHash);
	// hash all tuples (already sorted)
	return DJB2DoubleHashAdd(
		conjunction->tuples, conjunction->nRows * conjunction->nColumns * sizeof(Atom), hash);
}


/**
 * Compute the hash of the entire current IFact, including all tuples,
 * and store in the header->hash field.
 */
static data64 hashIFact(IFactDraft * draft)
{
	data64 hash = djb2InitialHash;
	for(index32 i = 0; i < draft->header.nConjunctions; i++) {
		IFactConjunction * conjunction = &(draft->header.conjunctions[i]);
		hash = hashConjunction(conjunction, hash);
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

	for(index32 i = 0; i < existingIFact->nConjunctions; i++) {
		IFactConjunction * conjunction = &(draft->header.conjunctions[i]);
		size16 nRows = conjunction->nRows;
		size8 nColumns = conjunction->nColumns;
		IFactConjunction * existingConjunction = &(existingIFact->conjunctions[i]);
		// check conjunctions headers are identical
		if(compareConjunctions(conjunction, existingConjunction, sizeof(IFactConjunction)) != 0)
			return false;
		if(nRows != existingConjunction->nRows)
			return false;

		// compare tuples
		Atom * tuple = conjunction->tuples;
		Atom * existingTuple = existingConjunction->tuples;
		for(index32 j = 0; j < nRows; j++) {
			for(index8 k = 0; k < nColumns; k++) {
				if(k != conjunction->idColumn && !SameAtoms(existingTuple[k], tuple[k]))
					return false;
			}
			tuple += nColumns;
			existingTuple += nColumns;
		}
	}
	return true;
}


Atom IFactEndBootstrap(IFactDraft * draft, data64 hash)
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
			setIFactIDAtoms(draft);
			acquireIFact(existingHeader);
			if(ifactStorage->flagCreatedIFacts)
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
		setIFactIDAtoms(draft);
		acquireIFact(&(draft->header));
		if(ifactStorage->flagCreatedIFacts)
			draft->header.flags |= IFACT_NEW;
		ASSERT(BTreeInsert(ifactStorage->headers, &(draft->header)) == BTREE_INSERTED)
		keepConjunctions = true;
	}

	if(!keepConjunctions)
		freeConjunctions(draft->header.conjunctions, draft->header.nConjunctions);

	return (Atom) {.hash = draft->header.hash};
}


Atom IFactEnd(IFactDraft * draft)
{
	return IFactEndBootstrap(draft, 0);
}


void IFactRelease(Atom idAtom)
{
	IFactHeader * header = peekIFactHeader(idAtom.hash);
	ASSERT(header);
	ASSERT(header->refCount > 0);
	ASSERT(ifactStorage->totalReferenceCount > 0);

	header->refCount--;
	ifactStorage->totalReferenceCount--;

	if(header->refCount == 0) {
		// We make a copy of the IFactHeader since the below operations
		// may move items in the btree and thereby invalidate the pointer.
		IFactHeader headerCopy = *header;

		// remove IFact
		ASSERT(BTreeDelete(ifactStorage->headers, &headerCopy, 0) == BTREE_DELETED);
		// CLAUDE: Releasing the atoms of the defining facts may release other ifacts
		freeConjunctions(headerCopy.conjunctions, headerCopy.nConjunctions);
	}
}


void IFactMarkCached(Atom ifact)
{
	IFactHeader * header = peekIFactHeader(ifact.hash);
	ASSERT(header)
	if(header->flags & IFACT_CACHED)
		IFactRelease(ifact);
	else
		header->flags |= IFACT_CACHED;
}


void IFactReleaseCached(Atom form, index8 idColumn)
{
	// Collect the cached ifacts first, since IFactRelease() modifies the B-tree
	ResizingArray cachedArray;
	CreateResizingArray(&cachedArray, sizeof(Atom), 10);
	BTreeIterator iterator;
	BTreeIterate(&iterator, ifactStorage->headers);
	while(BTreeIteratorNext(&iterator)) {
		IFactHeader const * header = BTreeIteratorPeekItem(&iterator);
		if((header->flags & IFACT_CACHED) &&
			(header->nConjunctions == 1) &&
			SameAtoms(header->conjunctions[0].relation.form, form) &&
			(header->conjunctions[0].idColumn == idColumn)) {
			Atom ifact = {.hash = header->hash};
			ResizingArrayAppend(&cachedArray, &ifact);
		}
	}
	BTreeIteratorEnd(&iterator);

	for(index32 i = 0; i < cachedArray.nElements; i++) {
		Atom const * ifact = ResizingArrayGetElement(&cachedArray, i);
		IFactHeader * header = peekIFactHeader(ifact->hash);
		header->flags &= ~((data8) IFACT_CACHED);
		IFactRelease(*ifact);
	}
	FreeResizingArray(&cachedArray);
}


/**
 * CLAUDE: Iteration over the tuples of one ifact on one relation, with the identified
 * atom in the given ID column. The tuples are yielded in the order of TupleCompare().
 * The ifact must not be released during the iteration.
 */
typedef struct s_IFactTupleIterator {
	IFactConjunction const * conjunction;
	index32 nextRow;
} IFactTupleIterator;


static IFactConjunction const * findConjunction(
	IFactHeader const * header, TypedRelation relation, index8 idColumn)
{
	for(index8 i = 0; i < header->nConjunctions; i++) {
		IFactConjunction const * conjunction = &(header->conjunctions[i]);
		if(SameRelations(conjunction->relation, relation) && (conjunction->idColumn == idColumn))
			return conjunction;
	}
	return 0;
}


/**
 * CLAUDE: Begin iterating over the tuples of the given ifact on the given relation.
 * If the atom is not an ifact, or the ifact has no conjunction on the relation with
 * the given ID column, the iteration yields no tuples.
 * The iterator holds no allocated memory, so no function ends the iteration.
 */
static void iterateTuples(Atom ifact, TypedRelation relation, index8 idColumn, IFactTupleIterator * iterator)
{
	iterator->conjunction = 0;
	iterator->nextRow = 0;
	IFactHeader const * header = peekIFactHeader(ifact.hash);
	if(header)
		iterator->conjunction = findConjunction(header, relation, idColumn);
}


/**
 * CLAUDE: Return the next tuple, or 0 if the iteration is complete. The tuple holds the
 * identified atom in the ID column, and remains valid while the ifact exists.
 */
static Atom const * tupleIteratorNext(IFactTupleIterator * iterator)
{
	IFactConjunction const * conjunction = iterator->conjunction;
	if(!conjunction || (iterator->nextRow >= conjunction->nRows))
		return 0;
	return conjunction->tuples + (iterator->nextRow++) * conjunction->nColumns;
}


size32 IFactRelationNTuples(TypedRelation relation)
{
	size32 nTuples = 0;
	BTreeIterator iterator;
	BTreeIterate(&iterator, ifactStorage->headers);
	while(BTreeIteratorNext(&iterator)) {
		IFactHeader const * header = BTreeIteratorPeekItem(&iterator);
		for(index8 i = 0; i < header->nConjunctions; i++) {
			if(SameRelations(header->conjunctions[i].relation, relation))
				nTuples += header->conjunctions[i].nRows;
		}
	}
	BTreeIteratorEnd(&iterator);
	return nTuples;
}


/**
 * CLAUDE: The data of a reader created by setupReader().
 */
typedef struct s_IFactReaderData {
	TypedRelation relation;
	size8 nColumns;
	index8 idColumn;
	// The input columns other than the ID column
	bool isInput[RELATION_MAX_ARITY];
} IFactReaderData;


static void readerSetupState(void * state, Atom arguments[], void * readerData, void * storage)
{
	IFactReaderData const * data = readerData;
	iterateTuples(arguments[data->idColumn], data->relation, data->idColumn, state);
}


static bool readerCall(void * state, Atom arguments[], void * readerData, void * storage)
{
	IFactReaderData const * data = readerData;
	Atom const * tuple;
	while((tuple = tupleIteratorNext(state))) {
		bool matches = true;
		for(index8 i = 0; matches && (i < data->nColumns); i++) {
			if(data->isInput[i] && !SameAtoms(tuple[i], arguments[i]))
				matches = false;
		}
		if(matches) {
			TupleCopy(tuple, arguments, data->nColumns);
			return true;
		}
	}
	return false;
}


static void readerFinalizeReader(void * readerData, void * storage)
{
	Free(readerData);
}


/**
 * CLAUDE: Fill out a reader for the ifacts on the given relation, with the identified atom
 * in idColumn. The reader has the given IO signature, which must have idColumn as input.
 * The reader yields the tuples of the ifact given at idColumn that match the other inputs.
 * The reader yields its tuples in the identity index order; see CreateMachineOperator().
 */
static void setupReader(
	RelationReaderSpec * spec, TypedRelation relation, index8 idColumn, IOSignature ioSignature)
{
	size8 nColumns = TypeSignatureNAtomTypes(relation.typeSignature);
	ASSERT(ioSignature.parameterIO[idColumn] == PARAMETER_IN)
	IFactReaderData * data = Allocate(sizeof(IFactReaderData));
	data->relation = relation;
	data->nColumns = nColumns;
	data->idColumn = idColumn;
	for(index8 i = 0; i < nColumns; i++)
		data->isInput[i] = (i != idColumn) && (ioSignature.parameterIO[i] == PARAMETER_IN);

	SetMemory(spec, sizeof(RelationReaderSpec), 0);
	spec->ioSignature = ioSignature;
	spec->readerData = data;
	spec->stateSize = sizeof(IFactTupleIterator);
	spec->setupState = readerSetupState;
	spec->call = readerCall;
	spec->finalizeReader = readerFinalizeReader;
}


/**
 * CLAUDE: The service of a reader with idColumn as the only input; see IFactCreateReaderService().
 */
static Service createReaderService(TypedRelation relation, index8 idColumn)
{
	size8 nColumns = TypeSignatureNAtomTypes(relation.typeSignature);
	byte parameterIO[nColumns];
	for(index8 i = 0; i < nColumns; i++)
		parameterIO[i] = (i == idColumn) ? PARAMETER_IN : PARAMETER_OUT;
	return (Service) {.relation = relation, .ioSignature = CreateIOSignature(parameterIO, nColumns)};
}


static bool isReaderOperator(Operator const * op)
{
	return (op->type == OPERATOR_MACHINE) && (op->impl.machine.readerSpec.call == readerCall);
}


void IFactCreateReaderService(TypedRelation relation, index8 idColumn)
{
	size8 nColumns = TypeSignatureNAtomTypes(relation.typeSignature);
	Service service = createReaderService(relation, idColumn);
	// CLAUDE: The invalidation removes any compiled service of the relation
	bool isStale = InvalidateServicesForNewRelation(relation.form);
	ASSERT(!ServiceGetRecord(service))
	RelationReaderSpec readerSpec;
	setupReader(&readerSpec, relation, idColumn, service.ioSignature);
	CreateService(service, CreateMachineOperator(nColumns, 0, &readerSpec, 0));
	if(isStale)
		ServiceMarkStale(service);
}


bool IFactRelationHasReader(TypedRelation relation)
{
	bool hasReader = false;
	ServiceIterator iterator;
	ServiceRegistryIterate(relation, &iterator);
	while(!hasReader && ServiceIteratorNext(&iterator))
		hasReader = isReaderOperator(ServiceIteratorPeekRecord(&iterator)->op);
	ServiceIteratorEnd(&iterator);
	return hasReader;
}


bool IFactSetupRelation(TypedRelation relation, index8 idColumn, bool * createdReader)
{
	*createdReader = false;
	if(RelationExists(relation)) {
		ServiceRecord const * record = ServiceGetRecord(createReaderService(relation, idColumn));
		if(record && isReaderOperator(record->op))
			return true;
		if(RelationGetTupleStore(relation))
			return false;
	}
	IFactCreateReaderService(relation, idColumn);
	*createdReader = true;
	return true;
}


// CLAUDE: see IFactSetPrinter()
static IFactPrinter ifactPrinter = 0;


void IFactSetPrinter(IFactPrinter printer)
{
	ifactPrinter = printer;
}


void IFactPrint(Atom atom)
{
	IFactHeader * header = peekIFactHeader(atom.hash);
	ASSERT(header)
	if(ifactPrinter && ifactPrinter(atom))
		return;
	PrintF("ID(..%x)", atom.hash & 0xFFFF);
}


void IFactDump(void)
{
	BTreeIterator iterator;
	BTreeIterate(&iterator, ifactStorage->headers);
	while(BTreeIteratorNext(&iterator)) {
		IFactHeader const * header = BTreeIteratorPeekItem(&iterator);
		IFactPrint((Atom) {.hash = header->hash});
		PrintChar('\n');
	}
	BTreeIteratorEnd(&iterator);
}


void IFactsEnableFlagging(void)
{
	ifactStorage->flagCreatedIFacts = true;
}

void IFactDumpFlagged(void)
{
	BTreeIterator iterator;
	BTreeIterate(&iterator, ifactStorage->headers);
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
	ifactStorage->flagCreatedIFacts = false;
	// clear all flags
	byte mask = ~((byte) IFACT_NEW);
	BTreeIterator iterator;
	BTreeIterate(&iterator, ifactStorage->headers);
	while(BTreeIteratorNext(&iterator)) {
		IFactHeader * header = BTreeIteratorPeekItem(&iterator);
		header->flags &= mask;
	}
	BTreeIteratorEnd(&iterator);
}
