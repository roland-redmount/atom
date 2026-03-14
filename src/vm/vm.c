
#include "kernel/list.h"
#include "lang/Formula.h"
#include "vm/bytecode.h"
#include "vm/vm.h"




/**
 * This structure describes a VM execution thread
 */
static struct {
	addr64 stackBottom;
	addr64 stackTop;
	size32 stackSize;
	bool trace;
	// Here we can add VM "registers" as necessary.
	// Or we just keep them on the VM stack
	bool flag;
} vm;


void VMInitialize(void * stack, size32 stackSize)
{
	// stack grows upward in memory space,
	// so stackTop is the most recent item
	vm.stackBottom = (addr64) stack;
	vm.stackTop = vm.stackBottom;
	vm.stackSize = stackSize;
	vm.trace = true;
}


void vmStackPush32(data32 data)
{
	*((data32 *) vm.stackTop) = data;
	vm.stackTop += 4;
}


void vmStackPush64(data64 data)
{
	*((data64 *) vm.stackTop) = data;
	vm.stackTop += 8;
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


void vmStackPushRegisters(Datum bytecode)
{
	Atom registersList = BytecodeGetRegisters((Atom) {.type = DT_ID, .datum = bytecode});
	size32 nRegisters = ListLength(registersList);
	copyListDatums(registersList, (void *) vm.stackTop);
	vm.stackTop += nRegisters * sizeof(Datum);
}


/**
 * VM execution context structure
 */
typedef struct s_VMContext VMContext;

struct s_VMContext {
	// array of arguments precede this structure on the stack
	VMContext * parentContext;
	index32 parentProgramCounter;	// the instruction to return to after the call
	Atom bytecode;
	Atom program;					// list of instructions
	size32 programLength;
	index32 programCounter;
	size8 nChildContexts;			// determined at compile time
	index8 activeChildIndex;		// > 0 if a child context becomes active
	size8 arity;
	size8 nRegisters;
	size8 reserved1;
	size8 reserved2;
	Datum registers[];
	// child context pointers follow the registers
}; //__attribute__((packed))


/**
 * Context size, including registers and child context pointers,
 * but excluding actors
 */
static size32 contextSize(size8 nRegisters, size8 nChildContexts)
{
	return sizeof(VMContext) + nRegisters * sizeof(Datum) + nChildContexts * sizeof(VMContext *);
}


/**
 * Return argument at the given index (1-based)
 */
static Datum * contextGetArgument(VMContext * context, index32 index)
{
	return ((Datum *) (((addr64) context) - index * sizeof(Datum)));
}

static VMContext ** childContexts(VMContext * context)
{
	return (VMContext **) (
		((addr64) context) + sizeof(VMContext) + context->nRegisters * sizeof(Datum)
	);
}

/**
 * Return context for a given childe (1-based number)
 */
static VMContext * getChildContext(VMContext * context, index8 childNumber)
{
	ASSERT(childNumber > 0 && childNumber <= context->nChildContexts)
	VMContext ** children = childContexts(context);
	return children[childNumber - 1];
}

/**
 * Set context for a given childe (1-based number)
 */
static void setChildContext(VMContext * context, index8 childNumber, VMContext * childContext)
{
	ASSERT(childNumber > 0 && childNumber <= context->nChildContexts)
	VMContext ** children = childContexts(context);
	children[childNumber - 1] = childContext;
}


/**
 * Create a new execution context for the given bytecode program on the top of the stack. 
 * The program arguments must have been pushed prior to calling this function.
 * The child context contains pointers to the bytecode program
 * and a working copy of the registers used.
 */
static VMContext * createContext(Atom bytecode, VMContext * parentContext)
{
	VMContext * context = (VMContext *) vm.stackTop;
	SetMemory(context, sizeof(VMContext), 0);

	context->parentContext = parentContext;
//	context->parentProgramCounter = parentContext->programCounter;

	context->bytecode = bytecode;
	context->arity = FormulaArity(BytecodeGetSignature(bytecode));

	context->program = BytecodeGetProgram(bytecode);
	context->programLength = ListLength(context->program);
	context->programCounter = 1;

	context->nChildContexts = BytecodeNChildContexts(bytecode);

	// Copy registers (initial values).
	// NOTE: this will be more efficient if we use an array-based list relation
	// where datums and types are separated
	Atom registersList = BytecodeGetRegisters(bytecode);
	context->nRegisters = ListLength(registersList);
	copyListDatums(registersList, context->registers);

	vm.stackTop += contextSize(context->nRegisters, context->nChildContexts);
	return context;
}


// TODO: this is very inefficient
static Datum accessConstant(Atom bytecode, index8 op)
{
	Atom constantsList = BytecodeGetConstants(bytecode);
	return ListGetElement(constantsList, op).datum;
}


typedef enum { OPERAND_LEFT, OPERAND_RIGHT } Operand;


static Datum readOperand(VMContext * context, Instruction inst, Operand operand)
{
	index8 opIndex;
	byte accessMode;
	switch(operand) {
		case OPERAND_LEFT:
		 opIndex = inst.fields.op1;
		 accessMode = inst.fields.accessMode.op1;
		 break;
	
		case OPERAND_RIGHT:
		opIndex = inst.fields.op2;
		accessMode = inst.fields.accessMode.op2;
		break;
	}

	switch(accessMode) {
	case ACCESS_ARGUMENT:
		return *contextGetArgument(context, opIndex);
	
	case ACCESS_REGISTER:
		return context->registers[opIndex - 1];

	case ACCESS_CONSTANT:
		return accessConstant(context->bytecode, opIndex);

	default:
		ASSERT(false);
		return 0;
	}
}


void writeOperand(VMContext * context, Instruction inst, index8 operand, Datum datum)
{
	index8 opIndex;
	byte accessMode;
	switch(operand) {
		case OPERAND_LEFT:
		 opIndex = inst.fields.op1;
		 accessMode = inst.fields.accessMode.op1;
		 break;
	
		case OPERAND_RIGHT:
		opIndex = inst.fields.op2;
		accessMode = inst.fields.accessMode.op2;
		break;
	}

	switch(accessMode) {
	case ACCESS_ARGUMENT:
		*contextGetArgument(context, opIndex) = datum;
		break;
	
	case ACCESS_REGISTER:
		context->registers[opIndex - 1] = datum;
		break;

	case ACCESS_CONSTANT:
		ASSERT(false);

	default:
		ASSERT(false);
	}
}


void VMStart(Atom bytecode, Datum ** arguments)
{
	/**
	 * Stack layout at the start of bytecode service execution:
	 * (from top to bottom)
	 *   
	 *   Datum register_N
	 *   ...
	 *   Datum register_1
	 *   Datum bytecode
	 *   Datum * argument_N
	 *   ..
	 *   Datum * argument_1       <-- stackBottom
	 */

	vm.stackTop = vm.stackBottom;
	// push arguments in reverse order
	size8 arity = FormulaArity(BytecodeGetSignature(bytecode));
	for(index8 i = 0; i < arity; i++)
		vmStackPush64(*arguments[arity - i - 1]);
	
	// create the root context
 	VMContext * context = createContext(bytecode, 0);

	// Iterate through program

iterate:
	while(true) {
		Instruction inst;
		if(context->programCounter <= context->programLength) {
			Atom instruction = ListGetElement(context->program, context->programCounter);
			if(vm.trace)
				 PrintInstruction(instruction);
			inst = InstructionGetData(instruction);
		}
		else {
			// Reached end of program, implicit END instruction
			inst.fields.opcode = OP_END;
		}

		Datum left, right;
		switch(inst.fields.opcode) {
		case OP_COPY:
			left = readOperand(context, inst, OPERAND_LEFT);
			writeOperand(context, inst, OPERAND_RIGHT, left);
			break;

		case OP_PUSH:
			// push a datum on the stack
			left = readOperand(context, inst, OPERAND_LEFT);
			vmStackPush64(left);
			break;

		case OP_ADD:
			left = readOperand(context, inst, OPERAND_LEFT);
			right = readOperand(context, inst, OPERAND_RIGHT);
			// TODO: INT vs UINT? Overflow?
			right = ((uint64) left) + ((uint64) right);
			writeOperand(context, inst, OPERAND_RIGHT, right);
			break;

		case OP_SUB:
			left = readOperand(context, inst, OPERAND_LEFT);
			right = readOperand(context, inst, OPERAND_RIGHT);
			// TODO: INT vs UINT? Overflow?
			right = ((uint64) right) - ((uint64) left);
			writeOperand(context, inst, OPERAND_RIGHT, right);
			break;

		case OP_INC:
			left = readOperand(context, inst, OPERAND_LEFT);
			writeOperand(context, inst, OPERAND_LEFT, ((uint64) left) + 1);
			break;

		case OP_MUL:
			left = readOperand(context, inst, OPERAND_LEFT);
			right = readOperand(context, inst, OPERAND_RIGHT);
			right = ((uint64) left) * ((uint64) right);
			writeOperand(context, inst, OPERAND_RIGHT, right);
			break;
		
		case OP_CALL: {
			/** 
			 * CALL <service> <childIndex>
			 * 
			 * Create a "child" context and invoke a bytecode service.
			 * Arguments must be PUSHed on the stack before this instruction
			 * in reverse order (similar to __stdcall), so that argument k
			 * can be accesssed as *(context - k * sizeof(Datum))
			 * First push the current program counter, then registers
			 * according to the bytecode information.
			 * 
			 * Stack layout after this instruction
			 *   
			 *   VMContext * child_M     |
			 *   ...                     |
			 *   VMContext * child_1     |
			 *   size32 nChildren        |
			 *   Datum register_N        |
			 *   ...                     | VMContext structure
			 *   Datum register_1        | pushed by CALL
			 *   data32 reserved         |
			 *   index32 pc              |
			 *   Datum bytecode          |
			 *   VMContext * parent    --|--------------------+
			 *                           <-- newContext       |
			 *   Datum * argument_1      |                    |
			 *   ..                      | Previous PUSH      |
			 *   Datum * argument_N      | instructions       |
			 *                                                |
			 *   top of caller's context  <-------------------+ 
			 *   
			 * The created stack frame remains in place after a YIELD,
			 * until the service exits.
			 */
			
			// setup the new child context at the top of the stack
			Atom childBytecode = (Atom) {.type = DT_ID, .datum = readOperand(context, inst, OPERAND_LEFT)};
			VMContext * childContext = createContext(childBytecode, context);
			context->activeChildIndex = inst.fields.op2;
			setChildContext(context, context->activeChildIndex, childContext);
			
			// we will return from the service at the next instruction
			context->programCounter++;
			// give control to the child context and continue
			context = childContext;
			goto iterate;
		}

		case OP_RESUME: {
			// For now, we're using the op1 field as a context index.
			// When creating bytecode, we can fill in this index by
			// keeping a count of CALL instructions processed.
			context->activeChildIndex = inst.fields.op1;
			VMContext * childContext = getChildContext(context, context->activeChildIndex);

			// we will resume at the instruction following the resumed CALL instruction
			// TODO: if the child terminates, proceed to the next instruction.
			// We could have a VM flag indicating whether the previous CALL terminated
			context = childContext;
			goto iterate;
		}

		case OP_YIELD: {
			// NOTE: of we pass arguments by value, we have to copy outputs back here.
			// We then need to know which ones are outputs ... 
			vm.flag = true;
			// we will resume at the instruction following this one
			context->programCounter++;
			// continue parent context execution where we left off
			VMContext * childContext = context;
			context = context->parentContext;
			context->programCounter = childContext->parentProgramCounter;
			context->activeChildIndex = 0;
			goto iterate;
		}

		case OP_END: {
			vm.flag = false;
			context->programCounter++;
			if(context->parentContext) {
				// continue parent context execution where we left off
				VMContext * childContext = context;
				context = context->parentContext;
				context->programCounter = childContext->parentProgramCounter;
				// Clear the context (is this necessary?)
				setChildContext(context, context->activeChildIndex, 0);
				
				// TODO: What to do with the stack?
				// We cannot just reset it, as there may have been other
				// allocations ahead of our context made by our caller,
				// or its descendants. It seems we need heap allocation ...
			}
			else {
				// end root context execution
				// reset the stack pointer
				vm.stackTop = vm.stackBottom;
				// Copy arguments back to the given datum array
				Datum * stackArgs = (Datum *) vm.stackTop;
				for(index8 i = 0; i < arity; i++)
					*arguments[arity - i - 1] = stackArgs[i];
			}

			goto iterate;
		}


		default:
			// instruction not implemented yet
			ASSERT(false)
			break;
		}
		context->programCounter++;
	}


	
}
