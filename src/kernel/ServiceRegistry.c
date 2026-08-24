
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/tuple.h"
#include "lang/TypedAtom.h"
#include "lang/formula.h"
#include "memory/allocator.h"
#include "util/ResizingArray.h"


/**
 * The registry of services, as a B-tree storing Service items.
 * The relations these refer to are registered separately; see RelationRegistry.h
 */
static BTree * services;

/**
 * Index mapping each root operator to the relations for which it is a root operator;
 * the pair (operator, relation) is 1:1 with a registered Service, so this allows
 * lookup of services by root operator.
 * Note that two services may share the same root operator.
 * A lookup with a null relation is a prefix key matching every service with root operator op.
 */
typedef struct {
	Operator const * op;
	Relation const * relation;
} OperatorRelation;

static BTree * operatorRelations;

/**
 * A record of a dependency where the operator "dependent" depends on the operator "dependency".
 * Both operators must be the root operator in some service. The dependent operator is
 * always from a compiled service.
 * If a dependent = 0, the record is a prefix key matching every dependent of the dependency.
 */
typedef struct {
	Operator * dependency;
	Operator * dependent;
} OperatorDependency;

static BTree * dependencies;

// Number of registered services compiled from the rules
static size32 nCompiledServices;


static void setupService(
	Service * service, Relation const * relation, IOSignature ioSignature, Operator * op,
	enum ServiceKind kind)
{
	ASSERT(relation->nColumns <= RELATION_MAX_ARITY)
	SetMemory(service, sizeof(Service), 0);
	service->relation = relation;
	service->kind = kind;
	service->ioSignature = ioSignature;
	service->op = op;
}


static int8 compareServices(Service const * service, Service const * serviceOrKey)
{
	// First compare relations
	if(service->relation < serviceOrKey->relation)
		return -1;
	else if(service->relation > serviceOrKey->relation)
		return 1;
	else {
		// then compare IO signatures; a zeroed signature for the key matches any IO
		if(!serviceOrKey->ioSignature.parameterIO[0])
			return 0;
		return CompareMemory(
			service->ioSignature.parameterIO, serviceOrKey->ioSignature.parameterIO,
			service->relation->nColumns);
	}
}


static int8 btreeCompareServices(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareServices((Service *) item, (Service *) itemOrKey);
}


/**
 * Order OperatorRelation records by operator, the by relation.
 * A null relation is a prefix key matching every service of the operator.
 */
static int8 btreeCompareOperatorRelations(void const * item, void const * itemOrKey, size32 itemSize)
{
	OperatorRelation const * record = item;
	OperatorRelation const * recordOrKey = itemOrKey;
	if(record->op < recordOrKey->op)
		return -1;
	if(record->op > recordOrKey->op)
		return 1;
	if(!recordOrKey->relation)
		return 0;
	if(record->relation < recordOrKey->relation)
		return -1;
	if(record->relation > recordOrKey->relation)
		return 1;
	return 0;
}


/**
 * Order OperatorDependency records by the the dependency, then by the dependent operator.
 * A null dependent is a prefix key matching every dependent of the operator.
 */
static int8 btreeCompareDependencies(void const * item, void const * itemOrKey, size32 itemSize)
{
	OperatorDependency const * record = item;
	OperatorDependency const * recordOrKey = itemOrKey;
	if(record->dependency < recordOrKey->dependency)
		return -1;
	if(record->dependency > recordOrKey->dependency)
		return 1;
	if(!recordOrKey->dependent)
		return 0;
	if(record->dependent < recordOrKey->dependent)
		return -1;
	if(record->dependent > recordOrKey->dependent)
		return 1;
	return 0;
}


void SetupServiceRegistry(void)
{
	// B-tree of Services, mapping relations -> services
	services = BTreeCreate(
		sizeof(Service),
		btreeCompareServices,
		0	// nothing to deallocate
	);
	operatorRelations = BTreeCreate(sizeof(OperatorRelation), btreeCompareOperatorRelations, 0);
	dependencies = BTreeCreate(sizeof(OperatorDependency), btreeCompareDependencies, 0);
	nCompiledServices = 0;
}


