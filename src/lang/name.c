
#include "btree/btree.h"
#include "lang/name.h"
#include "kernel/kernel.h"
#include "memory/allocator.h"
#include "util/hashing.h"


// this structure is stored in the B-tree
typedef struct s_NameRecord {
	data64 hash;
	uint32 nReferences;
	size32 length;
	// heap allocated string from Allocate(), no zero terminator
	char * string;
} NameRecord;


static struct {
	BTree * tree;
	uint32 nReferencesTotal;
} nameStorage;


static data64 nameHash(char const * string, size32 length, data64 initialHash)
{
	return DJB2DoubleHashAdd(string, length, initialHash);
}


static int8 compareNameRecords(NameRecord const * record1, NameRecord const * record2)
{
	return CompareAtoms((Atom) {.hash = record1->hash}, (Atom) {.hash = record2->hash});
}


static int8 btreeCompareNameRecords(void const * item1, void const * item2, size32 itemSize)
{
	return compareNameRecords(item1, item2);
}

// when deallocating from the tree, we need to Free() the name string
static void btreeFreeNameRecord(void const * item, size32 itemSize)
{
	NameRecord const * record = item;
	Free(record->string);
}


static NameRecord * peekNameRecord(data64 hash)
{
	NameRecord keyRecord;
	keyRecord.hash = hash;
	return (NameRecord *) BTreePeekItem(nameStorage.tree, &keyRecord);
}


static bool addNameRecord(NameRecord const * record)
{
	return BTreeInsert(nameStorage.tree, record);
}


void InitializeNameStorage(void)
{
	nameStorage.tree = BTreeCreate(
	    sizeof(NameRecord),
	    btreeCompareNameRecords,
	    btreeFreeNameRecord
	);
	nameStorage.nReferencesTotal = 0;
}


void FreeNameStorage(void)
{
	BTreeFree(nameStorage.tree);
}


size32 NumberOfNames(void)
{
	return BTreeNItems(nameStorage.tree);
}


Atom CreateName(char const * cString, size32 length)
{
	data64 hash = nameHash(cString, length, djb2InitialHash);
	NameRecord * existingRecord = peekNameRecord(hash);
	if(existingRecord) {
		// A name with the same hash exists.
		// Check for hash collision
		if(CompareMemory(cString, existingRecord->string, length)) {
			Panic(
				"Hash collision for names \"%s\" and \"%.*s\", hash = %llx",
				cString, existingRecord->length, existingRecord->string, hash
			);
		}
		existingRecord->nReferences++;
	}
	else {
		// create new name
		NameRecord record;
		record.hash = hash;
		record.nReferences = 1;
		record.length = length;
		record.string = Allocate(length);
		CopyMemory(cString, record.string, length);
		ASSERT(addNameRecord(&record));
	}
	nameStorage.nReferencesTotal++;
	return (Atom) {.hash = hash};
}


Atom CreateNameFromCString(char const * cString)
{
	size32 length = CStringLength(cString);
	return CreateName(cString, length);
}


void NameAcquire(Atom name)
{
	NameRecord * nameRecord = peekNameRecord(name.hash);
	ASSERT(nameRecord)
	nameRecord->nReferences++;
	nameStorage.nReferencesTotal++;
}


void NameRelease(Atom name)
{
	NameRecord * nameRecord = peekNameRecord(name.hash);
	ASSERT(nameRecord->nReferences > 0);
	nameRecord->nReferences--;
	if(nameRecord->nReferences == 0) {
		ASSERT(BTreeDelete(nameStorage.tree, nameRecord, 0) == BTREE_DELETED);
	}
	nameStorage.nReferencesTotal--;
}


uint32 NameTotalReferenceCount(void)
{
	return nameStorage.nReferencesTotal;
}


bool IsName(TypedAtom atom)
{
	return atom.type == AT_NAME;
}

void PrintName(Atom name)
{
	NameRecord * nameRecord = peekNameRecord(name.hash);
	ASSERT(nameRecord);
	PrintCharString(nameRecord->string, nameRecord->length);
}


data64 NameHashFromCString(char const * cString, data64 initialHash)
{
	return nameHash(cString, CStringLength(cString), initialHash);
}


void NameDump(void)
{
	PrintF("Name table %u names:\n", NumberOfNames());

	BTreeIterator iterator;
	BTreeIterate(&iterator, nameStorage.tree);
	while(BTreeIteratorNext(&iterator)) {
		NameRecord const * nameRecord = BTreeIteratorPeekItem(&iterator);
		PrintF("%llx (%llu) ", nameRecord->hash, nameRecord->hash);
		PrintCharString(nameRecord->string, nameRecord->length);
		PrintF(" %u references\n", nameRecord->nReferences);
	};
}
