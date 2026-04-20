#include "kernel/RelationBTree.h"
#include "memory/allocator.h"
#include "vm/ccontext.h"

typedef struct s_CContext {
	size8 nArguments;
	BTree * btree;
	RelationBTreeIterator btreeIterator;
	// these arrays follow the structure (variable size)
	// TypedAtom typedArguments[nArguments]
} CContext;



static size32 cContextSize(size8 nArguments)
{
	return sizeof(CContext) + nArguments * sizeof(TypedAtom);
}


TypedAtom * CContextArguments(Atom cContext)
{
	CContext * _cContext = (CContext *) cContext;
	return (TypedAtom *) (_cContext + 1);
}


Atom CreateCContext(ServiceRecord * service)
{
	// for now, we only support B-tree services
	ASSERT(service->type == SERVICE_BTREE)
	// create structure
	size8 nArguments = RelationBTreeNColumns(service->provider.tree);
	size32 size = cContextSize(nArguments);
	CContext * ccontext = Allocate(size);
	SetMemory(ccontext, size, 0);
	ccontext->btree = service->provider.tree;
	
	// TODO: arguments should be set by copy operations,
	// but how do we find the atom type? RelationBTreeIterator
	// requires typed arguments, but bytecode is not typed.
	// I think the types here must be inferred at compile time
	// and stored in ... a typed COPY instruction? 
	// A separate SETTYPE instruction?
	// The types are specific to the CCTX / CCALL instructions,
	// not to the service record (it is untyped).

	ASSERT(false)
	return 0;
}


void CContextCall(Atom cContext)
{
	CContext * _cContext = (CContext *) cContext;
	
	RelationBTreeIterate(
		_cContext->btree,
		CContextArguments(cContext),
		&(_cContext->btreeIterator)
	);
}