void FreeServiceRegistry(void)
{
	BTreeFree(dependencies);
	BTreeFree(operatorRelations);
	BTreeFree(services);
}


/**
 * Copy the service of the given relation evaluated by the given operator to *service.
 * Returns false if the registry holds no such service.
 */
static bool findService(Relation const * relation, Operator const * op, Service * service)
{
	Service key;
	setupService(&key, relation, (IOSignature) {0}, (Operator *) op, SERVICE_PRIMITIVE);
	BTreeIterator iterator;
	BTreeIterate(&iterator, services);
	bool found = false;
	if(BTreeIteratorSeek(&iterator, &key)) {
		do {
			Service const * candidate = BTreeIteratorPeekItem(&iterator);
			// the seek finds the first service of the relation, so leave the loop
			// once the services of another relation are reached
			if(compareServices(candidate, &key) != 0)
				break;
			if(candidate->op == op) {
				*service = *candidate;
				found = true;
				break;
			}
		} while(BTreeIteratorNext(&iterator));
	}
	BTreeIteratorEnd(&iterator);
	return found;
}


/**
 * Test whether the given operator is the root operator of some service.
 */
static bool isServiceOperator(Operator const * op)
{
	OperatorRelation key = {.op = op, .relation = 0};
	return BTreeContainsItem(operatorRelations, &key);
}


/**
 * Collect the relations of the services where op is the root operator,
 * and add to the relations array.
 * More than one relation may be added if several services share the operator op.
 */
static void collectOperatorRelations(Operator const * op, ResizingArray * relations)
{
	OperatorRelation key = {.op = op, .relation = 0};
	BTreeIterator iterator;
	BTreeIterate(&iterator, operatorRelations);
	if(BTreeIteratorSeek(&iterator, &key)) {
		do {
			OperatorRelation const * record = BTreeIteratorPeekItem(&iterator);
			if(record->op != op)
				break;
			ResizingArrayAppend(relations, &(record->relation));
		} while(BTreeIteratorNext(&iterator));
	}
	BTreeIteratorEnd(&iterator);
}


/**
 * Record dependencies between the dependent (a root operator of some service) and
 * every child operator that is also a root operator in some other service.
 * Recursively follows child operators that are not root operators.
 */
static void recordDependencies(Operator * dependent, Operator const * op)
{
	for(index8 i = 0; i < OperatorNChildren(op); i++) {
		Operator * child = OperatorGetChild(op, i);
		if(isServiceOperator(child)) {
			OperatorDependency dependency = {.dependency = child, .dependent = dependent};
			BTreeInsert(dependencies, &dependency);
		}
		else
			recordDependencies(dependent, child);
	}
}


/**
 * Remove the dependency records where the given operator is the dependent.
 * This is used when a service is removed to release the dependencies of its root operator.
 */
static void removeDependencyRecords(Operator const * dependent)
{
	// Array of dependencies to be removed
	ResizingArray staleDependencies;
	CreateResizingArray(&staleDependencies, sizeof(OperatorDependency), 8);

	// Collect before removing: a B-tree cannot be modified while it is iterated
	// NOTE: this scans the entire dependencies B-tree
	BTreeIterator iterator;
	BTreeIterate(&iterator, dependencies);
	while(BTreeIteratorNext(&iterator)) {
		OperatorDependency const * dependency = BTreeIteratorPeekItem(&iterator);
		if(dependency->dependent == dependent)
			ResizingArrayAppend(&staleDependencies, dependency);
	}
	BTreeIteratorEnd(&iterator);

	for(index32 i = 0; i < ResizingArrayNElements(&staleDependencies); i++)
		ASSERT(BTreeDelete(
			dependencies, ResizingArrayGetElement(&staleDependencies, i), 0) == BTREE_DELETED)
	FreeResizingArray(&staleDependencies);
}


/**
 * Remove a service from the registry and its index, releasing the references to its
 * operator and its relation. Nothing is invalidated here; see invalidateDependents().
 */
