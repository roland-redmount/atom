
#include "vm/instruction.h"


char const * mnemonics[] = {
	// 0x00 - 0x0F general purpose instructions, any atom type

	"NOP", "COPY", "EQ", "NOP", "NOP", "NOP", "NOP", "NOP", 
	"NOP", "NOP", "NOP", "NOP", "NOP", "NOP", "NOP", "NOP", 
	
	// 0x10 - 0x1F program control
	"NOT", "MARK", "JUMP", "ENDIF", "YES", "YESIF", "BCTX", "CCTX",
	"BCALL", "CCALL", "YIELD", "END", "NOP", "NOP", "NOP", "NOP",

	// 0x20 - 0x2F integer arithmetic
	"ADD", "SUB", "INC", "DEC", "MUL", "LESS", "LESSEQ", "NOP", 
	"NOP", "NOP", "NOP", "NOP", "NOP", "NOP", "NOP", "NOP", 

	// 0x30 - 0x3F floating points arithmetic
	"FADD", "FSUB", "FMUL", "FDIV", "FLESS", "FLESSEQ", "NOP", "NOP", 
	"NOP", "NOP", "NOP", "NOP", "NOP", "NOP", "NOP", "NOP", 
};


void InstructionBegin(Instruction * draft, byte opcode)
{
	SetMemory(draft, sizeof(Instruction), 0);
	draft->fields.opcode = opcode;
}


void InstructionSetOperand(Instruction * draft, Operand operand, index8 opIndex, byte accessMode)
{
	switch(operand) {
		case OPERAND_LEFT:
		draft->fields.op1Index = opIndex;
		draft->fields.accessMode.op1 = accessMode;
		break;

		case OPERAND_RIGHT:
		draft->fields.op2Index = opIndex;
		draft->fields.accessMode.op2 = accessMode;
	}
}


void InstructionSetContext(Instruction * draft, Operand operand, index8 contextIndex)
{
	switch(operand) {
		case OPERAND_LEFT:
		draft->fields.op1ContextRegister = contextIndex;
		break;

		case OPERAND_RIGHT:
		draft->fields.op2ContextRegister = contextIndex;
	}
}


Atom InstructionEnd(Instruction * draft)
{
	return (Atom) draft->value;
}


Instruction InstructionGetData(Atom instruction)
{
	Instruction inst;
	inst.value = instruction;
	return inst;
}


byte InstructionGetOpCode(Atom instruction)
{
	Instruction inst = InstructionGetData(instruction);
	return inst.fields.opcode;	
}


static void printOperand(Operand operand, byte accessMode, index8 opIndex, index8 contextIndex)
{
	// Contexts are always in registers
	if(contextIndex) {
		PrintF("#%u", contextIndex);
		// for contexts, we write to inputs (to set arguments)
		// and read from outputs (to retrieve results), so we
		// must flip the operand
		operand = (operand == OPERAND_LEFT) ? OPERAND_RIGHT : OPERAND_LEFT;
	}
	// decorator
	switch(accessMode) {
	case ACCESS_PARAMETER:
		// We assume left operand is read (@n), right is write ($n)
		if(operand == OPERAND_LEFT)
			PrintChar('@');
		else
			PrintChar('$');
		PrintF("%u ", opIndex);
		break;

	case ACCESS_REGISTER:
		PrintChar('#');
		PrintF("%u ", opIndex);
		break;

	case ACCESS_CONSTANT:
		// NOTE: we cannot print the constant value
		// without access to the bytecode
		PrintF("%u ", opIndex);
		break;

	case ACCESS_NONE:
		break;

	default:
		ASSERT(false)
		;
	}	
}


void PrintInstruction(Atom instruction)
{
	Instruction inst = InstructionGetData(instruction);
	// opcode
	PrintF("%-7s", mnemonics[inst.fields.opcode]);
	
	printOperand(OPERAND_LEFT,
		inst.fields.accessMode.op1, inst.fields.op1Index, inst.fields.op1ContextRegister);
	printOperand(OPERAND_RIGHT,
		inst.fields.accessMode.op2, inst.fields.op2Index, inst.fields.op2ContextRegister);
	PrintChar('\n');
}

