
#include "datumtypes/context.h"
#include "lang/Form.h"
#include "lang/Formula.h"
#include "kernel/list.h"
#include "memory/allocator.h"
#include "vm/bytecode.h"


typedef struct s_BytecodeContext BytecodeContext;

struct s_BytecodeContext {
	uint32 nReferences;
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


size32 ContextSize(size8 arity, size8 nRegisters)
{
	return sizeof(BytecodeContext) + (arity + nRegisters) * sizeof(Atom);
}


Atom * ContextArguments(Atom context)
{
	return (Atom *) (((byte *) context) + sizeof(BytecodeContext));
}


size8 ContextNArguments(Atom context)
{
	BytecodeContext * _context = (BytecodeContext *) context;
	return _context->nArguments;
}


Atom * ContextRegisters(Atom context)
{
	BytecodeContext * _context = (BytecodeContext *) context;
	return ContextArguments(context) + _context->nArguments;
}


Atom * ContextConstants(Atom context)
{
	BytecodeContext * _context = (BytecodeContext *) context;
	return ContextRegisters(context) + _context->nRegisters;
}


Atom ContextGetParent(Atom context)
{
	BytecodeContext * _context = (BytecodeContext *) context;
	return (Atom) _context->parentContext;
}


void ContextSetParent(Atom context, Atom parentContext)
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


Atom CreateBytecodeContext(ServiceRecord * service, Atom parentContext)
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
	context->bytecode = bytecode;	// needed by FreeChildContexts()
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
	copyListDatums(registersList, ContextRegisters((Atom) context));
	// Copy constants. Not strictly needed since they never change,
	// but allows faster access
	Atom constantsList = BytecodeGetConstants(bytecode);
	copyListDatums(constantsList, ContextConstants((Atom) context));
	return (Atom) context;
}


bool ContextNextInstruction(Atom context, Atom * instruction)
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


void FreeChildContexts(Atom context)
{
	// This requires knowing the register's atom type, so we need
	// to retrieve the register list from the bytecode again ...
	BytecodeContext * _context = (BytecodeContext *) context;
	Atom registersList = BytecodeGetRegisters(_context->bytecode);
	Atom * rp = ContextRegisters(context);
	
	ListIterator iterator;
	ListIterate(registersList, &iterator);
	while(ListIteratorHasNext(&iterator)) {
		TypedAtom _register = ListIteratorGetElement(&iterator);
		if(_register.type == AT_CONTEXT && *rp)
			FreeContext(*rp);
		ListIteratorNext(&iterator);
		*rp++ = 0;
	}
	ListIteratorEnd(&iterator);
}


void FreeContext(Atom context)
{
	FreeChildContexts(context);
	Free((BytecodeContext *) context);
}