static void removeService(Service const * service)
{
	if(service->kind == SERVICE_COMPILED)
		nCompiledServices--;
	OperatorRelation record = {.op = service->op, .relation = service->relation};
	ASSERT(BTreeDelete(operatorRelations, &record, 0) == BTREE_DELETED)
	// The dependencies are the operator's, so they stand as long as another service is
	// evaluated by it
	if(!isServiceOperator(service->op))
		removeDependencyRecords(service->op);

	Operator * op = service->op;
	Relation const * relation = service->relation;
	ASSERT(BTreeDelete(services, service, 0) == BTREE_DELETED)
	ReleaseOperator(op);
	ReleaseRelation(relation);
}


/**
 * Add an operator to the staleOperators list, unless it is already present.
 * The list owns a reference to every operator on it, until freeStaleOperators() releases it.
 * This is to prevent premature free'ing of the operator by removeService().
 */
static void addStaleOperator(ResizingArray * staleOperators, Operator * op)
{
	// The list holds each operator once, so that it is visited once
	for(index32 i = 0; i < ResizingArrayNElements(staleOperators); i++) {
		if(*(Operator **) ResizingArrayGetElement(staleOperators, i) == op)
			return;
	}
	AcquireOperator(op);
	ResizingArrayAppend(staleOperators, &op);
}


/**
 * Remove every compiled service (SERVICE_COMPILED) that has the given operator as its root
 * operator. A primitive service is not removed, being part of the knowledge base rather
 * than derived from it.
 *
 * One operator may be the root of several services, one per relation. Such service yield
 * the same tuples under different signatures, and so are all stale when op is stale.
 */
static void removeOperatorServices(Operator const * op)
{
	// Collect the relations for services with op as root operator, to be removed
	ResizingArray relations;
	CreateResizingArray(&relations, sizeof(Relation const *), 4);
	collectOperatorRelations(op, &relations);

	for(index32 i = 0; i < ResizingArrayNElements(&relations); i++) {
		Relation const * relation =
			*(Relation const **) ResizingArrayGetElement(&relations, i);
		Service service;
		if(!findService(relation, op, &service))
			continue;
		if(service.kind != SERVICE_COMPILED)
			continue;
		removeService(&service);
	}
	FreeResizingArray(&relations);
}


/**
 * Work through the list of stale operators, removing the compiled services each of them
 * evaluates and putting the operators that depend on it on the list. Whatever depends on
 * those is then reached in turn: the list is the work list of a walk over the dependency
 * graph, and grows while it is walked.
 *
 * Removing the services of a stale operator is also what takes a service sharing its
 * operator with a stale one: the two are one operator, and the same tuples under two
 * signatures.
 */
static void invalidateDependents(ResizingArray * staleOperators)
{
	for(index32 i = 0; i < ResizingArrayNElements(staleOperators); i++) {
		Operator * stale = *(Operator **) ResizingArrayGetElement(staleOperators, i);
		removeOperatorServices(stale);

		// Every root operator depending on the stale one is stale in turn,
		// so add it to the stale list
		OperatorDependency key = {.dependency = stale, .dependent = 0};
		BTreeIterator iterator;
		BTreeIterate(&iterator, dependencies);
		if(BTreeIteratorSeek(&iterator, &key)) {
			do {
				OperatorDependency const * dependency = BTreeIteratorPeekItem(&iterator);
				if(dependency->dependency != stale)
					break;	// no more matches
				// Appending modifies no B-tree, and so is safe under the iterator
				addStaleOperator(staleOperators, dependency->dependent);
			} while(BTreeIteratorNext(&iterator));
		}
		BTreeIteratorEnd(&iterator);
	}
}


/**
 * Release the operator references the work list holds, and deallocate it. No reference
 * outlives the invalidation that created the list.
 */
static void freeStaleOperators(ResizingArray * staleOperators)
{
	for(index32 i = 0; i < ResizingArrayNElements(staleOperators); i++)
		ReleaseOperator(*(Operator **) ResizingArrayGetElement(staleOperators, i));
	FreeResizingArray(staleOperators);
}


Service ServiceRegistryAdd(
	Relation const * relation, IOSignature ioSignature, Operator * op,
	enum ServiceKind kind)
{
	Service service;
	setupService(&service, relation, ioSignature, op, kind);
	// add to the service registry
	ASSERT(BTreeInsert(services, &service) == BTREE_INSERTED)
	AcquireOperator(op);
	AcquireRelation(relation);
	// add to the operator-relation index
	OperatorRelation record = {.op = op, .relation = relation};
	ASSERT(BTreeInsert(operatorRelations, &record) == BTREE_INSERTED)

