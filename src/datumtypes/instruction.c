
#include "datumtypes/instruction.h"


char const * mnemonics[] = {
	"INVALID",
	
	// general purpose instructions, any datum type
	"COPY",
	"EQ",

	// program control
	"NOT",
	"MARK",
	"JUMP",
	"ENDIF",
	"YES",
	"YESIF",

	// integer arithmetic
	"ADD",
	"SUB",
	"INC",
	"DEC",
	"MUL",
	"LESS",
	"LESSEQ",

	// floating points arithmetic
	"FADD",
	"FSUB",
	"FMUL",
	"FDIV",
	"FLESS",
	"FLESSEQ",
};


void InstructionBegin(Instruction * draft, OpCode opcode)
{
	SetMemory(draft, sizeof(Instruction), 0);
	draft->fields.opcode = opcode;
}


static void addOperand(Instruction * draft, byte operand, byte accessMode)
{
	if(!draft->fields.op1) {
		draft->fields.op1 = operand;
		draft->fields.accessMode.op1 = accessMode;
	}
	else if(!draft->fields.op2) {
		draft->fields.op2 = operand;
		draft->fields.accessMode.op2 = accessMode;
	}
	else
		ASSERT(false);	// both operands have already been assigned
}

void InstructionOperandArgument(Instruction * draft, index8 argumentIndex)
{
	addOperand(draft, argumentIndex, ACCESS_ARGUMENT);
}


void InstructionOperandRegister(Instruction * draft, index8 registerIndex)
{
	addOperand(draft, registerIndex, ACCESS_REGISTER);
}


void InstructionOperandConstant(Instruction * draft, index8 constantIndex)
{
	addOperand(draft, constantIndex, ACCESS_CONSTANT);
}


Atom InstructionEnd(Instruction * draft)
{
	return (Atom) {DT_INSTRUCTION, 0, 0, 0, draft->value};
}


Instruction InstructionGetData(Atom instruction)
{
	Instruction inst;
	inst.value = instruction.datum;
	return inst;
}



OpCode InstructionGetOpCode(Atom instruction)
{
	Instruction inst = InstructionGetData(instruction);
	return inst.fields.opcode;	
}


static void printOperand(byte accessMode, index8 op)
{
	// decorator
	switch(accessMode) {
	case ACCESS_ARGUMENT:
		PrintChar('_');
		break;
	case ACCESS_REGISTER:
		PrintChar('@');
		break;
	default:
		// no decorator
		break;
	}	
	PrintF("%u ", op);
}


void PrintInstruction(Atom instruction)
{
	Instruction inst = InstructionGetData(instruction);

	PrintCString(mnemonics[inst.fields.opcode]);
	PrintChar(' ');
	printOperand(inst.fields.accessMode.op1, inst.fields.op1);
	printOperand(inst.fields.accessMode.op2, inst.fields.op2);
	PrintChar('\n');
}
