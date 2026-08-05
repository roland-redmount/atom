
#include "kernel/UInt.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "kernel/dictionary.h"
#include "kernel/ifact.h"
#include "kernel/letter.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/Parameter.h"
#include "kernel/RelationBTree.h"
#include "kernel/RelationTable.h"
#include "kernel/RelationRegistry.h"
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

static const size8 corePredicateArity[N_CORE_PREDICATES + 1] = {
	0,
	3,	// (multiset element multiple)
	1,	// (predicate-form)
	3,	// (term-form predicate-form sign)
	1,	// (clause-form)
	1,	// (conjunction-form)
	3,	// (list position element)
	2,	// (list length)
	2,	// (quote quoted)
	1,	// (string)
};

/**
 * This defines a stable "kernel order" of roles in core predicates,
 * for "addressing" a role in a given predicate. The corresponding role index
 * in canonical order is provided by CorePredicateRoleIndex().
 * This ordering is also used as indexColumns in CreateRelationTable(), so that
 * lookup is fast on leading columns in this order.
 */ 
static const index32 coreFormRoleIds[N_CORE_PREDICATES + 1][CORE_FORMS_MAX_ARITY] = {
	{0},
	{ROLE_MULTISET, ROLE_ELEMENT, ROLE_MULTIPLE},
	{ROLE_PREDICATE_FORM},
	{ROLE_TERM_FORM, ROLE_PREDICATE_FORM, ROLE_SIGN},
	{ROLE_CLAUSE_FORM},
	{ROLE_CONJUNCTION_FORM},
	{ROLE_LIST, ROLE_POSITION, ROLE_ELEMENT},
	{ROLE_LIST, ROLE_LENGTH},
	{ROLE_QUOTE, ROLE_QUOTED},
	{ROLE_STRING},
};

/**
 * The core predicate form ID for each core relation
 */
static const index32 coreRelationFormId[N_CORE_RELATIONS + 1] = {
	0,
	FORM_MULTISET_ELEMENT_MULTIPLE,
	FORM_PREDICATE_FORM,
	FORM_MULTISET_ELEMENT_MULTIPLE,
	FORM_TERM_FORM,
	FORM_CLAUSE_FORM,
	FORM_CONJUNCTION_FORM,
	FORM_LIST_POSITION_ELEMENT,
	FORM_LIST_POSITION_ELEMENT,
	FORM_LIST_LENGTH,
	FORM_QUOTE_QUOTED,
	FORM_STRING,
};

/**
 * List of atom types for each relation, in the "kernel order" given by coreFormRoleIds
 * for the corresponding form.
 */
static const byte coreRelationAtomTypes[N_CORE_RELATIONS + 1][CORE_FORMS_MAX_ARITY] = {
	{0},
	// (multiset:ID element:NAME multiple:UINT)
	{AT_ID, AT_NAME, AT_UINT},
	// (predicate-form:ID)
	{AT_ID},
	// (multiset:ID element:ID multiple:INT)
	{AT_ID, AT_ID, AT_UINT},
	// (term-form:ID predicate-form:ID sign:UINT)
	{AT_ID, AT_ID, AT_UINT},
	// (clause-form:ID)
	{AT_ID},
	// (conjunction-form:ID)
	{AT_ID},
	// (list:ID position:UINT element:LETTER)
	{AT_ID, AT_UINT, AT_LETTER},
	// (list:ID position:UINT element:ID)
	{AT_ID, AT_UINT, AT_ID},
	// (list:ID length:UINT)
	{AT_ID, AT_UINT},
	// (quote:ID quoted:ID)
	{AT_ID, AT_ID},
	// (string:ID)
	{AT_ID},
};

/**
 * The core relation ID for each core service
 */
static const index32 coreServiceRelationId[N_CORE_SERVICES + 1] = {
	0,
	// (multiset <ID element >NAME multiple >UINT)
	RELATION_MULTISET_NAME,
	// (predicate-form >ID)
	RELATION_PREDICATE_FORM,
	// (multiset <ID element >ID multiple >UINT)
	RELATION_MULTISET_ID,
	// (multiset >ID element >ID multiple >UINT)
	RELATION_MULTISET_ID,
	// (term-form <ID predicate-form >ID)
	RELATION_TERM_FORM,
	// (list <ID length >UINT)
	RELATION_LIST_LENGTH,
	// (list <ID position >UINT element >LETTER)
	RELATION_LIST_LETTER,
	// (list <ID position >UINT element >ID)
	RELATION_LIST_ID,
};


