
#include "kernel/UInt.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "kernel/ifact.h"
#include "kernel/letter.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"
#include "memory/allocator.h"
#include "memory/paging.h"
#include "vm/vm.h"


static void checkTypeSizes(void)
{
	// make sure the C compiler used gives the expected type sizes
	ASSERT(sizeof(uint8) == 1)
	ASSERT(sizeof(uint16) == 2)
	ASSERT(sizeof(uint32) == 4)
	ASSERT(sizeof(uint64) == 8)

	ASSERT(sizeof(int8) == 1)
	ASSERT(sizeof(int16) == 2)
	ASSERT(sizeof(int32) == 4)
	ASSERT(sizeof(int64) == 8)

	ASSERT(sizeof(void *) == 8)

	// check that packed data structures are padded correctly
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
	3,	// (formula form actors)
	2,	// (quote quoted)
	1,	// (string)
	2,	// (bytecode program)
	2,	// (bytecode registers)
	2,	// (bytecode constants)
};

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
	{ROLE_FORMULA, ROLE_FORM, ROLE_ACTORS},
	{ROLE_QUOTE, ROLE_QUOTED},
	{ROLE_STRING},
	{ROLE_BYTECODE, ROLE_PROGRAM},
	{ROLE_BYTECODE, ROLE_REGISTERS},
	{ROLE_BYTECODE, ROLE_CONSTANTS},
};

