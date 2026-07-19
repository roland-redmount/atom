
#include "kernel/UInt.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "kernel/dictionary.h"
#include "kernel/ifact.h"
#include "kernel/letter.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationTable.h"
#include "kernel/ServiceRegistry.h"
#include "memory/allocator.h"
#include "memory/paging.h"


/**
 * Make sure the C compiler used gives the expected type sizes.
 */ 
static void checkTypeSizes(void)
{
	ASSERT(sizeof(uint8) == 1)
	ASSERT(sizeof(uint16) == 2)
	ASSERT(sizeof(uint32) == 4)
	ASSERT(sizeof(uint64) == 8)

	ASSERT(sizeof(int8) == 1)
	ASSERT(sizeof(int16) == 2)
	ASSERT(sizeof(int32) == 4)
	ASSERT(sizeof(int64) == 8)

	ASSERT(sizeof(void *) == 8)

	ASSERT(sizeof(Atom) == 8)
	ASSERT(sizeof(TypedAtom) == 12)
}


// structure of core predicate forms

#define CORE_FORMS_MAX_ARITY		3

const index8 corePredicateArity[N_CORE_PREDICATES + 1] = {
	0,
	3,	// (multiset element multiple)
	1,	// (predicate-form)
	3,	// (term-form predicate-form sign)
	1,	// (clause-form)
	1,	// (conjunction-form)
	3,	// (list position element)
	2,	// (list length)
	3,	// (pair left right)
	2,	// (quote quoted)
	1,	// (string)
};

// This defines a "reference" order of roles in core predicates,
// for "addressing" a role in a given predicate. The actual role index
// is provided by kernel.corePredicateRoleIndex
const index32 coreFormRoleIds[N_CORE_PREDICATES + 1][CORE_FORMS_MAX_ARITY] = {
	{0},
	{ROLE_MULTISET, ROLE_ELEMENT, ROLE_MULTIPLE},
	{ROLE_PREDICATE_FORM},
	{ROLE_TERM_FORM, ROLE_PREDICATE_FORM, ROLE_SIGN},
	{ROLE_CLAUSE_FORM},
	{ROLE_CONJUNCTION_FORM},
	{ROLE_LIST, ROLE_POSITION, ROLE_ELEMENT},
	{ROLE_LIST, ROLE_LENGTH},
	{ROLE_PAIR, ROLE_LEFT, ROLE_RIGHT},
	{ROLE_QUOTE, ROLE_QUOTED},
	{ROLE_STRING},
};


const byte corePredicateAtomTypes[N_CORE_PREDICATES + 1][CORE_FORMS_MAX_ARITY] = {
	{0},
	{AT_ID, AT_NAME, AT_UINT},
	{AT_ID},
	{AT_ID, AT_ID, AT_UINT},
	{AT_ID},
	{AT_ID},
	// (list position element) for list of letters (strings)
	{AT_ID, AT_UINT, AT_LETTER},
	{AT_ID, AT_UINT},
	{AT_ID, AT_ID, AT_ID},
	{AT_ID, AT_ID},
	{AT_ID},
};


// TODO: this structure must be persistent
struct s_Kernel {
	void * allocatorArea;

	// Core predicate forms and roles, defined during bootstrapping
	// TODO: this is redundant with the ServiceRegistry core tables array
	//  which also stores the core predicate forms
	Atom corePredicateForms[N_CORE_PREDICATES + 1];
	Atom coreRoleNames[N_CORE_ROLES + 1];
	index8 corePredicateRoleIndex[N_CORE_PREDICATES + 1][CORE_FORMS_MAX_ARITY];

	// number of ifacts abnd references created by bootstrapping
	size32 nCoreIFacts;
	size32 nCoreIFactRefs;
	size32 nCoreNameRefs;
	size32 nCoreLookupEntries;

} kernel = {0};


Atom GetCorePredicateForm(index32 formId)
{
	ASSERT((formId >= 1) && (formId <= N_CORE_PREDICATES))
	return kernel.corePredicateForms[formId];
}


