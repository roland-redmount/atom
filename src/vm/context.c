
#include "lang/Form.h"
#include "lang/Formula.h"
#include "kernel/list.h"
#include "memory/allocator.h"
#include "vm/bytecode.h"
#include "vm/context.h"


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


typedef struct s_BytecodeContext BytecodeContext;

struct s_BytecodeContext {
	BytecodeContext * parentContext;
	Atom bytecode;
	Atom program;				// list of instructions
	size32 programLength;
	index32 programCounter;
	size8 nRegisters;
	Atom * registers;
	Atom * constants;
};

typedef struct s_CompiledContext {
	BTree * btree;
	TypedAtom * typedArguments;
	RelationBTreeIterator iterator;
	bool iterating;
} CompiledContext;


/**
 * A generic context, with bytecode and compiled variants
*/
enum ContextType {
	BYTECODE_CONTEXT = 1,
	COMPILED_CONTEXT = 2
};

typedef struct s_Context {
	enum ContextType type;
	size8 nArguments;
	Atom * arguments;
	union {
		BytecodeContext bytecode;
		CompiledContext compiled;
	} variant;
} Context;



/**
 * Allocate context structure and initialize common fields
 */
static Context * createContext(ServiceRecord * service)
{
	Context * context = Allocate(sizeof(Context));
	SetMemory(context, sizeof(BytecodeContext), 0);
	if(service->type == SERVICE_BYTECODE)
		context->type = BYTECODE_CONTEXT;
	else
		context->type = COMPILED_CONTEXT;

	context->nArguments = FormArity(service->form);
	context->arguments = Allocate(context->nArguments * sizeof(Atom));
	return context;
}


Atom CreateBytecodeContext(ServiceRecord * service, Atom parentContext)
{
	ASSERT(service->type == SERVICE_BYTECODE)

	Context * context = createContext(service);
	BytecodeContext * bytecodeContext = &(context->variant.bytecode);

	// BytecodeContextFreeChildContexts() needs access to the bytecode block
	bytecodeContext->bytecode = service->provider.bytecode;
	bytecodeContext->parentContext = (BytecodeContext *) parentContext;

	// Copy registers (initial values).
	Atom registersList = BytecodeGetRegisters(bytecodeContext->bytecode);
	bytecodeContext->nRegisters = ListLength(registersList);
	bytecodeContext->registers = Allocate(bytecodeContext->nRegisters * sizeof(Atom));
	copyListDatums(registersList, bytecodeContext->registers);

	// Program length and counter
	bytecodeContext->program = BytecodeGetProgram(bytecodeContext->bytecode);
	bytecodeContext->programLength = ListLength(bytecodeContext->program);
	// 1-based program counter, marking the last instruction retrieved;
	// initialize to zero to indicate no instruction retrieved
	bytecodeContext->programCounter = 0;

	// Copy constants. Not strictly necessary, but allows faster access
	Atom constantsList = BytecodeGetConstants(bytecodeContext->bytecode);
	size32 nConstants = ListLength(constantsList);
	bytecodeContext->constants = Allocate(nConstants * sizeof(Atom));
	copyListDatums(constantsList, bytecodeContext->constants);
	
	return (Atom) context;
}


