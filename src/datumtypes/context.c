
#include "datumtypes/context.h"
#include "lang/Formula.h"
#include "kernel/list.h"
#include "memory/allocator.h"
#include "vm/bytecode.h"


size32 ContextSize(size8 nRegisters)
{
	return sizeof(VMContext) + nRegisters * sizeof(Datum);
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
	Atom registersList = BytecodeGetRegisters(bytecode);
	size8 nRegisters = ListLength(registersList);

	VMContext * context = Allocate(ContextSize(nRegisters));
	SetMemory(context, ContextSize(nRegisters), 0);

	context->parentContext = parentContext;

	context->bytecode = bytecode;
	context->arity = FormulaArity(BytecodeGetSignature(bytecode));

	context->program = BytecodeGetProgram(bytecode);
	context->programLength = ListLength(context->program);
	context->programCounter = 1;

	// Copy registers (initial values).
	// NOTE: this will be more efficient if we use an array-based list relation
	// where datums and types are separated
	copyListDatums(registersList, ContextRegisters(context));

	return context;
}
