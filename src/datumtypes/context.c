
#include "datumtypes/context.h"
#include "lang/Formula.h"
#include "kernel/list.h"
#include "memory/allocator.h"
#include "vm/bytecode.h"


size32 ContextSize(size8 arity, size8 nRegisters)
{
	return sizeof(VMContext) + (arity + nRegisters) * sizeof(Datum);
}


Datum * ContextArguments(VMContext * context)
{
	return (Datum *) (((byte *) context) + sizeof(VMContext));
}

Datum * ContextRegisters(VMContext * context)
{
	return (Datum *) (
		((byte *) context) + sizeof(VMContext) + context->arity * sizeof(Datum)
	);
}


static void copyListDatums(Atom list, Datum * datums)
{
	Datum * rp = datums;
	ListIterator iterator;
	ListIterate(list, &iterator);
	while(ListIteratorHasNext(&iterator)) {
		Atom a = ListIteratorGetElement(&iterator);
		*rp++ = a.datum;
		ListIteratorNext(&iterator);
	}
	ListIteratorEnd(&iterator);
}


/**
 * Create a new execution context for the given bytecode program on the top of the stack. 
 * The program arguments must have been pushed prior to calling this function.
 * The child context contains pointers to the bytecode program
 * and a working copy of the registers used.
 */
VMContext * CreateContext(Atom bytecode, VMContext * parentContext)
{
	// NOTE: Should contexts acquire the bytecode program?
	// I think not, since the lifetime of a context is only
	// the execution of the bytecode block.
	Atom registersList = BytecodeGetRegisters(bytecode);
	size8 arity = FormulaArity(BytecodeGetSignature(bytecode));
	size8 nRegisters = ListLength(registersList);
	size32 contextSize = ContextSize(arity, nRegisters);

	VMContext * context = Allocate(contextSize);
	SetMemory(context, contextSize, 0);

	context->parentContext = parentContext;
	context->bytecode = bytecode;
	context->arity = arity;
	context->nRegisters = nRegisters;

	context->program = BytecodeGetProgram(bytecode);
	context->programLength = ListLength(context->program);
	context->programCounter = 1;

	// Copy registers (initial values).
	// NOTE: this will be more efficient if we use an array-based list relation
	// where datums and types are separated
	copyListDatums(registersList, ContextRegisters(context));

	return context;
}

void FreeChildContexts(VMContext * context)
{
	// This requires knowing the register's datum type, so we need
	// to retrieve the register list from the bytecode again ...
	Atom registersList = BytecodeGetRegisters(context->bytecode);
	Datum * rp = ContextRegisters(context);
	
	ListIterator iterator;
	ListIterate(registersList, &iterator);
	while(ListIteratorHasNext(&iterator)) {
		Atom _register = ListIteratorGetElement(&iterator);
		if(_register.type == DT_CONTEXT && *rp)
			FreeContext((VMContext *) *rp);
		ListIteratorNext(&iterator);
		*rp++ = 0;
	}
	ListIteratorEnd(&iterator);
}


void FreeContext(VMContext * context)
{
	FreeChildContexts(context);
	Free(context);
}
