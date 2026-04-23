
#include "lang/Form.h"
#include "lang/Formula.h"
#include "kernel/list.h"
#include "memory/allocator.h"
#include "vm/bytecode.h"
#include "vm/context.h"


static void copyListDatums(Atom list, Tuple * tuple)
{
	ListIterator iterator;
	ListIterate(list, &iterator);
	index8 index = 0;
	while(ListIteratorHasNext(&iterator)) {
		TypedAtom typedAtom = ListIteratorGetElement(&iterator);
		TupleSetElement(tuple, index, typedAtom);
		ListIteratorNext(&iterator);
		index++;
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
	Tuple * registers;
	Tuple * constants;
};

typedef struct s_CompiledContext {
	BTree * btree;
	RelationBTreeIterator iterator;
} CompiledContext;


/**
 * A generic context, with bytecode and compiled variants
*/

typedef struct s_Context {
	enum ContextType type;
	Tuple * arguments;
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
	size8 nArguments = FormArity(service->form);
	context->arguments = CreateTuple(nArguments);

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
	size8 nRegisters = ListLength(registersList);
	bytecodeContext->registers = CreateTuple(nRegisters);
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
	bytecodeContext->constants = CreateTuple(nConstants);
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

	ASSERT(false)
	return 0;
}


enum ContextType ContextGetType(Atom context)
{
	Context * _context = (Context *) context;
	return _context->type;
}


bool CompiledContextCall(Atom context)
{
	Context * _context = (Context *) context;
	ASSERT(_context->type == COMPILED_CONTEXT)
	CompiledContext * compiledContext = &(_context->variant.compiled);

	// TODO: we must ensure that the results tuple matches
	// the atom type of the operand that they are copied to.
	// This involves (1) filtering out any  

	if(!compiledContext->iterator.btree) {
		// first call, begin new iteration
		RelationBTreeIterate(
			compiledContext->btree,
			_context->arguments,
			&(compiledContext->iterator)
		);
	}
	if(RelationBTreeIteratorHasTuple(&(compiledContext->iterator))) {
		RelationBTreeIteratorGetTuple(
			&(compiledContext->iterator), _context->arguments);
		return true;
	}
	else {
		// this zeroes the iterator structure
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
	return TupleGetAtom(_context->arguments, i);
}


void ContextSetParameter(Atom context, index8 i, Atom argument)
{
	Context * _context = (Context *) context;
	TupleSetAtom(_context->arguments, i, argument);
}


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
		// parameters may be read from a bytecode context in a register
		Context * operandContext = contextIndex ?
			(Context *) TupleGetAtom(bytecodeContext->registers, contextIndex - 1) :
			_context;
		return TupleGetAtom(operandContext->arguments, opIndex - 1);
	}
	
	case ACCESS_REGISTER:
		return TupleGetAtom(bytecodeContext->registers, opIndex - 1);

	case ACCESS_CONSTANT:
		return TupleGetAtom(bytecodeContext->constants, opIndex - 1);

	default:
		ASSERT(false);
		return 0;
	}
}

TypedAtom ContextReadTypedOperand(Atom context, Instruction inst, Operand operand)
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
		Context * operandContext = contextIndex ?
			(Context *) TupleGetAtom(bytecodeContext->registers, contextIndex - 1) :
			_context;
		return TupleGetElement(operandContext->arguments, opIndex - 1);
	}
	case ACCESS_REGISTER:
		return TupleGetElement(bytecodeContext->registers, opIndex - 1);

	case ACCESS_CONSTANT:
		return TupleGetElement(bytecodeContext->constants, opIndex - 1);

	default:
		ASSERT(false);
		return (TypedAtom) {0};
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
		// parameters may be written to specific bytecode contexts
		Context * operandContext = contextIndex ?
			(Context *) TupleGetAtom(bytecodeContext->registers, contextIndex - 1) :
			_context;
		
		TupleSetAtom(operandContext->arguments, opIndex - 1, atom);
		break;
	}
	
	case ACCESS_REGISTER:
		TupleSetAtom(bytecodeContext->registers, opIndex - 1, atom);
		break;

	case ACCESS_CONSTANT:
		ASSERT(false);

	default:
		ASSERT(false);
	}
}


/**
 * Write to a tuple element but require the atom type to match
 */
static void tupleWriteChecked(Tuple * tuple, index8 index, TypedAtom typedAtom)
{
	TypedAtom previous = TupleGetElement(tuple, index);
	ASSERT(previous.type == typedAtom.type)
	TupleSetAtom(tuple, index, typedAtom.type);
}


void ContextWriteTypedOperand(Atom context, Instruction inst, Operand operand, TypedAtom typedAtom)
{
	Context * _context = (Context *) context;
	BytecodeContext * bytecodeContext = &(_context->variant.bytecode);
	
	index8 opIndex;
	byte accessMode;
	index8 contextIndex;
	fetchOperand(inst, operand, &opIndex, &accessMode, &contextIndex);

	switch(accessMode) {
	case ACCESS_PARAMETER: {
		if(contextIndex) {
			Context * operandContext = (Context *) TupleGetAtom(
				bytecodeContext->registers, contextIndex - 1);
			if(operandContext->type == COMPILED_CONTEXT)
				TupleSetElement(operandContext->arguments, opIndex - 1, typedAtom);
			else
				tupleWriteChecked(operandContext->arguments, opIndex - 1, typedAtom);
		}
		else
			tupleWriteChecked(_context->arguments, opIndex - 1, typedAtom);
		break;
	}
	case ACCESS_REGISTER:
		tupleWriteChecked(bytecodeContext->registers, opIndex - 1, typedAtom);
		break;

	case ACCESS_CONSTANT:
		ASSERT(false)
		break;

	default:
		ASSERT(false)
		break;
	}
}



void BytecodeContextFreeChildContexts(Atom context)
{
	Context * _context = (Context *) context;
	ASSERT(_context->type == BYTECODE_CONTEXT)

	BytecodeContext * bytecodeContext = &(_context->variant.bytecode);
	// locate registers containing AT_CONTEXT atoms
	for(index8 i = 0; i < bytecodeContext->registers->nAtoms; i++) {
		TypedAtom _register = TupleGetElement(bytecodeContext->registers, i);
		if((_register.type == AT_CONTEXT) && _register.atom)
			FreeContext(_register.atom);
	}
}


void FreeContext(Atom context)
{
	Context * _context = (Context *) context;
	switch(_context->type) {
		case BYTECODE_CONTEXT: {
			BytecodeContext * bytecodeContext = &(_context->variant.bytecode);
			BytecodeContextFreeChildContexts(context);
			FreeTuple(bytecodeContext->registers);
			FreeTuple(bytecodeContext->constants);
			break;
		}

		case COMPILED_CONTEXT: {
			CompiledContext * compiledContext = &(_context->variant.compiled);
			if(compiledContext->iterator.btree) {
				// iteration was not terminated
				RelationBTreeIteratorEnd(&(compiledContext->iterator));
			}
			break;
		}
	}
	FreeTuple(_context->arguments);
	Free((BytecodeContext *) context);
}
