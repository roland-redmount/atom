
#include "datumtypes/UInt.h"
#include "datumtypes/Variable.h"
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
	fixture.atoms[0] = CreateUInt(42);
	fixture.atoms[1] = GetAlphabetLetter('X');
	fixture.atoms[2] = GetAlphabetLetter('Y');
	return fixture;
}

#define EXAMPLE_LIST_N_ELEMENTS		3


static void testCreateList(void)
{
	AtomsFixture fixture = createAtomsFixture();
	
	Datum list = CreateListFromArray(fixture.atoms, EXAMPLE_LIST_N_ELEMENTS);
	
	// test (list length) relation table
	BTree * listLength = RegistryGetCoreTable(FORM_LIST_LENGTH);
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(listLength), 1)

	// test (list position element) relation table
	Datum listPositionElementForm = GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT);
	Service service = RegistryFindService(listPositionElementForm);
	ASSERT(service.type == SERVICE_BTREE)
	BTree * listPositionElement = service.service.tree;
	ASSERT_UINT32_EQUAL(RelationBTreeNRows(listPositionElement), EXAMPLE_LIST_N_ELEMENTS)

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
	tuple[listRoleIndex] = CreateTypedAtom(DT_ID, list);
	tuple[positionRoleIndex] = CreateUInt(7);
	tuple[elementRoleIndex] = GetAlphabetLetter('Z');
	ASSERT_UINT32_EQUAL(RelationBTreeAddTuple(listPositionElement, tuple), TUPLE_PROTECTED)

	// attempt to remove any tuple (list @string position _ element _) will violate the ifact
	tuple[listRoleIndex] = CreateTypedAtom(DT_ID, list);
	tuple[positionRoleIndex] = CreateUInt(3);
	tuple[elementRoleIndex] = GetAlphabetLetter('Y');
	ASSERT_UINT32_EQUAL(RelationBTreeRemoveTuples(listPositionElement, tuple, REMOVE_NORMAL), 0)
	
	IFactRelease(list);
}


typedef struct {
	AtomsFixture atomsFixture;
	Datum list;
} ExampleListFixture;

// a list containing only "small" datums
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
		CreateTypedAtom(DT_ID, fixture.list)
	};

	Datum nestedList = CreateListFromArray(nestedListAtoms, NESTED_LIST_N_ELEMENTS);
	
	// test ListGetElement
	for(index8 i = 0; i < NESTED_LIST_N_ELEMENTS; i++)
		ASSERT_TRUE(SameTypedAtoms(ListGetElement(nestedList, i+1), nestedListAtoms[i]))

	// test ListGetPosition
	index32 position = ListGetPosition(nestedList, CreateTypedAtom(DT_ID, fixture.list));
	ASSERT_UINT32_EQUAL(position, 2)

	IFactRelease(nestedList);

	teardownExampleListFixture(fixture);
}


static void testCreateEmptyList(void)
{
	Datum emptyList = CreateListFromArray(0, 0);
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