/**
 * Parameter IO for core services, arguments in "kernel order"
 */
static const byte coreServiceParameterIO[N_CORE_SERVICES + 1][CORE_FORMS_MAX_ARITY] = {
	{0},
	// (multiset <ID element >NAME multiple >UINT)
	{PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT},
	// (predicate-form >ID)
	{PARAMETER_IN},
	// (multiset <ID element >ID multiple >UINT)
	{PARAMETER_IN, PARAMETER_IN, PARAMETER_OUT},
	// (multiset >ID element >ID multiple >UINT)
	{PARAMETER_OUT, PARAMETER_OUT, PARAMETER_OUT},
	// (term-form <ID predicate-form >ID sign >UINT)
	{PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT},
	// (list <ID length >UINT)
	{PARAMETER_IN, PARAMETER_OUT},
	// (list <ID position >UINT element >LETTER)
	{PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT},
	// (list <ID position >UINT element >ID)
	{PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT},
};


// TODO: this structure must be persistent
static struct s_Kernel {
	void * allocatorArea;

	// Core predicate forms and roles, defined during bootstrapping
	Atom corePredicateForms[N_CORE_PREDICATES + 1];
	Atom coreRoleNames[N_CORE_ROLES + 1];

	// Mapping of roles from "kernel order" in canonical order, such that
	// corePredicateRoleIndex[i][j] is the canonical order index of role j
	// in "kernel order" for form i.
	index8 corePredicateRoleIndex[N_CORE_PREDICATES + 1][CORE_FORMS_MAX_ARITY];
	// Corresponding core relations and services
	RelationTable const * coreRelations[N_CORE_RELATIONS + 1];
	Service * coreServices[N_CORE_SERVICES + 1];

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
	// running out of pages is not a bug on our part, so we cannot assume it away
	kernel.allocatorArea = AllocatePages(ALLOCATOR_N_PAGES);
	if(kernel.allocatorArea == 0)
		Panic("cannot reserve %u pages for the allocator\n", ALLOCATOR_N_PAGES);
	CreateAllocator(kernel.allocatorArea, LOG_ALLOCATOR_AREA_SIZE);
}


