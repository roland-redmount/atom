
#include "btree/btree.h"
#include "lang/name.h"
#include "kernel/kernel.h"
#include "kernel/RelationBTree.h"
#include "memory/allocator.h"
#include "util/hashing.h"


// this structure is stored in the B-tree
typedef struct s_NameRecord {
	data64 hash;
	uint32 nReferences;
	size32 length;
	char * string;		// variable lengths string from Allocate()
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
	return CompareDatums(record1->hash, record2->hash);
}


static int8 btreeCompareNameRecords(void const * item1, void const * item2, size32 itemSize)
{
	return compareNameRecords((NameRecord *) item1, (NameRecord *) item2);
}

// when deallocating from the tree, we need to Free() the name string
static void btreeFreeNameRecord(void * item, size32 itemSize)
{
	NameRecord * record = (NameRecord *) item;
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


Atom CreateName(char const * string, size32 length)
{
	data64 hash = nameHash(string, length, djb2InitialHash);
	NameRecord * existingRecord = peekNameRecord(hash);
	if(existingRecord) {
		// name already exists
		// TODO: check for hash collision
		existingRecord->nReferences++;
	}
	else {
		// create new name
		NameRecord record;
		record.hash = hash;
		record.nReferences = 1;
		record.length = length;
		record.string = Allocate(length);
		CopyMemory(string, record.string, length);
		ASSERT(addNameRecord(&record));
	}
	nameStorage.nReferencesTotal++;
	return (Atom) {DT_NAME, 0, 0, 0, (Datum) hash};
}


Atom CreateNameFromCString(char const * cString)
{
	size32 length = CStringLength(cString);
	return CreateName(cString, length);
}


void NameAcquire(Atom name)
{
	ASSERT(name.type == DT_NAME);
	NameRecord * nameRecord = peekNameRecord(name.datum);
	nameRecord->nReferences++;
	nameStorage.nReferencesTotal++;
}


void NameRelease(Atom name)
{
	ASSERT(name.type == DT_NAME);
	NameRecord * nameRecord = peekNameRecord(name.datum);
	ASSERT(nameRecord->nReferences > 0);
	nameRecord->nReferences--;
	if(nameRecord->nReferences == 0) {
		ASSERT(BTreeDelete(nameStorage.tree, nameRecord) == BTREE_DELETED);
	}
	nameStorage.nReferencesTotal--;
}


uint32 NameTotalReferenceCount(void)
{
	return nameStorage.nReferencesTotal;
}


bool IsName(Atom atom)
{
	return atom.type == DT_NAME;
}

void PrintName(Atom name)
{
	NameRecord * nameRecord = peekNameRecord(name.datum);
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
	BTreeIterate(&iterator, nameStorage.tree, 0, 0);
	while(BTreeIteratorHasItem(&iterator)) {
		NameRecord const * nameRecord = BTreeIteratorPeekItem(&iterator);
		PrintF("%llx: ", nameRecord->hash);
		PrintCharString(nameRecord->string, nameRecord->length);
		PrintF(" %u references\n", nameRecord->nReferences);
		BTreeIteratorNext(&iterator);
	};
}
