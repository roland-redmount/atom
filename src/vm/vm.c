#include "kernel/list.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "lang/Form.h"
#include "vm/instruction.h"
#include "vm/context.h"
#include "vm/vm.h"


/**
 * This structure describes a VM execution thread
 * TODO: this must not be a global, perhaps local to VMExecute() ?
 */
static struct {
	bool trace;
	bool flag;
} vm;


void VMInitialize()
{
	ASSERT(sizeof(Instruction) == 8)

	vm.trace = true;
}


Atom VMCreateRootContext(ServiceRecord * service, Atom * arguments)
{
 	Atom context = CreateBytecodeContext(service, 0);
	// copy arguments to context
	size8 nArguments = FormArity(service->form);
	for(index8 i = 0; i < nArguments; i++)
		ContextSetParameter(context, i, arguments[i]);

	return context;
}


void VMExecute(Atom context)
{
	// Iterate through program
	while(true) {
		Atom instruction;
		Instruction inst = {0};
		if(BytecodeContextNextInstruction(context, &instruction)) {
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
			left = ContextReadOperand(context, inst, OPERAND_LEFT);
			ContextWriteOperand(context, inst, OPERAND_RIGHT, left);
			break;

		case OP_ADD:
			left = ContextReadOperand(context, inst, OPERAND_LEFT);
			right = ContextReadOperand(context, inst, OPERAND_RIGHT);
			// TODO: INT vs UINT? Overflow?
			right = ((uint64) left) + ((uint64) right);
			ContextWriteOperand(context, inst, OPERAND_RIGHT, right);
			break;

		case OP_SUB:
			left = ContextReadOperand(context, inst, OPERAND_LEFT);
			right = ContextReadOperand(context, inst, OPERAND_RIGHT);
			// TODO: INT vs UINT? Overflow?
			right = ((uint64) right) - ((uint64) left);
			ContextWriteOperand(context, inst, OPERAND_RIGHT, right);
			break;

		case OP_INC:
			left = ContextReadOperand(context, inst, OPERAND_LEFT);
			ContextWriteOperand(context, inst, OPERAND_LEFT, ((uint64) left) + 1);
			break;

		case OP_MUL:
			left = ContextReadOperand(context, inst, OPERAND_LEFT);
			right = ContextReadOperand(context, inst, OPERAND_RIGHT);
			right = ((uint64) left) * ((uint64) right);
			ContextWriteOperand(context, inst, OPERAND_RIGHT, right);
			break;
		
		case OP_BCTX: {
			/** 
			 * BCTX <service> <operand>
			 * Create a bytecode context and store in the destination operand.
			 */
			Atom service = ContextReadOperand(context, inst, OPERAND_LEFT);
			ServiceRecord record = RegistryGetServiceRecord(service);
			ASSERT(record.type == SERVICE_BYTECODE);
			Atom newContext = CreateBytecodeContext(&record, context);
			ContextWriteOperand(context, inst, OPERAND_RIGHT, (Atom) newContext);
			break;
		}

		case OP_CCTX: {
			/** 
			 * CCTX <service> <operand>
			 * Create a C context and store in the destination operand.
			 */
			Atom service = ContextReadOperand(context, inst, OPERAND_LEFT);
			ServiceRecord record = RegistryGetServiceRecord(service);
			// TODO: this should be a more generic "C code" service?
			ASSERT(record.type == SERVICE_BTREE);
			Atom newContext = CreateCompiledContext(&record);
			ContextWriteOperand(context, inst, OPERAND_RIGHT, (Atom) newContext);
			break;
		}

		case OP_BCALL: {
			/**
			 * CALL <context>
			 * Give control to another execution context.
			 */

			// Currently, contexts must be stored in registers.
			ASSERT(inst.fields.accessMode.op1 == ACCESS_REGISTER)
	
			// The new context to switch to. Must have been initialized by BCTX
			Atom newContext = ContextReadOperand(context, inst, OPERAND_LEFT);
			ASSERT(newContext)
			// NOTE: if the new context program counter is already at end,
			// we can abort here

			// upon YIELD we will return to this context
			// NOTE: since this can be modified between calls, it is possible
			// to pass a context to another program as an argument ...
			BytecodeContextSetParent(newContext, context);
			// transfer control
			context = newContext;
			break;
		}

		case OP_CCALL: {
			/**
			 * CCALL <context
			 * Call a C service
			 */

			 // The new context to switch to. Must have been initialized by CCTX
			Atom cContext = ContextReadOperand(context, inst, OPERAND_LEFT);
			ASSERT(cContext)
			CompiledContextCall(cContext);
			break;
		}

		case OP_YIELD: {
			vm.flag = true;
			Atom parentContext = BytecodeContextGetParent(context);
			if(parentContext) {
				// continue parent context execution
				// On next CALL, we continue at the instruction following this one
				context = parentContext;
				break;
			}
			else {
				// YIELD from root context ends execution
				BytecodeContextFreeChildContexts(context);
				return;
			}
		}

		case OP_END: {
			// END terminates the current context
			vm.flag = false;
			// NOTE: any register holding a reference-counted atom should be released?
			// Deallocating child contexts seem like a special case of this ... but we
			// don't have reference counting for contexts.
			BytecodeContextFreeChildContexts(context);	
			Atom parentContext = BytecodeContextGetParent(context);
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
