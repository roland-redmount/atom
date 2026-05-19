/**
 * A service computing a join (Carthesian product) between two other services,
 * 
 *  result = left x right
 */

 #include "kernel/service.h"


typedef struct s_JoinService {
	// Mapping between arguments of this service and
	// the parameters of the left and right services
	index8 * argumentMapLeft;
	index8 * argumentMapRight;
} JoinService;

struct {
	// Execution contexts for the left and right services
	void * left;
	void * right;
}


CompositeService CreateJoinService(ServiceRecord const * left, ServiceRecord const * right)
{
	// TODO;
	return 0;
}


/**
 * Stubs for using a B-tree as a service
 */
static void * serviceSetupContext(MachineService * service, Tuple const * arguments)
{
	BTree * btree = (BTree *) service->serviceParameter;
	RelationBTreeIterator * iterator = Allocate(sizeof(RelationBTreeIterator));
	RelationBTreeIterate(btree, arguments, iterator);
	return iterator;
}


static bool serviceCall(void * context, Tuple * result)
{
	RelationBTreeIterator * iterator = context;
	bool hasTuple = RelationBTreeIteratorNext(iterator);
	if(hasTuple) {
		Tuple const * tuple = RelationBTreeIteratorPeekTuple(iterator);
		CopyTuples(tuple, result);
	}
	return hasTuple;
}


static void serviceFinalizeContext(void * context)
{
	RelationBTreeIterator * iterator = context;
	RelationBTreeIteratorEnd(iterator);
	Free(iterator);
}


void serviceAddTuple(MachineService * service, Tuple const * arguments)
{
	RelationBTreeAddTuple((BTree *) service->serviceParameter, arguments);
}


void serviceRemoveTuples(MachineService * service, Tuple const * arguments)
{
	RelationBTreeRemoveTuples((BTree *) service->serviceParameter, arguments, REMOVE_NORMAL);
}


bool serviceIsEmpty(MachineService const * service)
{
	return BTreeNItems((BTree *) service->serviceParameter) == 0;
}


void serviceTeardown(MachineService * service)
{
	FreeRelationBTree((BTree *) service->serviceParameter);
}


// naming: create record?
MachineService RelationBTreeCreateRecord(BTree * btree)
{
	return (MachineService) {
		.serviceParameter = (data64) btree,
		.setupContext = &serviceSetupContext,
		.call = &serviceCall,
		.finalizeContext = &serviceFinalizeContext,
		.addTuple = &serviceAddTuple,
		.removeTuples = &serviceRemoveTuples,
		.isEmpty = &serviceIsEmpty,
		.teardown = &serviceTeardown,
	};
}