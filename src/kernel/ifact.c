
#include "btree/btree.h"
#include "lang/Variable.h"
#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/Parameter.h"
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



static IFactRelation * lastConjunction(IFactHeader * header)
{
	return &(header->relations[header->nRelations-1]);
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
	ASSERT(sizeof(IFactHeader) == 16 + sizeof(IFactRelation *));

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


void IFactBeginRelation(IFactDraft * draft, TypedRelation relation, index8 idColumn)
{
	ASSERT(!draft->hasBegunConjunction);

	// append new relation to array
	draft->header.nRelations++;
	draft->header.relations = Reallocate(
		draft->header.relations,
		draft->header.nRelations * sizeof(IFactRelation)
	);
	IFactRelation * ifactRelation = lastConjunction(&(draft->header));
	SetMemory(ifactRelation, sizeof(IFactRelation), 0);
	ifactRelation->relation = relation;
	ifactRelation->nColumns = TypeSignatureNAtomTypes(relation.typeSignature);
	ifactRelation->idColumn = idColumn;
	ASSERT(idColumn < ifactRelation->nColumns)

	draft->hasBegunConjunction = true;
}


/**
 * Add the given tuple to the temporary tuple storage for the current relation.
 */
void IFactAddTuple(IFactDraft * draft, Atom const tuple[])
{
	ASSERT(draft->hasBegunConjunction);
	IFactRelation * relation = lastConjunction(&(draft->header));

	relation->nRows++;
	size32 tupleNBytes = relation->nColumns * sizeof(Atom);
	relation->tuples = Reallocate(relation->tuples, relation->nRows * tupleNBytes);
	Atom * newTuple = relation->tuples + (relation->nRows - 1) * relation->nColumns;
	CopyMemory(tuple, newTuple, tupleNBytes);
	// ensure the identifying column is zero, to not affect hashCurrentIFact()
	newTuple[relation->idColumn] = (Atom) {0};
	// acquire atoms
	for(index8 j = 0; j < relation->nColumns; j++) {
		if(j != relation->idColumn)
			AcquireAtom(newTuple[j], relation->relation.typeSignature.atomTypes[j]);
	}
}


/**
 * Release the atoms of the tuples of each relation, except the identified atom,
 * and free the tuples and the relations array; see IFactAddTuple().
 */
static void freeConjunctions(IFactRelation relations[], size8 nConjunctions)
{
	for(index8 i = 0; i < nConjunctions; i++) {
		IFactRelation const * relation = &(relations[i]);
		Atom const * tuple = relation->tuples;
		for(index32 j = 0; j < relation->nRows; j++) {
			for(index8 k = 0; k < relation->nColumns; k++) {
				if(k != relation->idColumn)
					ReleaseTypedAtom(CreateTypedAtom(relation->relation.typeSignature.atomTypes[k], tuple[k]));
			}
			tuple += relation->nColumns;
		}
		Free(relation->tuples);
	}
	Free(relations);
}


size32 IFactEndRelation(IFactDraft * draft)
{
	ASSERT(draft->hasBegunConjunction);
	IFactRelation * relation = lastConjunction(&(draft->header));
	ASSERT(relation->nRows > 0);
	draft->hasBegunConjunction = false;
	return relation->nRows;
}


size32 IFactDraftCurrentNTuples(IFactDraft * draft)
{
	ASSERT(draft->hasBegunConjunction);
	IFactRelation * relation = lastConjunction(&(draft->header));
	return relation->nRows;
}


static int8 compareTuples(void const * tuple1, void const * tuple2, size32 tupleNBytes)
{
	return TupleCompare(tuple1, tuple2, tupleNBytes / sizeof(Atom));
}


/**
 * Order two IFactRelaton by TypedRelation, then by ID column.
 */
static int8 compareIFactRelations(void const * item1, void const * item2, size32 itemSize)
{
	IFactRelation const * ifactRelation1 = item1;
	IFactRelation const * ifactRelation2 = item2;
	int8 relationOrder = CompareRelations(ifactRelation1->relation, ifactRelation2->relation);
	if(relationOrder != 0)
		return relationOrder;
	return CompareUInt(ifactRelation1->idColumn, ifactRelation2->idColumn);
}


static void sortIFactDraft(IFactDraft * draft)
{
	IFactHeader * ifact = &(draft->header);
	// First sort the tuples of each relation
	for(index8 i = 0; i < ifact->nRelations; i++) {
		IFactRelation * relation = &(ifact->relations[i]);
		size32 tupleNBytes = relation->nColumns * sizeof(Atom);
		QuickSort(relation->tuples, relation->nRows, tupleNBytes, compareTuples);
		// CLAUDE: an ifact holds a set of tuples
		for(index32 j = 1; j < relation->nRows; j++) {
			ASSERT(TupleCompare(
				relation->tuples + (j - 1) * relation->nColumns,
				relation->tuples + j * relation->nColumns, relation->nColumns) < 0)
		}
	}
	// Then sort the relations by relation and ID column
	QuickSort(ifact->relations, ifact->nRelations, sizeof(IFactRelation), compareIFactRelations);
	// CLAUDE: findConjunction() assumes at most one relation per relation and ID column
	for(index8 i = 1; i < ifact->nRelations; i++) {
		ASSERT(compareIFactRelations(
			&(ifact->relations[i - 1]), &(ifact->relations[i]), sizeof(IFactRelation)) != 0)
	}
}


/**
 * Set the ID atom for all tuples in the draft ifact
 */
static void setIFactIDAtoms(IFactDraft * draft)
{
	Atom idAtom = (Atom) {.hash = draft->header.hash};
	// walk through all ifact relations
	IFactRelation const * relation = draft->header.relations;
	for(index8 i = 0; i < draft->header.nRelations; i++) {
		Atom * tuple = relation->tuples;
		for(index32 j = 0; j < relation->nRows; j++) {
			// set the identified atom
			tuple[relation->idColumn] = idAtom;
			tuple += relation->nColumns;
		}
		relation++;
	}
}


static data64 hashIFactRelation(IFactRelation const * ifactRelation, data64 initialHash)
{
	data64 hash = initialHash;
	hash = RelationHash(ifactRelation->relation, initialHash);
	// hash all tuples (already sorted)
	return DJB2DoubleHashAdd(
		ifactRelation->tuples, ifactRelation->nRows * ifactRelation->nColumns * sizeof(Atom), hash);
}


/**
 * Compute the hash of the entire current IFact, including all tuples,
 * and store in the header->hash field.
 */
static data64 hashIFact(IFactDraft * draft)
{
	data64 hash = djb2InitialHash;
	for(index32 i = 0; i < draft->header.nRelations; i++) {
		IFactRelation * relation = &(draft->header.relations[i]);
		hash = hashIFactRelation(relation, hash);
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

	if(draft->header.nRelations != existingIFact->nRelations)
		return false;

	for(index32 i = 0; i < existingIFact->nRelations; i++) {
		IFactRelation * relation = &(draft->header.relations[i]);
		size16 nRows = relation->nRows;
		size8 nColumns = relation->nColumns;
		IFactRelation * existingRelation = &(existingIFact->relations[i]);
		// check IFactRelation headers are identical
		if(compareIFactRelations(relation, existingRelation, sizeof(IFactRelation)) != 0)
			return false;
		if(nRows != existingRelation->nRows)
			return false;

		// compare tuples
		Atom * tuple = relation->tuples;
		Atom * existingTuple = existingRelation->tuples;
		for(index32 j = 0; j < nRows; j++) {
			for(index8 k = 0; k < nColumns; k++) {
				if(k != relation->idColumn && !SameAtoms(existingTuple[k], tuple[k]))
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
	ASSERT(draft->header.relations);

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
			ASSERT(existingHeader->nRelations == 0);
			existingHeader->nRelations = draft->header.nRelations;
			existingHeader->relations = draft->header.relations;
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
		freeConjunctions(draft->header.relations, draft->header.nRelations);

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
		freeConjunctions(headerCopy.relations, headerCopy.nRelations);
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


/*
 * Test whether the IFactRelations of an ifact correspond 1:1 to the given terms, with
 * the same (term form, ID column) pair. The term forms must be distinct; see IsIFactRule().
 */
static bool hasTermRelations(
	IFactHeader const * header, Atom const termForms[], index8 const idColumns[], size8 nTerms)
{
	if(header->nRelations != nTerms)
		return false;
	bool isFound[RELATION_MAX_ARITY] = {false};
	for(index8 i = 0; i < header->nRelations; i++) {
		IFactRelation const * ifactRelation = &(header->relations[i]);
		bool matchesTerm = false;
		for(index8 j = 0; !matchesTerm && (j < nTerms); j++) {
			matchesTerm = !isFound[j] && SameAtoms(ifactRelation->relation.form, termForms[j]) &&
				(ifactRelation->idColumn == idColumns[j]);
			isFound[j] = isFound[j] || matchesTerm;
		}
		if(!matchesTerm)
			return false;
	}
	return true;
}


void IFactReleaseCached(Atom form, index8 const idColumns[])
{
	Atom termForms[RELATION_MAX_ARITY];
	size8 nTerms = RelationFormGetTermForms(form, termForms);

	// Collect the cached ifacts first, since IFactRelease() modifies the B-tree
	ResizingArray cachedArray;
	CreateResizingArray(&cachedArray, sizeof(Atom), 10);
	BTreeIterator iterator;
	BTreeIterate(&iterator, ifactStorage->headers);
	while(BTreeIteratorNext(&iterator)) {
		IFactHeader const * header = BTreeIteratorPeekItem(&iterator);
		if((header->flags & IFACT_CACHED) && hasTermRelations(header, termForms, idColumns, nTerms)) {
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
	IFactRelation const * ifactRelation;
	index32 nextRow;
} IFactTupleIterator;


static IFactRelation const * findIFactRelation(
	IFactHeader const * header, TypedRelation relation, index8 idColumn)
{
	for(index8 i = 0; i < header->nRelations; i++) {
		IFactRelation const * ifactRelation = &(header->relations[i]);
		if(SameRelations(ifactRelation->relation, relation) && (ifactRelation->idColumn == idColumn))
			return ifactRelation;
	}
	return 0;
}


/**
 * Begin iterating over the tuples of the given ifact for the given relation
 * and idColumn. If the atom is not an ifact, or the ifact has no relation with
 * the given ID column, the iteration yields no tuples.
 * The iterator holds no allocated memory, so no function ends the iteration.
 */
static void iterateTuples(Atom ifact, TypedRelation relation, index8 idColumn, IFactTupleIterator * iterator)
{
	iterator->ifactRelation = 0;
	iterator->nextRow = 0;
	IFactHeader const * header = peekIFactHeader(ifact.hash);
	if(header)
		iterator->ifactRelation = findIFactRelation(header, relation, idColumn);
}


/**
 * CLAUDE: Return the next tuple, or 0 if the iteration is complete. The tuple holds the
 * identified atom in the ID column, and remains valid while the ifact exists.
 */
static Atom const * tupleIteratorNext(IFactTupleIterator * iterator)
{
	IFactRelation const * relation = iterator->ifactRelation;
	if(!relation || (iterator->nextRow >= relation->nRows))
		return 0;
	return relation->tuples + (iterator->nextRow++) * relation->nColumns;
}


size32 IFactRelationNTuples(TypedRelation relation)
{
	size32 nTuples = 0;
	BTreeIterator iterator;
	BTreeIterate(&iterator, ifactStorage->headers);
	while(BTreeIteratorNext(&iterator)) {
		IFactHeader const * header = BTreeIteratorPeekItem(&iterator);
		for(index8 i = 0; i < header->nRelations; i++) {
			if(SameRelations(header->relations[i].relation, relation))
				nTuples += header->relations[i].nRows;
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


/* CLAUDE: TODO: For a term with a repeated role, the reader should yield every actor order of
 * each tuple; see the TODO at ifactCall() in operator.c. */
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
	ServiceRegistryIterator iterator;
	ServiceRegistryIterate(relation, &iterator);
	while(!hasReader && ServiceRegistryIteratorNext(&iterator))
		hasReader = isReaderOperator(ServiceRegistryIteratorPeekRecord(&iterator)->op);
	ServiceRegistryIteratorEnd(&iterator);
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