	switch(kind) {
	case SERVICE_COMPILED:
		nCompiledServices++;
		recordDependencies(op, op);
		break;

	case SERVICE_PRIMITIVE:
		// A query of this term form may now have one more relation to match, so
		// whatever was compiled for it is incomplete
		ServiceRegistryInvalidateByTermForm(relation->termForm);
		break;

	case SERVICE_TEMPORARY:
		// Compiler scaffolding, which is never registered; see ServiceKind
		ASSERT(false)
		break;
	}

	// NOTE: this returns a copy of the Service struct, but the allocated
	// service.paramaeterIO array is still owned by the registry.
	return service;
}


void ServiceRegistryRemove(Relation const * relation, Operator * op)
{
	Service service;
	bool found = findService(relation, op, &service);
	ASSERT(found)

	// Add the operator to a new stale list
	ResizingArray staleOperators;
	CreateResizingArray(&staleOperators, sizeof(Operator *), 8);
	addStaleOperator(&staleOperators, op);

	removeService(&service);

	invalidateDependents(&staleOperators);
	freeStaleOperators(&staleOperators);
}


void ServiceRegistryRemoveAll(Relation const * relation)
{
	ResizingArray staleOperators;
	CreateResizingArray(&staleOperators, sizeof(Operator *), 8);

	Service key;
	setupService(&key, relation, (IOSignature) {0}, 0, SERVICE_PRIMITIVE);
	Service service;
	while(BTreeGetItem(services, &key, &service)) {
		addStaleOperator(&staleOperators, service.op);
		removeService(&service);
	}
	// service is shallow-copied by BTreeGetItem() and does not need deallocation

	invalidateDependents(&staleOperators);
	freeStaleOperators(&staleOperators);
}


/**
 * Remove every compiled service the given operator evaluates, and every compiled service
 * built on one of them, transitively.
 */
static void invalidateOperator(Operator * op)
{
	ResizingArray staleOperators;
	CreateResizingArray(&staleOperators, sizeof(Operator *), 8);
	addStaleOperator(&staleOperators, op);
	invalidateDependents(&staleOperators);
	freeStaleOperators(&staleOperators);
}


void ServiceRegistryInvalidateByTermForm(Atom termForm)
{
	if(nCompiledServices == 0)
		return;

	// Collect stale operators: a compiled service is stale itself, and a
	// primitive service may be referred to by compiled services, which are then stale.
	ResizingArray staleOperators;
	CreateResizingArray(&staleOperators, sizeof(Operator *), 8);

	// Iterate over all relations matching the the termForm
	RelationIterator relationIterator;
	RelationRegistryIterate(termForm, &relationIterator);
	while(RelationIteratorNext(&relationIterator)) {
		Relation const * relation = RelationIteratorGet(&relationIterator);
		// Iterate over all services for the relation
		ServiceIterator serviceIterator;
		ServiceRegistryIterate(relation, &serviceIterator);
		while(ServiceIteratorNext(&serviceIterator))
			addStaleOperator(
				&staleOperators, ServiceIteratorPeekService(&serviceIterator)->op);
		ServiceIteratorEnd(&serviceIterator);
	}
	RelationIteratorEnd(&relationIterator);

	invalidateDependents(&staleOperators);
	freeStaleOperators(&staleOperators);
}


void ServiceRegistryInvalidateAll(void)
{
	while(nCompiledServices > 0) {
		// Find the operator of one compiled service; invalidating it takes the ones
		// built on it too, so the loop terminates
		Operator * op = 0;
		BTreeIterator iterator;
		BTreeIterate(&iterator, services);
		while(!op && BTreeIteratorNext(&iterator)) {
			Service const * candidate = BTreeIteratorPeekItem(&iterator);
			if(candidate->kind == SERVICE_COMPILED)
				op = candidate->op;
		}
		BTreeIteratorEnd(&iterator);
		ASSERT(op)
		invalidateOperator(op);
	}
}


size32 ServiceRegistryCount(void)
{
	return BTreeNItems(services);
}


size32 ServiceRegistryNCompiled(void)
{
	return nCompiledServices;
}


