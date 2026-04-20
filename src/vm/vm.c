#include "kernel/list.h"
#include "kernel/ServiceRegistry.h"
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


static Atom readOperand(Atom context, Instruction inst, Operand operand)
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
		Atom operandContext = contextIndex ?
			ContextRegisters(context)[contextIndex - 1] :
			context;
		return ContextArguments(operandContext)[opIndex - 1];
	}
	
	case ACCESS_REGISTER:
		return ContextRegisters(context)[opIndex - 1];

	case ACCESS_CONSTANT:
		return ContextConstants(context)[opIndex - 1];

	default:
		ASSERT(false);
		return 0;
	}
}


void writeOperand(Atom context, Instruction inst, index8 operand, Atom atom)
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
		Atom operandContext = contextIndex ?
			ContextRegisters(context)[contextIndex - 1] :
			context;
		ContextArguments(operandContext)[opIndex - 1] = atom;
		break;
	}
	
	case ACCESS_REGISTER:
		ContextRegisters(context)[opIndex - 1] = atom;
		break;

	case ACCESS_CONSTANT:
		ASSERT(false);

	default:
		ASSERT(false);
	}
}


Atom VMCreateRootContext(ServiceRecord * service, Atom * arguments)
{
 	Atom context = CreateBytecodeContext(service, 0);
	// copy arguments to context
	size8 nArguments = ContextNArguments(context);
	for(index8 i = 0; i < nArguments; i++)
		ContextArguments(context)[i] = arguments[i];

	return context;
}


void VMExecute(Atom context)
{
	// Iterate through program
	while(true) {
		Atom instruction;
		Instruction inst = {0};
		if(ContextNextInstruction(context, &instruction)) {
			if(vm.trace)
				PrintInstruction(instruction);
			inst = InstructionGetData(instruction);
		}
		else {
			// Reached end of program, implicit END instruction
			inst.fields.opcode = OP_END;
		}

		Atom left, right;
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
		
		case OP_BCTX: {
			/** 
			 * BCTX <service> <operand>
			 * Create a bytecode context and store in the destination operand.
			 * 
			 * TODO: handle B-tree services
			 */
			Atom service = readOperand(context, inst, OPERAND_LEFT);
			ServiceRecord record = RegistryGetServiceRecord(service);
			ASSERT(record.type == SERVICE_BYTECODE);
			Atom newContext = CreateBytecodeContext(&record, context);
			writeOperand(context, inst, OPERAND_RIGHT, (Atom) newContext);
			break;
		}

		case OP_CALL: {
			/**
			 * CALL <context>
			 * Give control to another execution context.
			 */

			// Currently, contexts must be stored in registers.
			ASSERT(inst.fields.accessMode.op1 == ACCESS_REGISTER)
	
			// The new context to switch to. Must have been initialized by CTX
			Atom newContext = readOperand(context, inst, OPERAND_LEFT);
			ASSERT(newContext)
			// NOTE: if the new context program counter is already at end,
			// we can abort here

			// upon YIELD we will return to this context
			// NOTE: since this can be modified between calls, it is possible
			// to pass a context to another program as an argument ...
			ContextSetParent(newContext, context);
			// transfer control
			context = newContext;
			break;
		}

		case OP_YIELD: {
			vm.flag = true;
			Atom parentContext = ContextGetParent(context);
			if(parentContext) {
				// continue parent context execution
				// On next CALL, we continue at the instruction following this one
				context = parentContext;
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
			// NOTE: any register holding a reference-counted atom should be released?
			// Deallocating child contexts seem like a special case of this ... but we
			// don't have reference counting for contexts.
			FreeChildContexts(context);	
			Atom parentContext = ContextGetParent(context);
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
	}

}