Atom GetCoreRoleName(index32 roleId)
{
	ASSERT((roleId >= 1) && (roleId <= N_CORE_ROLES))
	return kernel.coreRoleNames[roleId];
}


index8 CorePredicateRoleIndex(index32 formId, index32 roleId)
{
	for(index8 i = 0; i < corePredicateArity[formId]; i++) {
		if(coreFormRoleIds[formId][i] == roleId)
			return kernel.corePredicateRoleIndex[formId][i];
	}
	ASSERT(false)
	return 0;
}


// 1 << 20 = 1Mb memory area for allocator
#define LOG_ALLOCATOR_AREA_SIZE 	20
#define ALLOCATOR_AREA_SIZE 		(1 << LOG_ALLOCATOR_AREA_SIZE)
#define ALLOCATOR_N_PAGES			(ALLOCATOR_AREA_SIZE / MEMORY_PAGE_SIZE)


void SetupMemory(void)
{
	checkTypeSizes();
	InitializePaging();

	// setup allocator
	kernel.allocatorArea = AllocatePages(ALLOCATOR_N_PAGES);
	ASSERT(kernel.allocatorArea)
	CreateAllocator(kernel.allocatorArea, LOG_ALLOCATOR_AREA_SIZE);
}


void CleanupMemory(void)
{
	// check for memory leaks
	size32 nBytesAllocated = AllocatorNBytesAllocated();
	if(nBytesAllocated > 0) {
		PrintF("Failed to free %u bytes of memory\n", nBytesAllocated);
		PrintFreeLists();
		DumpAllocatedBlocks();
#ifdef DEBUG_ALLOCATE
		DumpAllocateLog();
#endif
		ASSERT(false)
	}
	ASSERT(AllocatorIsEmpty())
	CloseAllocator();
	FreePages(kernel.allocatorArea, ALLOCATOR_N_PAGES);
}


// byte const * GetCorePredicateAtomTypes(index32 formId)
// {
// 	return corePredicateAtomTypes[formId];
// }


static void setupCoreRoleNames(void)
{
	InitializeNameStorage();
	kernel.coreRoleNames[0] = (Atom) {.hash = 0};
	kernel.coreRoleNames[ROLE_MULTISET] = CreateNameFromCString("multiset");
	kernel.coreRoleNames[ROLE_ELEMENT] = CreateNameFromCString("element");
	kernel.coreRoleNames[ROLE_MULTIPLE] = CreateNameFromCString("multiple");
	kernel.coreRoleNames[ROLE_PREDICATE_FORM] = CreateNameFromCString("predicate-form");
	kernel.coreRoleNames[ROLE_TERM_FORM] = CreateNameFromCString("term-form");
	kernel.coreRoleNames[ROLE_CLAUSE_FORM] = CreateNameFromCString("clause-form");
	kernel.coreRoleNames[ROLE_CONJUNCTION_FORM] = CreateNameFromCString("conjunction-form");

	kernel.coreRoleNames[ROLE_LIST] = CreateNameFromCString("list");
	kernel.coreRoleNames[ROLE_POSITION] = CreateNameFromCString("position");
	kernel.coreRoleNames[ROLE_LENGTH] = CreateNameFromCString("length");

	kernel.coreRoleNames[ROLE_PAIR] = CreateNameFromCString("pair");
	kernel.coreRoleNames[ROLE_LEFT] = CreateNameFromCString("left");
	kernel.coreRoleNames[ROLE_RIGHT] = CreateNameFromCString("right");
	kernel.coreRoleNames[ROLE_QUOTE] = CreateNameFromCString("quote");
	kernel.coreRoleNames[ROLE_QUOTED] = CreateNameFromCString("quoted");
	kernel.coreRoleNames[ROLE_STRING] = CreateNameFromCString("string");
	kernel.coreRoleNames[ROLE_SIGN] = CreateNameFromCString("sign");
}


// The order of columns in the multiset relation table, needed by
// setupCorePredicateForms() before kernel.corePredicateRoleIndex is initialized

#define MULTISET_MULTISET_COLUMN	2
#define MULTISET_ELEMENT_COLUMN		1
#define MULTISET_MULTIPLE_COLUMN	0


