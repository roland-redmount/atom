
#include "kernel/list.h"
#include "lang/Formula.h"
#include "vm/bytecode.h"
#include "vm/vm.h"




/**
 * This structure describes a VM execution thread
 */
static struct {
	bool trace;
	// Here we can add VM "registers" as necessary.
	// Or we just keep them on the VM stack
	bool flag;
	index8 argIndex;	// used by ARG and EXEC instructions
} vm;


void VMInitialize(void * stack, size32 stackSize)
{
	vm.trace = true;
}


// TODO: this is very inefficient
static Datum accessConstant(Atom bytecode, index8 opIndex)
{
	Atom constantsList = BytecodeGetConstants(bytecode);
	return ListGetElement(constantsList, opIndex).datum;
}


static Datum readOperand(VMContext * context, Instruction inst, Operand operand)
{
	index8 opIndex;
	byte accessMode;
	index8 contextIndex;
	switch(operand) {
		case OPERAND_LEFT:
		opIndex = inst.fields.op1Index;
		accessMode = inst.fields.accessMode.op1;
		contextIndex = inst.fields.op1ContextRegister;
		break;
	
		case OPERAND_RIGHT:
		opIndex = inst.fields.op2Index;
		accessMode = inst.fields.accessMode.op2;
		contextIndex = inst.fields.op2ContextRegister;
		break;
	}

	switch(accessMode) {
	case ACCESS_PARAMETER: {
		// parameters may be read from specific contexts
		VMContext * operandContext = contextIndex ?
			(VMContext *) ContextRegisters(context)[contextIndex - 1] :
			context;
		return ContextArguments(operandContext)[opIndex - 1];
	}
	
	case ACCESS_REGISTER:
		return ContextRegisters(context)[opIndex - 1];

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
	index8 contextIndex;
	
	switch(operand) {
		case OPERAND_LEFT:
		opIndex = inst.fields.op1Index;
		accessMode = inst.fields.accessMode.op1;
		contextIndex = inst.fields.op1ContextRegister;
		break;
	
		case OPERAND_RIGHT:
		opIndex = inst.fields.op2Index;
		accessMode = inst.fields.accessMode.op2;
		contextIndex = inst.fields.op2ContextRegister;
		break;
	}

	switch(accessMode) {
	case ACCESS_PARAMETER: {
		// parameters may be written to specific contexts
		VMContext * operandContext = contextIndex ?
			(VMContext *) ContextRegisters(context)[contextIndex - 1] :
			context;
		ContextArguments(operandContext)[opIndex - 1] = datum;
		break;
	}
	
	case ACCESS_REGISTER:
		ContextRegisters(context)[opIndex - 1] = datum;
		break;

	case ACCESS_CONSTANT:
		ASSERT(false);

	default:
		ASSERT(false);
	}
}


VMContext * VMCreateRootContext(Atom bytecode, Datum * arguments)
{
 	VMContext * context = CreateContext(bytecode, 0);

	// copy arguments to context
	size8 arity = FormulaArity(BytecodeGetSignature(bytecode));
	for(index8 i = 0; i < arity; i++)
		ContextArguments(context)[i] = arguments[i];

	return context;
}


void VMExecute(VMContext * context)
{
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
		
		case OP_CTX: {
			/** 
			 * EXEC <service> <operand>
			 * Create a new context and store in the destination operand.
			 */
			Atom newBytecode = (Atom) {.type = DT_ID, .datum = readOperand(context, inst, OPERAND_LEFT)};
			VMContext * newContext = CreateContext(newBytecode, context);
			writeOperand(context, inst, OPERAND_RIGHT, (Datum) newContext);
			break;
		}

		case OP_CALL: {
			// RESUME <context>
			// Give control to another execution context.
			// Currently, contexts must be stored in registers.
			ASSERT(inst.fields.accessMode.op1 == ACCESS_REGISTER)
	
			// The new context to switch to. Must have been initialized by EXEC
			VMContext * newContext = (VMContext *) readOperand(context, inst, OPERAND_LEFT);
			ASSERT(newContext)
			// NOTE: if the new context program counter is already at end,
			// we can abort here

			// upon return, parent will continue execution at the next instruction
			newContext->parentContext = context;
			// transfer control
			context = newContext;
			goto iterate;
		}

		case OP_YIELD: {
			vm.flag = true;
			// On RESUME we will continue at the instruction following this one
			context->programCounter++;
			if(context->parentContext) {
				// continue parent context execution
				context = context->parentContext;
				break;
			}
			else {
				// YIELD from root context ends execution
				FreeChildContexts(context);
				return;
			}
		}

		case OP_END: {
			// END terminates the current context
			vm.flag = false;
			// NOTE: any register holding a reference-counted datum should be released?
			// Deallocating child contexts seem like a special case of this ...
			FreeChildContexts(context);	
			VMContext * parentContext = context->parentContext;
			if(parentContext) {
				// switch to parent context
				context = parentContext;
				break;
			}
			else {
				// Root context
				return;
			}
		}

		default:
			// instruction not implemented yet
			ASSERT(false)
			break;
		}
		context->programCounter++;
	}

}
