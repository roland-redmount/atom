
#include "btree/btree.h"
#include "memory/paging.h"
#include "memory/references.h"
#include "util/hashing.h"
#include "util/sort.h"


//------------------------------------ function registry ------------------------------------

#define MAX_REGISTERED_FUNCTIONS	256

typedef struct s_RegisteredFunction {
	data64 nameHash;
	AnyFunction function;
	char const * name;
} RegisteredFunction;

/*
 * The registry holds each registered function twice: functionsByHash is ordered by
 * name hash, and functionsByAddress is ordered by function address.
 */
static struct {
	RegisteredFunction functionsByHash[MAX_REGISTERED_FUNCTIONS];
	RegisteredFunction functionsByAddress[MAX_REGISTERED_FUNCTIONS];
	size32 nFunctions;
} registry;


static addr64 functionAddress(AnyFunction function)
{
	return (addr64) (uintptr_t) function;
}


static int8 compareByHash(void const * item, void const * key, size32 itemSize)
{
	data64 itemHash = ((RegisteredFunction const *) item)->nameHash;
	data64 keyHash = ((RegisteredFunction const *) key)->nameHash;
	return (itemHash < keyHash) ? -1 : (itemHash > keyHash) ? 1 : 0;
}


static int8 compareByAddress(void const * item, void const * key, size32 itemSize)
{
	addr64 itemAddress = functionAddress(((RegisteredFunction const *) item)->function);
	addr64 keyAddress = functionAddress(((RegisteredFunction const *) key)->function);
	return (itemAddress < keyAddress) ? -1 : (itemAddress > keyAddress) ? 1 : 0;
}


/**
 * Return the index of the first registered function whose name hash is >= nameHash.
 */
static index32 findHashIndex(data64 nameHash)
{
	RegisteredFunction key = {.nameHash = nameHash};
	return BinarySearchLowerBound(
		&key, registry.functionsByHash, registry.nFunctions, sizeof(RegisteredFunction), compareByHash);
}


/**
 * Return the index of the first registered function whose address is >= the function address.
 */
static index32 findAddressIndex(AnyFunction function)
{
	RegisteredFunction key = {.function = function};
	return BinarySearchLowerBound(
		&key, registry.functionsByAddress, registry.nFunctions, sizeof(RegisteredFunction),
		compareByAddress);
}


/**
 * Insert element into array, shifting existing elements right.
 */
static void insertRegisteredFunction(
	RegisteredFunction functions[], index32 index, RegisteredFunction const * entry)
{
	MoveMemory(
		&(functions[index]), &(functions[index + 1]),
		(registry.nFunctions - index) * sizeof(RegisteredFunction));
	functions[index] = *entry;
}


void RegisterFunctions(NamedFunction const functions[], size32 nFunctions)
{
	for(index32 i = 0; i < nFunctions; i++) {
		RegisteredFunction entry = {
			.nameHash = DJB2DoubleHashAdd(
				functions[i].name, CStringLength(functions[i].name), djb2InitialHash),
			.function = functions[i].function,
			.name = functions[i].name
		};
		index32 hashIndex = findHashIndex(entry.nameHash);
		if((hashIndex < registry.nFunctions) &&
			(registry.functionsByHash[hashIndex].nameHash == entry.nameHash)) {
			// registered before: the name must belong to the same function
			ASSERT(registry.functionsByHash[hashIndex].function == entry.function)
			continue;
		}
		// one function must not be registered under two names
		index32 addressIndex = findAddressIndex(entry.function);
		ASSERT((addressIndex == registry.nFunctions) ||
			(registry.functionsByAddress[addressIndex].function != entry.function))

		if(registry.nFunctions == MAX_REGISTERED_FUNCTIONS)
			Panic("cannot register more than %u functions\n", MAX_REGISTERED_FUNCTIONS);
		insertRegisteredFunction(registry.functionsByHash, hashIndex, &entry);
		insertRegisteredFunction(registry.functionsByAddress, addressIndex, &entry);
		registry.nFunctions++;
	}
}


/**
 * Return the function registered under the name hash, or 0 if there is none.
 */
static AnyFunction findFunctionByHash(data64 nameHash)
{
	index32 index = findHashIndex(nameHash);
	if((index < registry.nFunctions) && (registry.functionsByHash[index].nameHash == nameHash))
		return registry.functionsByHash[index].function;
	return 0;
}


/**
 * Return the registered function, or 0 if the function is not registered.
 */
static RegisteredFunction const * findRegisteredFunction(AnyFunction function)
{
	index32 index = findAddressIndex(function);
	if((index < registry.nFunctions) && (registry.functionsByAddress[index].function == function))
		return &(registry.functionsByAddress[index]);
	return 0;
}


//------------------------------------ reference table ------------------------------------