// static index32 getCorePredicateIndex(Atom predicateForm)
// {
// 	index32 formIndex = 0;
// 	while(formIndex < N_CORE_PREDICATES) {
// 		if(kernel.corePredicateForms[++formIndex].hash == predicateForm.hash)
// 			break;
// 	}
// 	ASSERT(formIndex <= N_CORE_PREDICATES)
// 	return formIndex;	
// }

/**
 * Set the elements of the atomTypes array from the inputAtomTypes array,
 * which is ordered according to coreFormRoleIds[formId]
 */
static void setAtomTypes(byte atomTypes[], size32 formId, byte const inputAtomTypes[])
{
	for(index8 i = 0; i < corePredicateArity[formId]; i++)
		atomTypes[i] = inputAtomTypes[kernel.corePredicateRoleIndex[formId][i]];
}


// void bootstrapAssertFact(Atom predicateForm, TypedTuple const * actors, uint8 identified)
// {
// 	// TODO: this now needs to retrieve an agent, not a service ...
// 	// This is a quick hack to get the BTree * pointer)
// 	index32 formIndex = getCorePredicateIndex(predicateForm);
// 	ServiceRecord const * record = RegistryGetCoreServiceRecord(formIndex);
// 	RelationBTree * btree = record->service->impl.machine.providerData;
// 	RelationBTreeAddTuple(btree, TypedTuplePeekAtoms(actors), identified);
// }

/**
 * Convenience function to create a core relation table.
 * The inputAtomTypes array is ordered according to coreFormRoleIds[formId]
 */
static RelationTable createCoreRelationTable(uint32 formId, byte inputAtomTypes[])
{
	// (term-form predicate_form sign)
	byte atomTypes[CORE_FORMS_MAX_ARITY];
	setAtomTypes(atomTypes, formId, inputAtomTypes);
	return CreateRelationTable(
		&btreeTableProvider,
		kernel.corePredicateForms[formId],
		corePredicateArity[formId],
		atomTypes
	);
}


RelationTable GetCoreRelationTable(index32 formId)
{
	byte atomTypes[CORE_FORMS_MAX_ARITY];
	setAtomTypes(atomTypes, formId, corePredicateAtomTypes[formId]);
	return FindRelationTable(
		kernel.corePredicateForms[formId],
		atomTypes
	);
}