// TODO: this structure must be persistent
struct s_Kernel {
	void * allocatorArea;
	void * vmStack;

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


// 1 << 20 = 1Mb memory area for allocator
#define LOG_ALLOCATOR_AREA_SIZE 	20
#define ALLOCATOR_AREA_SIZE 		(1 << LOG_ALLOCATOR_AREA_SIZE)
#define ALLOCATOR_N_PAGES			(ALLOCATOR_AREA_SIZE / MEMORY_PAGE_SIZE)

// 1 << 20 = 1Mb memory area for VM stack
#define VM_STACK_AREA_SIZE 			(1 << 20)
#define VM_STACK_N_PAGES			(VM_STACK_AREA_SIZE / MEMORY_PAGE_SIZE)


void SetupMemory(void)
{
	checkTypeSizes();
	InitializePaging();

	// setup allocator
	kernel.allocatorArea = AllocatePages(ALLOCATOR_N_PAGES);
	ASSERT(kernel.allocatorArea)
	CreateAllocator(kernel.allocatorArea, LOG_ALLOCATOR_AREA_SIZE);

	// setup VM stack area
	kernel.vmStack = AllocatePages(VM_STACK_N_PAGES);
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



static void setupCoreRoleNames(void)
{
	InitializeNameStorage();
	kernel.coreRoleNames[0] = 0;
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
	kernel.coreRoleNames[ROLE_FORMULA] = CreateNameFromCString("formula");
	kernel.coreRoleNames[ROLE_QUOTE] = CreateNameFromCString("quote");
	kernel.coreRoleNames[ROLE_QUOTED] = CreateNameFromCString("quoted");
	kernel.coreRoleNames[ROLE_STRING] = CreateNameFromCString("string");
	kernel.coreRoleNames[ROLE_SIGN] = CreateNameFromCString("sign");
	kernel.coreRoleNames[ROLE_FORM] = CreateNameFromCString("form");
	kernel.coreRoleNames[ROLE_ACTORS] = CreateNameFromCString("actors");

	kernel.coreRoleNames[ROLE_BYTECODE] = CreateNameFromCString("bytecode");
	kernel.coreRoleNames[ROLE_PROGRAM] = CreateNameFromCString("program");
	kernel.coreRoleNames[ROLE_REGISTERS] = CreateNameFromCString("registers");
	kernel.coreRoleNames[ROLE_CONSTANTS] = CreateNameFromCString("constants");
}


// The order of columns in the multiset relation table, needed by
// setupCorePredicateForms() before kernel.corePredicateRoleIndex is initialized

#define MULTISET_MULTISET_COLUMN	2
#define MULTISET_ELEMENT_COLUMN		1
#define MULTISET_MULTIPLE_COLUMN	0


void bootstrapAssertFact(Atom predicateForm, TypedAtom * actors)
{
	ServiceRecord record = RegistryFindBTreeService(predicateForm);
	ASSERT(record.type == SERVICE_BTREE)
	RelationBTreeAddTuple(record.provider.tree, actors);
}


static void setupCoreServices(void)
{
	/** 
	 * We must first create the forms
	 * 
	 * @multiset-form  =  (multiset element multiple) and
	 * @predicate-form =  (predicate-form)
	 * 
	 * since these are required to define all other predicate forms.
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
	 * where the elements are role names. The atom for each form is computed
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
	Atom multisetForm = 1;
	kernel.corePredicateForms[FORM_MULTISET_ELEMENT_MULTIPLE] = multisetForm;
	Atom predicateForm = 2;
	kernel.corePredicateForms[FORM_PREDICATE_FORM] = predicateForm;

	// create services and set role index arrays
	BTree * multisetBTree = RegistryAddCoreBTreeService(
		FORM_MULTISET_ELEMENT_MULTIPLE,
		multisetForm,
		corePredicateArity[FORM_MULTISET_ELEMENT_MULTIPLE]
	);

	kernel.corePredicateRoleIndex[FORM_MULTISET_ELEMENT_MULTIPLE][0] = MULTISET_MULTISET_COLUMN;
	kernel.corePredicateRoleIndex[FORM_MULTISET_ELEMENT_MULTIPLE][1] = MULTISET_ELEMENT_COLUMN;
	kernel.corePredicateRoleIndex[FORM_MULTISET_ELEMENT_MULTIPLE][2] = MULTISET_MULTIPLE_COLUMN;

	BTree * predicateFormBTree = RegistryAddCoreBTreeService(
		FORM_PREDICATE_FORM,
		predicateForm,
		corePredicateArity[FORM_PREDICATE_FORM]
	);
	kernel.corePredicateRoleIndex[FORM_PREDICATE_FORM][0] = 0;
	
	// create @multiset-form
	TypedAtom multisetFormAtom = CreateTypedAtom(AT_ID, multisetForm);
	IFactDraft multisetDraft;
	IFactBegin(&multisetDraft);
	TypedAtom tuple[3];

	// defining facts
	// (multiset @multiset-form element "multiset" multiple 1)
	IFactBeginConjunction(&multisetDraft,multisetForm, multisetBTree, MULTISET_MULTISET_COLUMN);
	MultisetSetTuple(
		tuple,
		CreateTypedAtom(AT_ID, multisetForm),
		(TypedAtom) {.type = AT_NAME, .atom = GetCoreRoleName(ROLE_MULTISET)},
		CreateUInt(1)
	);
	IFactAddClause(&multisetDraft, tuple);
	// (multiset @multiset-form element "element" multiple 1)
	MultisetSetTuple(
		tuple,
		CreateTypedAtom(AT_ID, multisetForm),
		(TypedAtom) {.type = AT_NAME, .atom = GetCoreRoleName(ROLE_ELEMENT)},
		CreateUInt(1)
	);
	IFactAddClause(&multisetDraft, tuple);
	// (multiset @multiset-form element "multiple" multiple 1)
	MultisetSetTuple(
		tuple,
		CreateTypedAtom(AT_ID, multisetForm),
		(TypedAtom) {.type = AT_NAME, .atom = GetCoreRoleName(ROLE_MULTIPLE)},
		CreateUInt(1)
	);
	IFactAddClause(&multisetDraft, tuple);
	IFactEndConjunction(&multisetDraft);

	// (predicate-form @multiset-form)
	IFactBeginConjunction(&multisetDraft, predicateForm, predicateFormBTree, 0);
	IFactAddClause(&multisetDraft, &multisetFormAtom);
	IFactEndConjunction(&multisetDraft);

	/**
	 * We can't call the usual IFactEnd() here because it calls 
	 * (1) hashIFact(), while we want a predefined hash value
	 * (2) AssertFact() which requires these forms to be in place already
	 * Instead we define the hash to be the same as the form hash,
	 * and provide our own bootstrapAssertFact() function.
	 * This gives us 1 reference to the multisetForm atom.
	 */
	IFactEndBootstrap(&multisetDraft, multisetForm, bootstrapAssertFact);

	// add lookup
	AtomAddRole(multisetForm, multisetForm, GetCoreRoleName(ROLE_MULTISET));
	AtomAddRole(multisetForm, predicateForm, GetCoreRoleName(ROLE_PREDICATE_FORM));
	
	// create @predicate-form
	TypedAtom predicateFormAtom = CreateTypedAtom(AT_ID, predicateForm);
	IFactDraft predicateFormDraft;
	IFactBegin(&predicateFormDraft);

	// defining facts
	// (multiset @predicate-form element "predicate-form" multiple 1)
	IFactBeginConjunction(&predicateFormDraft, multisetForm, multisetBTree, MULTISET_MULTISET_COLUMN);
	MultisetSetTuple(
		tuple,
		predicateFormAtom,
		(TypedAtom) {.type = AT_NAME, .atom = GetCoreRoleName(ROLE_PREDICATE_FORM)},
		CreateUInt(1)
	);
	IFactAddClause(&predicateFormDraft, tuple);
	IFactEndConjunction(&predicateFormDraft);
	// (predicate-form @predicate-form)
	IFactBeginConjunction(&predicateFormDraft, predicateForm, predicateFormBTree, 0);
	IFactAddClause(&predicateFormDraft, &predicateFormAtom);
	IFactEndConjunction(&predicateFormDraft);

	// This gives 1 reference to the predicateForm atom
	IFactEndBootstrap(&predicateFormDraft, predicateForm, bootstrapAssertFact);

	// add lookup
	AtomAddRole(predicateForm, multisetForm, GetCoreRoleName(ROLE_MULTISET));
	AtomAddRole(predicateForm, predicateForm, GetCoreRoleName(ROLE_PREDICATE_FORM));

	// We can now use CreatePredicateForm() and AssertFact()

	// Create remaining forms and their associated services
	Atom roles[CORE_FORMS_MAX_ARITY];	
	for(index32 i = FORM_TERM_FORM; i <= N_CORE_PREDICATES; i++) {
		for(index8 j = 0; j < corePredicateArity[i]; j++)
			roles[j] = kernel.coreRoleNames[coreFormRoleIds[i][j]];

		Atom form = CreatePredicateForm(roles, corePredicateArity[i]);

		kernel.corePredicateForms[i] = form;
		RegistryAddCoreBTreeService(i, form, corePredicateArity[i]);

		// precompute role indices (relation columns) for GetPredicateRoleIndex()
		for(index8 j = 0; j < corePredicateArity[i]; j++) {
			kernel.corePredicateRoleIndex[i][j] = PredicateRoleIndex(
				kernel.corePredicateForms[i], kernel.coreRoleNames[coreFormRoleIds[i][j]]
			);
		}
	}
	// NOTE: we now hold 1 reference to each of the core predicate forms.

	// verify hardcoded multiset role index matches computed index
	ASSERT(CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET) == MULTISET_MULTISET_COLUMN)
	ASSERT(CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_ELEMENT) == MULTISET_ELEMENT_COLUMN)
	ASSERT(CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTIPLE) == MULTISET_MULTIPLE_COLUMN)

	RegistryFinalizeCoreServices();
	// The service registry now holds references to each core predicate form,
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

	setupCoreRoleNames();
	setupCoreServices();

	VMInitialize(kernel.vmStack, VM_STACK_AREA_SIZE);

	kernel.nCoreIFacts = TotalIFactCount();
	kernel.nCoreIFactRefs = TotalIFactReferenceCount();
	kernel.nCoreNameRefs = NameTotalReferenceCount();
	kernel.nCoreLookupEntries = LookupTotalCount();
}


