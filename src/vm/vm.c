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

	vm.trace = false;
}

/**
 * Execute a bytecode context. If a YIELD was executed, the VM flag
 * is set; if execution terminated, the flag is cleared.
 */

bool VMCall(Atom context)
{
	// Iterate through program, starting at the current program counter
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
		TypedAtom typedAtom;
		switch(inst.fields.opcode) {
		case OP_COPY:
			// NOTE: we could make this handle typed arguments too
			// by checking if the destination is a COMPILED_CONTEXT parameter
			// -- this should be the only case where types are used (?)
			left = ContextReadOperand(context, inst, OPERAND_LEFT);
			ContextWriteOperand(context, inst, OPERAND_RIGHT, left);
			break;

		case OP_TCOPY:
			typedAtom = ContextReadTypedOperand(context, inst, OPERAND_LEFT);
			ContextWriteTypedOperand(context, inst, OPERAND_RIGHT, typedAtom);
			break;

		case OP_EQ:
			// TODO
			ASSERT(false);
			break;
		
		case OP_NOT:
			vm.flag = vm.flag ? false : true;
			break;

		case OP_JUMP:
			// NOTE: operand must be a UINT
			left = ContextReadOperand(context, inst, OPERAND_LEFT);
			BytecodeContextJump(context, (uint32) left);
			break;

		case OP_JUMPIF:
			// jump if the flag is true
			if(vm.flag) {
				left = ContextReadOperand(context, inst, OPERAND_LEFT);
				BytecodeContextJump(context, (uint32) left);
			}
			break;

		case OP_CTX: {
			/** 
			 * CTX <service> <operand>
			 * Create a bytecode context and store in the destination operand.
			 */
			Atom service = ContextReadOperand(context, inst, OPERAND_LEFT);
			ServiceRecord record = RegistryGetServiceRecord(service);
			Atom newContext;
			switch(record.type) {
			case SERVICE_BYTECODE:
				newContext = CreateBytecodeContext(&record, context);
				break;

			case SERVICE_BTREE:	// should be general compiiled service ...
				newContext = CreateCompiledContext(&record);
				break;
			
			default:
				ASSERT(false);
				break;
			}  
			ContextWriteOperand(context, inst, OPERAND_RIGHT, (Atom) newContext);
			break;
		}

		case OP_CALL: {
			/**
			 * CALL <context>
			 * Give control to another execution context.
			 */

			// Currently, contexts must be stored in registers.
			ASSERT(inst.fields.accessMode.op1 == ACCESS_REGISTER)
	
			// The child context to switch to. Must have been initialized by BCTX
			Atom childContext = ContextReadOperand(context, inst, OPERAND_LEFT);
			ASSERT(childContext)
			switch(ContextGetType(childContext)) {
				case BYTECODE_CONTEXT: {
					// NOTE: if the child context program counter is already at end,
					// we can abort here

					// upon YIELD we will return to this context
					// NOTE: since this can be modified between calls, it is possible
					// to pass a context to another program as an argument ...
					BytecodeContextSetParent(childContext, context);
					// transfer control
					context = childContext;
					break;
				}
				case COMPILED_CONTEXT: {
					vm.flag = CompiledContextCall(childContext);
					break;
				}
			}
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
				// context must be free'd by caller
				return true;
			}
		}

		case OP_END: {
			// END terminates the current context
			vm.flag = false;
			// TODO: this needs testing
			Atom parentContext = BytecodeContextGetParent(context);
			FreeContext(context);	
			if(parentContext) {
				// switch to parent context
				context = parentContext;
				break;
			}
			else {
				// Root context
				return false;
			}
		}

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

		case OP_DEC:
			// TODO
			ASSERT(false);
			break;

		case OP_MUL:
			left = ContextReadOperand(context, inst, OPERAND_LEFT);
			right = ContextReadOperand(context, inst, OPERAND_RIGHT);
			right = ((uint64) left) * ((uint64) right);
			ContextWriteOperand(context, inst, OPERAND_RIGHT, right);
			break;

		case OP_LESS:
			// TODO
			ASSERT(false);
			break;

		case OP_LESSEQ:
			// TODO
			ASSERT(false);
			break;

		default:
			// instruction not implemented yet
			ASSERT(false)
			break;
		}
	}
}


bool VMExecuteService(ServiceRecord * service, Tuple * arguments)
{
 	Atom context = CreateBytecodeContext(service, 0);
	ContextSetParameters(context, arguments);

	if(VMCall(context)) {
		// root context ended with YIELD
		// NOTE: here we could resume the context, but for now
		// we remove it and return only the first result tuple
		ContextGetParameters(context, arguments);
		FreeContext(context);
		return true;
	}
	else
		return false;
}


Atom VMBeginService(ServiceRecord * service, Tuple * arguments)
{
	Atom context = CreateBytecodeContext(service, 0);
	ContextSetParameters(context, arguments);
	return context;
}



