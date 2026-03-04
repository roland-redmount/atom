
#include "kernel/list.h"
#include "lang/Formula.h"
#include "vm/bytecode.h"
#include "vm/vm.h"


static struct {
	void * stack;		// this should be a predetermined address range 
	size32 stackSize;

	VMContext * rootContext;
	VMContext * currentContext;
} vm;


void VMInitialize(void * stack, size32 stackSize)
{
	vm.stack = stack;
	vm.stackSize = stackSize;

	// TODO: create root context
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

VMContext * VMCreateContext(Atom bytecode, Datum * actors)
{
	// temporary
	VMContext * context = (VMContext * ) vm.stack;
	SetMemory(context, sizeof(VMContext), 0);

	context->bytecode = bytecode;
	context->arity = FormulaArity(BytecodeGetSignature(bytecode));

	// NOTE: do we need to copy actors? 

	// copy registers
	// NOTE: this will be more efficient if we use an array-based list relation
	// where datums and types are separated
	copyListDatums(BytecodeGetRegisters(bytecode), &context->registers[0]);

	return context;
}


// TODO: this is very inefficient
static Datum accessConstant(Atom bytecode, index8 op)
{
	Atom constantsList = BytecodeGetConstants(bytecode);
	return ListGetElement(constantsList, op).datum;
}

#define OPERAND_LEFT	1
#define OPERAND_RIGHT	2

static Datum readOperand(
	VMContext const * context, Instruction inst, Datum * const actors, index8 operand)
{
	index8 op;
	byte accessMode;
	if(operand == OPERAND_LEFT) {
		 op = inst.fields.op1;
		 accessMode = inst.fields.accessMode.op1;
	}
	else {
		op = inst.fields.op2;
		accessMode = inst.fields.accessMode.op2;
	}

	switch(accessMode) {
	case ACCESS_ARGUMENT:
		return actors[op - 1];
	
	case ACCESS_REGISTER:
		return context->registers[op - 1];

	case ACCESS_CONSTANT:
		return accessConstant(context->bytecode, op);

	default:
		ASSERT(false);
		return 0;
	}
}


static void writeOperand(
	VMContext * context, Instruction inst, Datum * actors, index8 operand, Datum value)
{
	index8 op;
	byte accessMode;
	if(operand == OPERAND_LEFT) {
		 op = inst.fields.op1;
		 accessMode = inst.fields.accessMode.op1;
	}
	else {
		op = inst.fields.op2;
		accessMode = inst.fields.accessMode.op2;
	}

	switch(accessMode) {
	case ACCESS_ARGUMENT:
		actors[op - 1] = value;
		break;
	
	case ACCESS_REGISTER:
		context->registers[op - 1] = value;
		break;

	case ACCESS_CONSTANT:
		ASSERT(false);
	}
}



void VMExecuteInstruction(VMContext * context, Atom instruction, Datum * actors)
{
	Instruction inst = InstructionGetData(instruction);
	Datum left, right;
	switch(inst.fields.opcode) {
	case OP_COPY:
		left = readOperand(context, inst, actors, OPERAND_LEFT);
		writeOperand(context, inst, actors, OPERAND_RIGHT, left);
		break;

	case OP_ADD:
		left = readOperand(context, inst, actors, OPERAND_LEFT);
		right = readOperand(context, inst, actors, OPERAND_RIGHT);
		right = ((uint64) left) + ((uint64) right);
		writeOperand(context, inst, actors, OPERAND_RIGHT, right);
		break;

	case OP_MUL:
		left = readOperand(context, inst, actors, OPERAND_LEFT);
		right = readOperand(context, inst, actors, OPERAND_RIGHT);
		right = ((uint64) left) * ((uint64) right);
		writeOperand(context, inst, actors, OPERAND_RIGHT, right);
		break;

	}
}


void VMExecute(Atom bytecode, Datum * actors)
{
	VMContext * context = VMCreateContext(bytecode, actors);

	Atom instructions = BytecodeGetProgram(bytecode);

	ListIterator iterator;
	ListIterate(instructions, &iterator);
	while(ListIteratorHasNext(&iterator)) {
		Atom instruction = ListIteratorGetElement(&iterator);
		VMExecuteInstruction(context, instruction, actors);

		ListIteratorNext(&iterator);
	}

	ListIteratorEnd(&iterator);
}