Atom CreateCompiledContext(ServiceRecord * service)
{
	// for now, we only support B-tree services
	ASSERT(service->type == SERVICE_BTREE)

	Context * context = createContext(service);
	CompiledContext * compiledContext = &(context->variant.compiled);
	
	compiledContext->btree = service->provider.tree;
	
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


bool CompiledContextCall(Atom context)
{
	Context * _context = (Context *) context;
	ASSERT(_context->type == COMPILED_CONTEXT)
	CompiledContext * compiledContext = &(_context->variant.compiled);

	if(!compiledContext->iterating) {
		// first call, begin new iteration

		// copy arguments to typed arguments array
		// NOTE: this could be avoided with a layout where all Atoms are adjacent
		for(index8 i = 0; i < _context->nArguments; i++)
			compiledContext->typedArguments[i].atom = _context->arguments[i];

		RelationBTreeIterate(
			compiledContext->btree,
			compiledContext->typedArguments,
			&(compiledContext->iterator)
		);
		compiledContext->iterating = true;
	}
	if(RelationBTreeIteratorHasTuple(&(compiledContext->iterator))) {
		RelationBTreeIteratorGetTuple(
			&(compiledContext->iterator), compiledContext->typedArguments);

		for(index8 i = 0; i < _context->nArguments; i++)
			_context->arguments[i] = compiledContext->typedArguments[i].atom;
		return true;
	}
	else {
		RelationBTreeIteratorEnd(&(compiledContext->iterator));
		return false;
	}
}


Atom BytecodeContextGetParent(Atom context)
{
	Context * _context = (Context *) context;
	ASSERT(_context->type == BYTECODE_CONTEXT)
	return (Atom) _context->variant.bytecode.parentContext;
}


void BytecodeContextSetParent(Atom context, Atom parentContext)
{
	Context * _context = (Context *) context;
	ASSERT(_context->type == BYTECODE_CONTEXT)
	_context->variant.bytecode.parentContext = (BytecodeContext *) parentContext;
}


Atom ContextGetParameter(Atom context, index8 i)
{
	Context * _context = (Context *) context;
	return _context->arguments[i];
}


void ContextSetParameter(Atom context, index8 i, Atom argument)
{
	Context * _context = (Context *) context;
	_context->arguments[i] = argument;
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
	Context * _context = (Context *) context;
	ASSERT(_context->type == BYTECODE_CONTEXT)
	BytecodeContext * bytecodeContext = &(_context->variant.bytecode);
	if(bytecodeContext->programCounter < bytecodeContext->programLength) {
		bytecodeContext->programCounter++;
		*instruction = ListGetElement(bytecodeContext->program, bytecodeContext->programCounter).atom;
		return true;
	}
	else
		return false;
}


static void fetchOperand(
	Instruction inst, Operand operand, index8 * opIndex, byte * accessMode, index8 * contextIndex)
{
	switch(operand) {
		case OPERAND_LEFT:
		*opIndex = inst.fields.op1Index;
		*accessMode = inst.fields.accessMode.op1;
		*contextIndex = inst.fields.op1ContextRegister;
		break;
	
		case OPERAND_RIGHT:
		*opIndex = inst.fields.op2Index;
		*accessMode = inst.fields.accessMode.op2;
		*contextIndex = inst.fields.op2ContextRegister;
		break;
	}
}


// this only applies to a bytecode context
Atom ContextReadOperand(Atom context, Instruction inst, Operand operand)
{
	Context * _context = (Context *) context;
	ASSERT(_context->type == BYTECODE_CONTEXT)
	BytecodeContext * bytecodeContext = &(_context->variant.bytecode);
	
	index8 opIndex;
	byte accessMode;
	index8 contextIndex;
	fetchOperand(inst, operand, &opIndex, &accessMode, &contextIndex);

	switch(accessMode) {
	case ACCESS_PARAMETER: {
		// parameters may be read from a context in a register
		Context * operandContext = contextIndex ?
			(Context *) bytecodeContext->registers[contextIndex - 1] :
			_context;
		return operandContext->arguments[opIndex - 1];
	}
	
	case ACCESS_REGISTER:
		return bytecodeContext->registers[opIndex - 1];

	case ACCESS_CONSTANT:
		return bytecodeContext->constants[opIndex - 1];

	default:
		ASSERT(false);
		return 0;
	}
}


void ContextWriteOperand(Atom context, Instruction inst, index8 operand, Atom atom)
{
	Context * _context = (Context *) context;
	ASSERT(_context->type == BYTECODE_CONTEXT)
	BytecodeContext * bytecodeContext = &(_context->variant.bytecode);
	
	index8 opIndex;
	byte accessMode;
	index8 contextIndex;
	fetchOperand(inst, operand, &opIndex, &accessMode, &contextIndex);

	switch(accessMode) {
	case ACCESS_PARAMETER: {
		// parameters may be written to specific contexts
		Context * operandContext = contextIndex ?
			(Context *) bytecodeContext->registers[contextIndex - 1] :
			_context;
		operandContext->arguments[opIndex - 1] = atom;
		break;
	}
	
	case ACCESS_REGISTER:
		bytecodeContext->registers[opIndex - 1] = atom;
		break;

	case ACCESS_CONSTANT:
		ASSERT(false);

	default:
		ASSERT(false);
	}
}


void BytecodeContextFreeChildContexts(Atom context)
{
	Context * _context = (Context *) context;
	ASSERT(_context->type == BYTECODE_CONTEXT)
	BytecodeContext * bytecodeContext = &(_context->variant.bytecode);
	// To locate registers containing AT_CONTEXT atoms we need
	// to retrieve the register list from the bytecode again ...
	Atom registersList = BytecodeGetRegisters(bytecodeContext->bytecode);
	Atom * rp = bytecodeContext->registers;
	
	ListIterator iterator;
	ListIterate(registersList, &iterator);
	while(ListIteratorHasNext(&iterator)) {
		TypedAtom _register = ListIteratorGetElement(&iterator);
		if(_register.type == AT_BCONTEXT && *rp)
			FreeContext(*rp);
		ListIteratorNext(&iterator);
		*rp++ = 0;
	}
	ListIteratorEnd(&iterator);
}


void FreeContext(Atom context)
{
	Context * _context = (Context *) context;
	switch(_context->type) {
		case BYTECODE_CONTEXT: {
			BytecodeContext * bytecodeContext = &(_context->variant.bytecode);
			BytecodeContextFreeChildContexts(context);
			Free(bytecodeContext->registers);
			Free(bytecodeContext->constants);
			break;
		}

		case COMPILED_CONTEXT: {
			CompiledContext * compiledContext = &(_context->variant.compiled);
			Free(compiledContext->typedArguments);
			break;
		}
	}
	Free(_context->arguments);
	Free((BytecodeContext *) context);
}