void ServiceRegistryIterate(Relation const * relation, ServiceIterator * iterator)
{
	iterator->relation = relation;
	BTreeIterate(&(iterator->btreeIterator), services);
}


Service const * ServiceIteratorPeekService(ServiceIterator const * iterator)
{
	return BTreeIteratorPeekItem(&(iterator->btreeIterator));
}


bool ServiceIteratorNext(ServiceIterator * iterator)
{
	Service key = {
		.relation = iterator->relation,
		.ioSignature = {.parameterIO = {0}},
		.op = 0
	};
	bool foundItem;
	if(BTreeIteratorBeforeFirst(&iterator->btreeIterator)) {
		foundItem = BTreeIteratorSeek(&(iterator->btreeIterator), &key);
	}
	else
		foundItem = BTreeIteratorNext(&(iterator->btreeIterator));

	if(foundItem) {
		Service const * btreeService = BTreeIteratorPeekItem(&(iterator->btreeIterator));
		if(compareServices(btreeService, &key) == 0)
			return true;		
	}
	return false;
}


void ServiceIteratorEnd(ServiceIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->btreeIterator));
}


Operator * ServiceRegistryFind(Relation const * relation, IOSignature ioSignature)
{
	Service key;
	setupService(&key, relation, ioSignature, 0, SERVICE_PRIMITIVE);

	BTreeIterator iterator;
	BTreeIterate(&iterator, services);
	Operator * op = 0;
	if(BTreeIteratorSeek(&iterator, &key)) {
		Service * service = BTreeIteratorPeekItem(&iterator);
		op = service->op;
	}
	BTreeIteratorEnd(&iterator);
	return op;
}


bool ServiceRegistryFindByMachineProvider(
	MachineProvider const * provider, Service * service)
{
	bool found = false;
	BTreeIterator iterator;
	BTreeIterate(&iterator, services);
	while(!found && BTreeIteratorNext(&iterator)) {
		Service const * candidate = BTreeIteratorPeekItem(&iterator);
		if((candidate->op->type == OPERATOR_MACHINE)
			&& (candidate->op->impl.machine.provider == provider)) {
			*service = *candidate;
			found = true;
		}
	}
	BTreeIteratorEnd(&iterator);
	return found;
}


void PrintService(Service const * service)
{
	TypedTuple * parameters = CreateTypedTuple(service->op->nArguments);
	for(index8 i = 0; i < service->op->nArguments; i++) {
		TypedAtom parameter = CreateTypedAtom(
			AT_PARAMETER,
			(Atom) {
				.parameter = {
					.number = i + 1,
					.atomType =	service->relation->typeSignature.atomTypes[i],
					.io = service->ioSignature.parameterIO[i]
				}
			}
		);
		TypedTupleSetElement(parameters, i, parameter);
	}
	PrintFormActorsAsFormula(service->relation->termForm, parameters);
	FreeTypedTuple(parameters);
	PrintCString(" => ");
	PrintOperator(service->op);
}


static void btreePrintCallback(void const * item)
{
	PrintService((Service const *) item);
	PrintChar('\n');
}


void RelationDump(Relation const * relation)
{
	// find a service for enumerating all tuples
	byte parameterIO[relation->nColumns];
	for(index8 i = 0; i < relation->nColumns; i++)
		parameterIO[i] = PARAMETER_OUT;
	Operator const * op = ServiceRegistryFind(
		relation, CreateIOSignature(parameterIO, relation->nColumns));
	ASSERT(op);

	PrintF("Relation %u columns\n", relation->nColumns);

	Atom arguments[relation->nColumns];
	OperatorContext * context = OperatorCreateContext(op, arguments);
	size32 nTuples = 0;
	while(OperatorCall(context)) {
		// TODO: we should probably not print the full representaiton
		// of identified atoms, as it triggers repeated queries
		PrintTuple(relation->typeSignature.atomTypes, arguments, relation->nColumns);
		PrintChar('\n');
		nTuples++;
	}
	OperatorFreeContext(context);
	PrintF("%u tuples\n", nTuples);
}


void ServiceRegistryDump(void)
{
	BTreeTraversal(services, &btreePrintCallback);
}

