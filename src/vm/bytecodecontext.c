
#include "lang/Form.h"
#include "lang/Formula.h"
#include "kernel/list.h"
#include "memory/allocator.h"
#include "vm/bytecode.h"
#include "vm/bytecodecontext.h"


typedef struct s_BytecodeContext BytecodeContext;

struct s_BytecodeContext {
	BytecodeContext * parentContext;
	Atom bytecode;
	Atom program;				// list of instructions
	size32 programLength;
	index32 programCounter;
	size8 nArguments;
	size8 nRegisters;
	// these arrays follow the structure (variable size)
	// Atom arguments[nArguments]
	// Atom registers[nRegisters]
	// Atom constants[nConstants]
};


size32 BytecodeContextSize(size8 arity, size8 nRegisters)
{
	return sizeof(BytecodeContext) + (arity + nRegisters) * sizeof(Atom);
}


Atom * BytecodeContextArguments(Atom context)
{
	return (Atom *) (((byte *) context) + sizeof(BytecodeContext));
}


size8 BytecodeContextNArguments(Atom context)
{
	BytecodeContext * _context = (BytecodeContext *) context;
	return _context->nArguments;
}


Atom * BytecodeContextRegisters(Atom context)
{
	BytecodeContext * _context = (BytecodeContext *) context;
	return BytecodeContextArguments(context) + _context->nArguments;
}


Atom * BytecodeContextConstants(Atom context)
{
	BytecodeContext * _context = (BytecodeContext *) context;
	return BytecodeContextRegisters(context) + _context->nRegisters;
}


Atom CreateBytecodeContext(ServiceRecord * service, Atom parentContext)
{
	ASSERT(service->type == SERVICE_BYTECODE)

	// determine context size
	Atom bytecode = service->provider.bytecode;
	size8 arity = FormArity(service->form);
	Atom registersList = BytecodeGetRegisters(bytecode);
	size8 nRegisters = ListLength(registersList);
	size32 contextSize = BytecodeContextSize(arity, nRegisters);
	// allocate context
	BytecodeContext * context = Allocate(contextSize);
	SetMemory(context, contextSize, 0);
	context->bytecode = bytecode;	// needed by BytecodeContextFreeChildContexts()
	context->parentContext = (BytecodeContext *) parentContext;
	context->nArguments = arity;
	context->nRegisters = nRegisters;
	context->program = BytecodeGetProgram(bytecode);
	context->programLength = ListLength(context->program);
	// 1-based program counter, marking the last instruction retrieved;
	// initialize to zero to indicate no instruction retrieved
	context->programCounter = 0;

	// Copy registers (initial values).
	// NOTE: this will be more efficient if we use an array-based list relation
	// where atoms and types are separated
	copyListDatums(registersList, BytecodeContextRegisters((Atom) context));
	// Copy constants. Not strictly needed since they never change,
	// but allows faster access
	Atom constantsList = BytecodeGetConstants(bytecode);
	copyListDatums(constantsList, BytecodeContextConstants((Atom) context));
	return (Atom) context;
}


Atom BytecodeContextGetParent(Atom context)
{
	BytecodeContext * _context = (BytecodeContext *) context;
	return (Atom) _context->parentContext;
}


void BytecodeContextSetParent(Atom context, Atom parentContext)
{
	BytecodeContext * _context = (BytecodeContext *) context;
	_context->parentContext = (BytecodeContext *) parentContext;
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


/*  Reference handling -- not sure if this is a good idea,
    Only needed if context are ever references from more than
	one parent context, and I don't think that will happen.

void ContextAcquire(Atom context)
{
	BytecodeContext * _context = (BytecodeContext *) context;
	_context->nReferences++;
}


void ContextRelease(Atom context)
{
	BytecodeContext * _context = (BytecodeContext *) context;
	ASSERT(_context->nReferences > 0);
	_context->nReferences--;
	if(_context->nReferences == 0) {
		FreeChildContexts(context);
		Free((BytecodeContext *) context);
	}
}
*/

bool BytecodeContextNextInstruction(Atom context, Atom * instruction)
{
	BytecodeContext * _context = (BytecodeContext *) context;
	if(_context->programCounter < _context->programLength) {
		_context->programCounter++;
		*instruction = ListGetElement(_context->program, _context->programCounter).atom;
		return true;
	}
	else
		return false;
}


void BytecodeContextFreeChildContexts(Atom context)
{
	// This requires knowing the register's atom type, so we need
	// to retrieve the register list from the bytecode again ...
	BytecodeContext * _context = (BytecodeContext *) context;
	Atom registersList = BytecodeGetRegisters(_context->bytecode);
	Atom * rp = BytecodeContextRegisters(context);
	
	ListIterator iterator;
	ListIterate(registersList, &iterator);
	while(ListIteratorHasNext(&iterator)) {
		TypedAtom _register = ListIteratorGetElement(&iterator);
		if(_register.type == AT_BCONTEXT && *rp)
			FreeBytecodeContext(*rp);
		ListIteratorNext(&iterator);
		*rp++ = 0;
	}
	ListIteratorEnd(&iterator);
}


void FreeBytecodeContext(Atom context)
{
	BytecodeContextFreeChildContexts(context);
	Free((BytecodeContext *) context);
}