void KernelShutdown(void)
{
	// check for dangling ifacts
	uint32 ifactCount = TotalIFactCount();
	ASSERT(ifactCount >= kernel.nCoreIFacts)
	if(ifactCount > kernel.nCoreIFacts) {
		PrintF("Failed to remove %u ifacts\n", ifactCount - kernel.nCoreIFacts);
		DumpIFacts();
		ASSERT(false);
	}
	// check for dangling references
	uint32 nIFactRefs = TotalIFactReferenceCount();
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
	 */
	RegistryTeardownCoreServices();

	ASSERT(TotalIFactCount() == 0)
	ASSERT(TotalIFactReferenceCount() == 0)
	// remaining name references are freed by FreeNameStorage() below

	uint32 nLookupEntries = LookupTotalCount();
	if(nLookupEntries > kernel.nCoreLookupEntries) {
		PrintF("Failed to remove %u lookup entries\n", 	nLookupEntries - kernel.nCoreLookupEntries);
		// print methods are not available for LookupDump() at this time
		ASSERT(false)
	}
	FreeIFacts();
	FreeLookup();
	FreeRegistry();
	FreeNameStorage();
	CleanupMemory();
}


// TODO: this should return a status code indicating whether the fact was created,
// already existed, or if the assert failed due to logical inconsistency
void AssertFact(Atom predicateForm, TypedAtom * actors)
{
	// TODO: currently we only support creating predicates
	ASSERT(IsPredicateForm(predicateForm));
	// add tuple to relation table
	ServiceRecord record = RegistryFindBTreeService(predicateForm);
	if(record.type == SERVICE_BTREE) {
		RelationBTreeAddTuple(record.provider.tree, actors);
	}
	else if(record.type == SERVICE_NONE) {
		// create new relation table
		size8 arity = PredicateArity(predicateForm);
		BTree * btree = CreateRelationBTree(arity);
		RegistryAddBTreeService(predicateForm, btree);
		RelationBTreeAddTuple(btree, actors);
	}
	else {
		ASSERT(false)
	}
	LookupAddPredicateRoles(predicateForm, actors);
}


void RetractFact(Atom predicateForm, TypedAtom * actors)
{
	ServiceRecord record = RegistryFindBTreeService(predicateForm);
	ASSERT(record.type == SERVICE_BTREE)
	BTree * btree = record.provider.tree;
	ASSERT(btree)
	// NOTE: this can cause IFacts to be removed if the tuple
	// being removed holds the last reference to an IFact
	RelationBTreeRemoveTuples(btree, actors, REMOVE_NORMAL);
	// remove btree if empty
	if(RelationBTreeNRows(btree) == 0) {
		RegistryRemoveService(record.service);
	}

	// remove lookup entries for each role in the predicate form
	LookupRemovePredicateRoles(predicateForm, actors);
}


void RetractAllFacts(Atom predicateForm)
{
	ServiceRecord record = RegistryFindBTreeService(predicateForm);
	ASSERT(record.type == SERVICE_BTREE)
	RelationBTreeRemoveTuples(record.provider.tree, 0, REMOVE_NORMAL);

	LookupRemoveAllPredicateRoles(predicateForm);
}


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