static void setupCoreServices(void)
{
	/** 
	 * We must first create the forms
	 * 
	 * @multiset-form  =  (multiset element multiple) and
	 * @predicate-form =  (predicate-form)
	 * 
	 * since these are required to define all other predicate forms, which
	 * are needed to create relation tables.
	 * 
	 * The defining fact for @multiset-form is
	 * 
	 *  (multiset @multiset-form element @multiset-role multiple 1) &
	 *  (multiset @multiset-form element @element-role multiple 1) &
	 *  (multiset @multiset-form element @multiple-role multiple 1) &
	 *  (predicate-form @form)
	 * 
	 * and for @predicate-form,
	 * 
	 *  (multiset @predicate-form element @predicate-form-role multiple 1) &
	 *  (predicate-form @predicate-form)
	 * 
	 * where the elements are role names (AT_NAME). The atom for each form is computed
	 * from the hash of each defining fact (See hashConjunction() in ifact.c).
	 * The @multiset-form and @predicate-form atoms are not part of the hash,
	 * but the hash of each defining fact depends on _its_ form, which is in
	 * turn either @multiset-form and @predicate-form. So the hash computation 
	 * for these forms is circular. Therefore, if we fix the hash to some
	 * pre-defined value, this will not agree with the hash value thereafter
	 * produced by hashIFact().
	 * 
	 * (We can describe this problem by an equation h = hash(h, tuples). The hash() function
	 * as currently implemented is complicated, and I don't know if there is a specific h
	 * that satisfies this equation. An option might be to redefine
	 * hash(h, tuples) = f(h, ht) where ht = hash(typles) and f() is some simple function,
	 * so that we can find an invariant h satisfying h = f(h, ht). But this is nontrivial.)
	 * 
	 * The current solution is to fix the hash value and create the corresponding IFact manually,
	 * bypassing the hash computation step.
	 */

	// fixed values for @multiset-form and @predicate-form
	Atom multisetForm = (Atom) {.hash = 1};
	kernel.corePredicateForms[FORM_MULTISET_ELEMENT_MULTIPLE] = multisetForm;
	Atom predicateForm = (Atom) {.hash = 2};
	kernel.corePredicateForms[FORM_PREDICATE_FORM] = predicateForm;

	// Set role index arrays
	kernel.corePredicateRoleIndex[FORM_MULTISET_ELEMENT_MULTIPLE][0] = MULTISET_MULTISET_COLUMN;
	kernel.corePredicateRoleIndex[FORM_MULTISET_ELEMENT_MULTIPLE][1] = MULTISET_ELEMENT_COLUMN;
	kernel.corePredicateRoleIndex[FORM_MULTISET_ELEMENT_MULTIPLE][2] = MULTISET_MULTIPLE_COLUMN;
	kernel.corePredicateRoleIndex[FORM_PREDICATE_FORM][0] = 0;

	// Create table for multisets of AT_NAME, used for predicate forms
	RelationTable multisetNameTable = createCoreRelationTable(
		FORM_MULTISET_ELEMENT_MULTIPLE,
		(byte[]) {AT_ID, AT_NAME, AT_UINT}
	);
	// Create table for multisets of AT_NAME, used for predicate forms
	// multisetAtomTypes[MULTISET_ELEMENT_COLUMN] = AT_ID;
	// RelationTable multisetIdTable = 
	createCoreRelationTable(
		FORM_MULTISET_ELEMENT_MULTIPLE,
		(byte[]) {AT_ID, AT_ID, AT_UINT}
	);
	// Create predicate form table
	RelationTable predicateFormTable = createCoreRelationTable(
		FORM_PREDICATE_FORM,
		(byte[]) {AT_ID}
	);

	/*
	 * Create @multiset-form
	 */
	IFactDraft multisetDraft;
	IFactBegin(&multisetDraft);

	// defining facts
	// (multiset @multiset-form element "multiset" multiple 1)
	IFactBeginConjunction(&multisetDraft, &multisetNameTable, MULTISET_MULTISET_COLUMN);
	Atom multisetTuple[3];
	MultisetSetTuple(
		multisetTuple, multisetForm, GetCoreRoleName(ROLE_MULTISET), (Atom) {._uint = 1});
	IFactAddTuple(&multisetDraft, multisetTuple);
	// (multiset @multiset-form element "element" multiple 1)
	MultisetSetTuple(
		multisetTuple, multisetForm, GetCoreRoleName(ROLE_ELEMENT), (Atom) {._uint = 1});
	IFactAddTuple(&multisetDraft, multisetTuple);
	// (multiset @multiset-form element "multiple" multiple 1)
	MultisetSetTuple(
		multisetTuple, multisetForm, GetCoreRoleName(ROLE_MULTIPLE), (Atom) {._uint = 1});
	IFactAddTuple(&multisetDraft, multisetTuple);
	IFactEndConjunction(&multisetDraft);

	// (predicate-form @multiset-form)
	IFactBeginConjunction(&multisetDraft, &predicateFormTable, 0);
	IFactAddTuple(&multisetDraft, (Atom[]) {multisetForm});
	IFactEndConjunction(&multisetDraft);

	/**
	 * We can't call the usual IFactEnd() here because it calls 
	 * (1) hashIFact(), while we want a predefined hash value
	 * (2) AssertFact() which requires these forms to be in place already
	 * Instead we define the hash to be the same as the form hash,
	 * and provide our own bootstrapAssertFact() function.
	 * This gives us 1 reference to the multisetForm atom.
	 */
	IFactEndBootstrap(&multisetDraft, multisetForm.hash);

	// Add lookup roles one by one
	AtomAddRole(multisetForm, multisetForm, GetCoreRoleName(ROLE_MULTISET));
	AtomAddRole(multisetForm, predicateForm, GetCoreRoleName(ROLE_PREDICATE_FORM));
	
	/*
	 * Create @predicate-form
	 */
	// TypedAtom predicateFormAtom = CreateTypedAtom(AT_ID, predicateForm);
	IFactDraft predicateFormDraft;
	IFactBegin(&predicateFormDraft);

	// defining facts
	// (multiset @predicate-form element "predicate-form" multiple 1)
	IFactBeginConjunction(&predicateFormDraft, &multisetNameTable, MULTISET_MULTISET_COLUMN);
	MultisetSetTuple(
		multisetTuple, predicateForm, GetCoreRoleName(ROLE_PREDICATE_FORM), (Atom) {._uint = 1});
	IFactAddTuple(&predicateFormDraft, multisetTuple);
	IFactEndConjunction(&predicateFormDraft);

	// (predicate-form @predicate-form)
	IFactBeginConjunction(&predicateFormDraft, &predicateFormTable, 0);
	IFactAddTuple(&predicateFormDraft, (Atom[]) {predicateForm});
	IFactEndConjunction(&predicateFormDraft);

	// This gives 1 reference to the predicateForm atom
	IFactEndBootstrap(&predicateFormDraft, predicateForm.hash);

	// add lookup
	AtomAddRole(predicateForm, multisetForm, GetCoreRoleName(ROLE_MULTISET));
	AtomAddRole(predicateForm, predicateForm, GetCoreRoleName(ROLE_PREDICATE_FORM));

	// We can now use CreatePredicateForm() and AssertFact()

	// Create remaining forms
	Atom roles[CORE_FORMS_MAX_ARITY];	
	for(index32 i = FORM_TERM_FORM; i <= N_CORE_PREDICATES; i++) {
		for(index8 j = 0; j < corePredicateArity[i]; j++)
			roles[j] = kernel.coreRoleNames[coreFormRoleIds[i][j]];

		Atom form = CreatePredicateForm(roles, corePredicateArity[i]);
		kernel.corePredicateForms[i] = form;

		// precompute role indices (relation columns) for CorePredicateRoleIndex()
		for(index8 j = 0; j < corePredicateArity[i]; j++)
			kernel.corePredicateRoleIndex[i][j] = PredicateRoleIndex(form, roles[j]);
	}
	// NOTE: we now hold 1 reference to each of the core predicate forms.

	// Create B-tree relation tables for each predicate form.
	// TODO: Corresponding services are now generated automatically by the
	// B-tree implementation, but this does not allow choosing index columns.
	// We probably need to create these services manually ...

	// (term-form predicate_form sign)
	createCoreRelationTable(FORM_TERM_FORM, (byte[]) {AT_ID, AT_ID, AT_UINT});
	// (clause-form)
	createCoreRelationTable(FORM_CLAUSE_FORM, (byte[]) {AT_ID});
	// (conjunction-form)
	createCoreRelationTable(FORM_CONJUNCTION_FORM, (byte[]) {AT_ID});
	// (list position element)
	createCoreRelationTable(FORM_LIST_POSITION_ELEMENT, (byte[]) {AT_ID, AT_UINT, AT_LETTER});
	createCoreRelationTable(FORM_LIST_POSITION_ELEMENT, (byte[]) {AT_ID, AT_UINT, AT_ID});
	// (list length)
	createCoreRelationTable(FORM_LIST_LENGTH, (byte[]) {AT_ID, AT_UINT});
	// (pair left right)
	createCoreRelationTable(FORM_PAIR_LEFT_RIGHT, (byte[]) {AT_ID, AT_ID, AT_ID});
	// (quote quoted)
	createCoreRelationTable(FORM_QUOTE_QUOTED, (byte[]) {AT_ID, AT_ID});
	// (string)
	createCoreRelationTable(FORM_STRING, (byte[]) {AT_ID});	

	// The relation table registry now holds references to each core predicate form,
	// so we can release our references
	for(index32 i = 1; i <= N_CORE_PREDICATES; i++)
		IFactRelease(kernel.corePredicateForms[i]);
}


