
#include "datumtypes/context.h"
#include "lang/Form.h"
#include "lang/Formula.h"
#include "kernel/list.h"
#include "memory/allocator.h"
#include "vm/bytecode.h"


size32 ContextSize(size8 arity, size8 nRegisters)
{
	return sizeof(BytecodeContext) + (arity + nRegisters) * sizeof(Atom);
}


Atom * ContextArguments(BytecodeContext * context)
{
	return (Atom *) (((byte *) context) + sizeof(BytecodeContext));
}

Atom * ContextRegisters(BytecodeContext * context)
{
	return (Atom *) (
		((byte *) context) + sizeof(BytecodeContext) + context->arity * sizeof(Atom)
	);
}


static void copyListDatums(Atom list, Atom * atoms)
{
	Atom * rp = atoms;
	ListIterator iterator;
	ListIterate(list, &iterator);
	while(ListIteratorHasNext(&iterator)) {
		TypedAtom a = ListIteratorGetElement(&iterator);
		*rp++ = a.atom;
		ListIteratorNext(&iterator);
	}
	ListIteratorEnd(&iterator);
}


/**
 * Create a new execution context for the given bytecode service.
 * The program arguments must have been pushed prior to calling this function.
 * The child context contains pointers to the bytecode program
 * and a working copy of the registers used.
 */
BytecodeContext * CreateBytecodeContext(ServiceRecord * service, BytecodeContext * parentContext)
{
	ASSERT(service->type == SERVICE_BYTECODE)

	// determine context size
	Atom bytecode = service->provider.bytecode;
	size8 arity = FormArity(service->form);
	Atom registersList = BytecodeGetRegisters(bytecode);
	size8 nRegisters = ListLength(registersList);
	size32 contextSize = ContextSize(arity, nRegisters);
	// allocate context
	BytecodeContext * context = Allocate(contextSize);
	SetMemory(context, contextSize, 0);
	// set fields
	context->bytecode = bytecode;
	context->parentContext = parentContext;
	context->arity = arity;
	context->nRegisters = nRegisters;

	context->program = BytecodeGetProgram(bytecode);
	context->programLength = ListLength(context->program);
	context->programCounter = 1;

	// Copy registers (initial values).
	// NOTE: this will be more efficient if we use an array-based list relation
	// where atoms and types are separated
	copyListDatums(registersList, ContextRegisters(context));

	return context;
}

void FreeChildContexts(BytecodeContext * context)
{
	// This requires knowing the register's atom type, so we need
	// to retrieve the register list from the bytecode again ...
	Atom registersList = BytecodeGetRegisters(context->bytecode);
	Atom * rp = ContextRegisters(context);
	
	ListIterator iterator;
	ListIterate(registersList, &iterator);
	while(ListIteratorHasNext(&iterator)) {
		TypedAtom _register = ListIteratorGetElement(&iterator);
		if(_register.type == AT_CONTEXT && *rp)
			FreeContext((BytecodeContext *) *rp);
		ListIteratorNext(&iterator);
		*rp++ = 0;
	}
	ListIteratorEnd(&iterator);
}


void FreeContext(BytecodeContext * context)
{
	FreeChildContexts(context);
	Free(context);
}
