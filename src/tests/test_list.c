
#include "kernel/UInt.h"
#include "lang/Variable.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/letter.h"
#include "kernel/lookup.h"
#include "kernel/ServiceRegistry.h"

#include "testing/testing.h"


// TODO: we need more test cases!
// Should have a fuzz test with large number of tuples

#define EXAMPLE_N_ATOMS		3

typedef struct {
	TypedAtom atoms[EXAMPLE_N_ATOMS];
} AtomsFixture;


static AtomsFixture createAtomsFixture(void)
{
	AtomsFixture fixture;
	fixture.atoms[0] = CreateTypedAtom(AT_UINT, 42);
	fixture.atoms[1] = GetAlphabetLetter('X');
	fixture.atoms[2] = GetAlphabetLetter('Y');
	return fixture;
}

#define EXAMPLE_LIST_N_ELEMENTS		3


static void testCreateList(void)
{
	AtomsFixture fixture = createAtomsFixture();
	BTree * listLength = RegistryGetCoreTable(FORM_LIST_LENGTH);
	BTree * listPositionElement = RegistryGetCoreTable(FORM_LIST_POSITION_ELEMENT);
	size32 listLengthNRowsInitial = RelationBTreeNRows(listLength);
	size32 listPositionElementNRowsInitial = RelationBTreeNRows(listPositionElement);
	
	Atom list = CreateListFromArray(fixture.atoms, EXAMPLE_LIST_N_ELEMENTS);
	// test (list length) relation table
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(listLength),listLengthNRowsInitial + 1)
	// test (list position element) relation table
	ASSERT_UINT32_EQUAL(
		RelationBTreeNRows(listPositionElement),
		listPositionElementNRowsInitial + EXAMPLE_LIST_N_ELEMENTS
	)

	// test elements are as expected
	for(index8 i = 0; i < EXAMPLE_LIST_N_ELEMENTS; i++)
		ASSERT_TRUE(SameTypedAtoms(ListGetElement(list, i+1), fixture.atoms[i]))

	// test list length
	ASSERT_UINT32_EQUAL(ListLength(list), EXAMPLE_LIST_N_ELEMENTS)

	// test list iteration
	ListIterator iterator;
	ListIterate(list, &iterator);
	for(index8 i = 0; i < EXAMPLE_LIST_N_ELEMENTS; i++) {
		ASSERT_TRUE(ListIteratorHasNext(&iterator))
		TypedAtom element = ListIteratorGetElement(&iterator);
		ASSERT_TRUE(SameTypedAtoms(element, fixture.atoms[i]))
		ListIteratorNext(&iterator);
	}
	ASSERT_FALSE(ListIteratorHasNext(&iterator))
	ListIteratorEnd(&iterator);

	// test ListGetPosition
	for(index8 i = 0; i < EXAMPLE_LIST_N_ELEMENTS; i++)
		ASSERT_UINT32_EQUAL(ListGetPosition(list, fixture.atoms[i]), i + 1)

	TypedAtom tuple[3];

	index8 listRoleIndex = CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_LIST);
	index8 positionRoleIndex = CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_POSITION);
	index8 elementRoleIndex = CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_ELEMENT);

	// attempt to add a tuple (list @string position 7 element 'Z') will violate the ifact
	tuple[listRoleIndex] = CreateTypedAtom(AT_ID, list);
	tuple[positionRoleIndex] = CreateTypedAtom(AT_UINT, 7);
	tuple[elementRoleIndex] = GetAlphabetLetter('Z');
	ASSERT_UINT32_EQUAL(RelationBTreeAddTuple(listPositionElement, tuple), TUPLE_PROTECTED)

	// attempt to remove any tuple (list @string position _ element _) will violate the ifact
	tuple[listRoleIndex] = CreateTypedAtom(AT_ID, list);
	tuple[positionRoleIndex] = CreateTypedAtom(AT_UINT, 3);
	tuple[elementRoleIndex] = GetAlphabetLetter('Y');
	ASSERT_UINT32_EQUAL(RelationBTreeRemoveTuples(listPositionElement, tuple, REMOVE_NORMAL), 0)
	
	IFactRelease(list);
}


typedef struct {
	AtomsFixture atomsFixture;
	Atom list;
} ExampleListFixture;

// a list containing only "small" atoms
static ExampleListFixture setupExampleListFixture(void)
{
	ExampleListFixture fixture;
	fixture.atomsFixture = createAtomsFixture();
	fixture.list = CreateListFromArray( fixture.atomsFixture.atoms, EXAMPLE_LIST_N_ELEMENTS);
	return fixture;
}


static void teardownExampleListFixture(ExampleListFixture fixture)
{
	IFactRelease(fixture.list);
}


#define NESTED_LIST_N_ELEMENTS	2

static void testNestedList(void)
{
	// arrange
	ExampleListFixture fixture = setupExampleListFixture();

	TypedAtom nestedListAtoms[] = {
		GetAlphabetLetter('A'),
		CreateTypedAtom(AT_ID, fixture.list)
	};

	Atom nestedList = CreateListFromArray(nestedListAtoms, NESTED_LIST_N_ELEMENTS);
	
	// test ListGetElement
	for(index8 i = 0; i < NESTED_LIST_N_ELEMENTS; i++)
		ASSERT_TRUE(SameTypedAtoms(ListGetElement(nestedList, i+1), nestedListAtoms[i]))

	// test ListGetPosition
	index32 position = ListGetPosition(nestedList, CreateTypedAtom(AT_ID, fixture.list));
	ASSERT_UINT32_EQUAL(position, 2)

	IFactRelease(nestedList);

	teardownExampleListFixture(fixture);
}


static void testCreateEmptyList(void)
{
	Atom emptyList = CreateListFromArray(0, 0);
	ASSERT_TRUE(IsList(emptyList))
	ASSERT_UINT32_EQUAL(ListLength(emptyList), 0)

	ListIterator iterator;
	ListIterate(emptyList, &iterator);
	ASSERT_FALSE(ListIteratorHasNext(&iterator))
	ListIteratorEnd(&iterator);

	IFactRelease(emptyList);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testCreateList);
	ExecuteTest(testNestedList);
	ExecuteTest(testCreateEmptyList);

	KernelShutdown();

	TestSummary();
}