void KernelInitialize(void)
{
	SetupMemory();
	SetupRegistry();
	InitializeLookup();
	InitializeIFacts();
	SetupDictionary();

	setupCoreRoleNames();
	setupCoreServices();

	kernel.nCoreIFacts = IFactTotalCount();
	kernel.nCoreIFactRefs = IFactTotalReferenceCount();
	kernel.nCoreNameRefs = NameTotalReferenceCount();
	kernel.nCoreLookupEntries = LookupTotalCount();
}


void KernelShutdown(void)
{
	// check for dangling ifacts
	uint32 ifactCount = IFactTotalCount();
	ASSERT(ifactCount >= kernel.nCoreIFacts)
	if(ifactCount > kernel.nCoreIFacts) {
		PrintF("Failed to remove %u ifacts\n", ifactCount - kernel.nCoreIFacts);
		// IFactDump();
		ASSERT(false);
	}
	// check for dangling references
	uint32 nIFactRefs = IFactTotalReferenceCount();
	ASSERT(nIFactRefs >= kernel.nCoreIFactRefs)
	if(nIFactRefs > kernel.nCoreIFactRefs) {
		PrintF("Failed to release %u ifact references\n", nIFactRefs - kernel.nCoreIFactRefs);
		ASSERT(false);
	}
	uint32 nNameRefs = NameTotalReferenceCount();
	ASSERT(nNameRefs >= kernel.nCoreNameRefs)
	if(nNameRefs > kernel.nCoreNameRefs) {
		PrintF("Failed to release %u name references\n", 	nNameRefs - kernel.nCoreNameRefs);
		ASSERT(false);
	}

	/**
	 * NOTE: The below removes all core services to rewind everything
	 * back to initial state. This is rather complicated due to special
	 * bootstrap considerations, and it is unnecessary in practise, as
	 * we would typically never completely destroy the "world" anyway.
	 * But it is useful to ensure we have no memory leaks.
	 */
	RegistryTeardownCoreServices();

	ASSERT(IFactTotalCount() == 0)
	ASSERT(IFactTotalReferenceCount() == 0)
	// remaining name references are freed by FreeNameStorage() below

	uint32 nLookupEntries = LookupTotalCount();
	if(nLookupEntries > kernel.nCoreLookupEntries) {
		PrintF("Failed to remove %u lookup entries\n", 	nLookupEntries - kernel.nCoreLookupEntries);
		// print methods are not available for LookupDump() at this time
		ASSERT(false)
	}
	TeardownDictionary();
	FreeIFacts();
	FreeLookup();
	FreeRegistry();
	FreeNameStorage();
	CleanupMemory();
}