/*
 * A Reference records an address in page-allocated memory holding a function pointer,
 * and the name hash of the function.
 */
typedef struct s_Reference {
	addr64 slot;
	data64 nameHash;
} Reference;


static int8 compareReferences(Reference const * reference, Reference const * referenceOrKey)
{
	if(reference->slot < referenceOrKey->slot)
		return -1;
	if(reference->slot > referenceOrKey->slot)
		return 1;
	return 0;
}


/**
 * Return the first Reference with a slot in the memory block [start, start + nBytes),
 * or 0 if there is none.
 */
static Reference const * findReferenceInRange(BTree * table, void const * start, size32 nBytes)
{
	Reference key = {.slot = (addr64) start};
	Reference const * reference = BTreePeekLowerBound(table, &key);
	if(reference && (reference->slot < key.slot + nBytes))
		return reference;
	return 0;
}


static int8 btreeCompareReferences(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareReferences((Reference const *) item, (Reference const *) itemOrKey);
}


static NamedFunction const referenceFunctions[] = {
	{"references.compareReferences", (AnyFunction) btreeCompareReferences},
};


/*
 * The reference table is a persisten B-tree of Reference items, ordered by slot.
 * It is always looked up from persistent state, which is 0 when no table exists.
 */
static BTree * getReferenceTable(void)
{
	return GetPersistentState(STATE_KEY_REFERENCES);
}


/*
 * Set while InitializeReferences() creates the reference table. The table's own
 * comparator is the only slot in the paging area that SetReference() does not record,
 * since the table does not exist yet.
 */
static bool isCreatingTable = false;


void InitializeReferences(void)
{
	ASSERT(getReferenceTable() == 0)
	RegisterFunctions(referenceFunctions, sizeof(referenceFunctions) / sizeof(NamedFunction));
	isCreatingTable = true;
	BTree * table = BTreeCreate(sizeof(Reference), btreeCompareReferences, 0);
	isCreatingTable = false;
	SetPersistentState(STATE_KEY_REFERENCES, table);
}


void FreeReferences(void)
{
	BTree * table = getReferenceTable();
	ASSERT(BTreeNItems(table) == 0)
	SetPersistentState(STATE_KEY_REFERENCES, 0);
	BTreeFree(table);
}


bool ResolveReferences(void)
{
	BTree * table = getReferenceTable();
	ASSERT(table)
	// the table's own comparator is not recorded; see InitializeReferences()
	table->compareItems = btreeCompareReferences;

	bool resolved = true;
	data64 missingHash = 0;
	BTreeIterator iterator;
	BTreeIterate(&iterator, table);
	while(BTreeIteratorNext(&iterator)) {
		Reference const * reference = BTreeIteratorPeekItem(&iterator);
		AnyFunction function = findFunctionByHash(reference->nameHash);
		if(!function) {
			resolved = false;
			missingHash = reference->nameHash;
			break;
		}
		CopyMemory(&function, (void *) reference->slot, sizeof(AnyFunction));
	}
	BTreeIteratorEnd(&iterator);
	if(!resolved) {
		PrintF("The paging file refers to a function that this version of atom does not have,"
			" with name hash %llx.\n", missingHash);
	}
	return resolved;
}


size32 NumberOfReferences(void)
{
	BTree * table = getReferenceTable();
	return table ? BTreeNItems(table) : 0;
}


void SetReference(void * slot, AnyFunction function)
{
	ASSERT(IsPagedMemoryAddress(slot))

	// Find the function in the registry
	RegisteredFunction const * registered = 0;
	if(function) {
		registered = findRegisteredFunction(function);
		if(!registered)
			Panic("SetReference(): function at %lx is not registered\n", functionAddress(function));
	}
	// Write the function pointer to the address
	CopyMemory(&function, slot, sizeof(AnyFunction));

	BTree * table = getReferenceTable();
	if(!table) {
		// A missing reference table only occurs while creating the table
		ASSERT(isCreatingTable)
		return;
	}
	Reference reference = {.slot = (addr64) slot};
	if(registered) {
		reference.nameHash = registered->nameHash;
		BTreeInsert(table, &reference);
	}
	else
		BTreeDelete(table, &reference, 0);
}


void ClearReferences(void const * start, size32 nBytes)
{
	BTree * table = getReferenceTable();
	if(!table)
		return;
	Reference const * found;
	while((found = findReferenceInRange(table, start, nBytes))) {
		Reference reference = *found;
		ASSERT(BTreeDelete(table, &reference, 0) == BTREE_DELETED)
	}
}


bool HasReferences(void const * start, size32 nBytes)
{
	BTree * table = getReferenceTable();
	if(!table)
		return false;
	return findReferenceInRange(table, start, nBytes) != 0;
}