void CleanupMemory(void)
{
	// check for memory leaks
	size32 nBytesAllocated = AllocatorNBytesAllocated();
	if(nBytesAllocated > 0) {
		PrintF("Failed to free %u bytes of memory\n", nBytesAllocated);
		// PrintFreeLists();
		// DumpAllocatedBlocks();
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

/**
 * Permute an input given in "kernel order" into the canonical role order used
 * for relation table columns. Since corePredicateRoleIndex[formId][i] is the
 * canonical index of the role at kernel position i, the canonical index is the
 * destination, not the source.
 */
void CoreFormSetTuple(index32 formId, Atom const inputTuple[], Atom tuple[])
{
	for(index8 i = 0; i < corePredicateArity[formId]; i++)
		tuple[kernel.corePredicateRoleIndex[formId][i]] = inputTuple[i];
}


void CoreFormSetByteArray(index32 formId, byte const inputArray[], byte array[])
{
	for(index8 i = 0; i < corePredicateArity[formId]; i++)
		array[kernel.corePredicateRoleIndex[formId][i]] = inputArray[i];
}


/**
 * Create a core relation table using the B-tree implementation,
 * and create associated services.
 * This requires kernel.corePredicateRoleIndex to be initialized for the correponding form
 */
static RelationTable const * createCoreRelationTable(uint32 relationId)
{
	byte atomTypes[CORE_FORMS_MAX_ARITY];
	CoreFormSetByteArray(coreRelationFormId[relationId], coreRelationAtomTypes[relationId], atomTypes);
	index32 formId = coreRelationFormId[relationId];

	return CreateRelationBTreeWithServices(
		kernel.corePredicateForms[formId],
		corePredicateArity[formId],
		atomTypes,
		kernel.corePredicateRoleIndex[formId]
	);
}


RelationTable const * GetCoreRelationTable(index32 relationId)
{
	byte atomTypes[CORE_FORMS_MAX_ARITY];
	CoreFormSetByteArray(coreRelationFormId[relationId], coreRelationAtomTypes[relationId], atomTypes);
	index32 formId = coreRelationFormId[relationId];
	return RelationRegistryFind(
		kernel.corePredicateForms[formId],
		corePredicateArity[formId],
		atomTypes
	);
}


// ----------------- Core services, moved from ServiceRegistry.c -----------------------

Service * GetCoreService(index32 serviceId)
{
	return kernel.coreServices[serviceId];
}

/**
 * Setup core relation tables and associated services during bootstrap.
 */
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
	 *  (predicate-form @multiset-form)
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

	/*
	 * Reserve the IFact headers for the two forms, so that the relation tables
	 * below can acquire a reference to their form even though the corresponding
	 * defining facts cannot be built until those very tables exist.
	 * The headers are finalized by the IFactEndBootstrap() calls further down.
	 */
	IFactReserve(multisetForm.hash);
	IFactReserve(predicateForm.hash);

	// Create table for multisets of AT_NAME, used for predicate forms
	kernel.coreRelations[RELATION_MULTISET_NAME] = createCoreRelationTable(RELATION_MULTISET_NAME);
	// Create table for multisets of AT_NAME, used for predicate forms
	kernel.coreRelations[RELATION_MULTISET_ID] = createCoreRelationTable(RELATION_MULTISET_ID);
	// Create predicate form table
	kernel.coreRelations[RELATION_PREDICATE_FORM] = createCoreRelationTable(RELATION_PREDICATE_FORM);

	/*
	 * Create @multiset-form
	 */
	IFactDraft multisetDraft;
	IFactBegin(&multisetDraft);

	// defining facts
	// (multiset @multiset-form element "multiset" multiple 1)
	IFactBeginConjunction(
		&multisetDraft, kernel.coreRelations[RELATION_MULTISET_NAME], MULTISET_MULTISET_COLUMN);
	Atom multisetTuple[3];
	CoreFormSetTuple(
		FORM_MULTISET_ELEMENT_MULTIPLE,
		(Atom []) {multisetForm, GetCoreRoleName(ROLE_MULTISET), (Atom) {._uint = 1}},
		multisetTuple
	);
	IFactAddTuple(&multisetDraft, multisetTuple);
	// (multiset @multiset-form element "element" multiple 1)
	CoreFormSetTuple(
		FORM_MULTISET_ELEMENT_MULTIPLE,
		(Atom []) {multisetForm, GetCoreRoleName(ROLE_ELEMENT), (Atom) {._uint = 1}},
		multisetTuple
	);
	IFactAddTuple(&multisetDraft, multisetTuple);
	// (multiset @multiset-form element "multiple" multiple 1)
	CoreFormSetTuple(
		FORM_MULTISET_ELEMENT_MULTIPLE,
		(Atom []) {multisetForm, GetCoreRoleName(ROLE_MULTIPLE), (Atom) {._uint = 1}},
		multisetTuple
	);
	IFactAddTuple(&multisetDraft, multisetTuple);
	IFactEndConjunction(&multisetDraft);

	// (predicate-form @multiset-form)
	IFactBeginConjunction(&multisetDraft, kernel.coreRelations[RELATION_PREDICATE_FORM], 0);
	IFactAddTuple(&multisetDraft, (Atom[]) {multisetForm});
	IFactEndConjunction(&multisetDraft);

	/**
	 * We can't call the usual IFactEnd() here because it calls 
	 * (1) hashIFact(), while we want a predefined hash value
	 * (2) AtomAddRole() which requires these forms to be in place already
	 * Instead we define the hash to be the same as the form hash,
	 * and provide our own bootstrapAssertFact() function.
	 * This gives us 1 reference to the multisetForm atom.
	 */
	IFactEndBootstrap(&multisetDraft, multisetForm.hash);

	// Add lookup
	AtomAddRole(
		multisetForm,
		kernel.coreRelations[RELATION_MULTISET_NAME],
		GetCoreRoleName(ROLE_MULTISET)
	);
	AtomAddRole(
		multisetForm,
		kernel.coreRelations[RELATION_PREDICATE_FORM],
		GetCoreRoleName(ROLE_PREDICATE_FORM)
	);
	
	/*
	 * Create @predicate-form
	 */
	// TypedAtom predicateFormAtom = CreateTypedAtom(AT_ID, predicateForm);
	IFactDraft predicateFormDraft;
	IFactBegin(&predicateFormDraft);

	// defining facts
	// (multiset @predicate-form element "predicate-form" multiple 1)
	IFactBeginConjunction(
		&predicateFormDraft, kernel.coreRelations[RELATION_MULTISET_NAME], MULTISET_MULTISET_COLUMN);
	CoreFormSetTuple(
		FORM_MULTISET_ELEMENT_MULTIPLE,
		(Atom []) {predicateForm, GetCoreRoleName(ROLE_PREDICATE_FORM), (Atom) {._uint = 1}},
		multisetTuple
	);
	IFactAddTuple(&predicateFormDraft, multisetTuple);
	IFactEndConjunction(&predicateFormDraft);

	// (predicate-form @predicate-form)
	IFactBeginConjunction(&predicateFormDraft, kernel.coreRelations[RELATION_PREDICATE_FORM], 0);
	IFactAddTuple(&predicateFormDraft, (Atom[]) {predicateForm});
	IFactEndConjunction(&predicateFormDraft);

	// This gives 1 reference to the predicateForm atom
	IFactEndBootstrap(&predicateFormDraft, predicateForm.hash);

	// add lookup
	AtomAddRole(
		predicateForm,
		kernel.coreRelations[RELATION_MULTISET_NAME],
		GetCoreRoleName(ROLE_MULTISET)
	);
	AtomAddRole(
		predicateForm,
		kernel.coreRelations[RELATION_PREDICATE_FORM],
		GetCoreRoleName(ROLE_PREDICATE_FORM)
	);

	// We can now use CreatePredicateForm() and AssertFact()

	// Create remaining forms
	Atom roles[CORE_FORMS_MAX_ARITY];	
	for(index32 formId = FORM_TERM_FORM; formId <= N_CORE_PREDICATES; formId++) {
		for(index8 j = 0; j < corePredicateArity[formId]; j++)
			roles[j] = kernel.coreRoleNames[coreFormRoleIds[formId][j]];
		Atom form = CreatePredicateForm(roles, corePredicateArity[formId]);
		kernel.corePredicateForms[formId] = form;
		// precompute role indices (relation columns) for CorePredicateRoleIndex()
		for(index8 j = 0; j < corePredicateArity[formId]; j++)
			kernel.corePredicateRoleIndex[formId][j] = PredicateRoleIndex(form, roles[j]);
	}
	// NOTE: we now hold 1 reference to each of the core predicate forms.

	// Create remaining B-tree relation tables.
	for(index32 i = RELATION_TERM_FORM; i <= N_CORE_RELATIONS; i++)
		kernel.coreRelations[i] = createCoreRelationTable(i);

	// The relation table registry now holds references to each core predicate form,
	// so we can release our references.
	for(index32 i = 1; i <= N_CORE_PREDICATES; i++)
		IFactRelease(kernel.corePredicateForms[i]);

	// Lookup core services and store in array
	kernel.coreServices[0] = 0;
	byte parameterIO[CORE_FORMS_MAX_ARITY];
	for(index32 i = 1; i <= N_CORE_SERVICES; i++) {
		uint8 relationId = coreServiceRelationId[i];
		CoreFormSetByteArray(
			coreRelationFormId[relationId],
			coreServiceParameterIO[i],
			parameterIO
		);
		kernel.coreServices[i] = ServiceRegistryFind(
			kernel.coreRelations[relationId],
			parameterIO
		);
		ASSERT(kernel.coreServices[i])
	}
}


void KernelInitialize(void)
{
	SetupMemory();
	SetupRelationRegistry();
	SetupServiceRegistry();
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
	 * back to initial state. This is unnecessary in practise, as
	 * we would typically never completely destroy the "world" anyway.
	 * But it can be useful to ensure we have no memory leaks.
	 */

	 // Remove all relations and services except for RELATION_PREDICATE_FORM
	 // and RELATION_MULTISET_NAME. This also removes the associated predicate forms
	for(index32 relationId = N_CORE_RELATIONS; relationId > RELATION_PREDICATE_FORM; relationId--) {
		ServiceRegistryRemoveAll(kernel.coreRelations[relationId]);
		// This releases the associated form
		RelationRegistryRemove(kernel.coreRelations[relationId]);
	}

	/**
	 * Remove RELATION_PREDICATE_FORM and RELATION_MULTISET_NAME.
	 * These are circular by design. RELATION_PREDICATE_FORM now contains the tuples
	 *
	 *  (predicate-form @multiset-form)
	 *  (predicate-form @predicate-form)
	 *
	 * which are part of the defining facts for the atoms @predicate-form and @multiset-form.
	 * Similarly, RELATION_MULTISET_NAME now contains the tuples

	 *  (multiset @multiset-form element @multiset-role multiple 1)
	 *  (multiset @multiset-form element @element-role multiple 1)
	 *  (multiset @multiset-form element @multiple-role multiple 1)
	 *  (multiset @predicate-form element @predicate-form-role multiple 1)
	 *
	 * as created by setupCoreServices(). These two relation tables now carry the sole reference
	 * to @multiset-form and @predicate-form, respectively. Neither of the above tuples holds
	 * a reference to the form it defines, since the identified atom sits in the id column
	 * and is not acquired by RelationTableAddTuple().
	 *
	 * So we first detach the form reference held by each table, which is the only thing
	 * keeping the two ifacts alive. Each release then retracts that ifact's defining facts
	 * from both tables, which are still alive and serviced at this point. Once both are
	 * detached, the two tables are empty and can be torn down in the usual way.
	 */
	RelationTableReleaseForm(kernel.coreRelations[RELATION_MULTISET_NAME]);
	RelationTableReleaseForm(kernel.coreRelations[RELATION_PREDICATE_FORM]);

	ASSERT(RelationTableNRows(kernel.coreRelations[RELATION_PREDICATE_FORM]) == 0)
	ASSERT(RelationTableNRows(kernel.coreRelations[RELATION_MULTISET_NAME]) == 0)

	for(index32 relationId = RELATION_PREDICATE_FORM; relationId >= RELATION_MULTISET_NAME; relationId--) {
		ServiceRegistryRemoveAll(kernel.coreRelations[relationId]);
		RelationRegistryRemove(kernel.coreRelations[relationId]);
	}

	// Verify ifact counts
	ASSERT(IFactTotalCount() == 0)
	ASSERT(IFactTotalReferenceCount() == 0)

	uint32 nLookupEntries = LookupTotalCount();
	if(nLookupEntries > kernel.nCoreLookupEntries) {
		PrintF("Failed to remove %u lookup entries\n", 	nLookupEntries - kernel.nCoreLookupEntries);
		// print methods are not available for LookupDump() at this time
		ASSERT(false)
	}
	TeardownDictionary();
	FreeIFacts();
	FreeLookup();
	FreeServiceRegistry();
	FreeRelationRegistry();
	FreeNameStorage();
	CleanupMemory();
}


// TODO: this should return a status code indicating whether the fact was created,
// already existed, or if the assert failed due to logical inconsistency
void AssertFact(Atom predicateForm, TypedTuple const * actors, uint8 idPosition)
{
	// NOTE: currently we only support creating predicates
	ASSERT(IsPredicateForm(predicateForm));
	Atom const * actorsArray = TypedTuplePeekAtoms(actors);
	RelationTable const * table = RelationRegistryFind(predicateForm, actors->nAtoms, TypedTuplePeekAtomTypes(actors));
	if(table)
		RelationTableAddTuple(table, actorsArray, idPosition);
	else {
		// TODO: create a relation table if not exists? Default to B-tree?
		ASSERT(false);
	}
	LookupAddPredicateRoles(table, actorsArray);
}


void RetractFact(Atom predicateForm, TypedTuple * actors)
{
	RelationTable const * relation = RelationRegistryFind(predicateForm, actors->nAtoms, TypedTuplePeekAtomTypes(actors));
	// this will not remove defining facts
	Atom const * actorsArray = TypedTuplePeekAtoms(actors);
	RelationTableRemoveTuple(relation, actorsArray, 0);
	// NOTE: the below does not accept variables in the actors tuple,
	// so we can only retract 1 fact at a time.
	LookupRemovePredicateRoles(relation, actorsArray);

	// TODO: remove service if empty?
}