// TODO: this should return a status code indicating whether the fact was created,
// already existed, or if the assert failed due to logical inconsistency
void AssertFact(Atom predicateForm, TypedTuple const * actors, uint8 idPosition)
{
	// NOTE: currently we only support creating predicates
	ASSERT(IsPredicateForm(predicateForm));
	RelationTable table = FindRelationTable(predicateForm, TypedTuplePeekAtomTypes(actors));
	if(table.provider)
		RelationTableAddTuple(&table, TypedTuplePeekAtoms(actors), idPosition);
	else {
		// TODO: create a relation table if not exists? Default to B-tree?
	}
	LookupAddPredicateRoles(predicateForm, actors);
}


void RetractFact(Atom predicateForm, TypedTuple * actors)
{
	RelationTable table = FindRelationTable(predicateForm, TypedTuplePeekAtomTypes(actors));
	// this will not remove defining facts
	RelationTableRemoveTuple(&table, TypedTuplePeekAtoms(actors), 0);
	// NOTE: the below does not accept variables in the actors tuple,
	// so we can only retract 1 fact at a time.
	LookupRemovePredicateRoles(predicateForm, actors);

	// TODO: remove service if empty?
}

/*
void RetractAllFacts(Atom predicateForm)
{
	removeBTreeTuples(predicateForm, 0);
	LookupRemoveAllPredicateRoles(predicateForm);
}
*/
