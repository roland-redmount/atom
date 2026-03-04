/**
 * A bytecode instruction, DT_INSTRUCTION.
 */

#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "lang/Atom.h"

/**
 * A bytecode instruction is coded as a one-byte opcode,
 * optionally an access mode byte, and one or two operand bytes,
 * for a maximum size fo 4 bytes. We store each instruction in a Datum.
 * 
 * Instructions with zero operands (NOT, END, etc) use only the opcode byte.
 * For two-operand instructions, operand 1 is always the source, operand 2 is destination.
 */


/**
 * Opcodes
 */

typedef byte OpCode;

// general purpose instructions, any datum type
#define	OP_COPY			0x01
#define	OP_EQ			0x02

// program control
#define	OP_NOT			0x03
#define	OP_MARK			0x04
#define	OP_JUMP			0x05		// should we use an instruction offset?
#define	OP_ENDIF		0x06
#define	OP_YES			0x07
#define	OP_YESIF		0x08

// integer arithmetic
#define	OP_ADD			0x09
#define	OP_SUB			0x0A
#define	OP_INC			0x0B
#define	OP_DEC			0x0C
#define OP_MUL			0x0D
#define	OP_LESS			0x0E
#define	OP_LESSEQ		0x0F

// floating points arithmetic
// NOTE: we have distinct opcodes from integer arithmetic
// since floating point operations are approximate and
// conversion to/from integers should be explicit
#define	OP_FADD			0x10
#define	OP_FSUB			0x11
#define	OP_FMUL			0x12
#define	OP_FDIV			0x13
#define	OP_FLESS		0x14
#define	OP_FLESSEQ		0x15


/**
 * Structure stored in a 64-bit instruction datum.
 * Each operand is an argument, a register, or a constant.
 */
typedef union {
	struct {
		OpCode opcode;
		struct {
			byte op1 : 2;
			byte op2 : 2;
		} accessMode;
		index8 op1;		// 1-based indices into operand
		index8 op2;
	} fields;
	data64 value;
} Instruction;

#define	ACCESS_ARGUMENT		0x1
#define	ACCESS_REGISTER		0x2
#define	ACCESS_CONSTANT		0x3


/**
 * Create an instruction, specifying operands with indices.
 * Operands not used by the opcode are ignored.
 */
void InstructionBegin(Instruction * draft, OpCode opcode);

/**
 * Add an argumnet operand represented by an index into the
 * byte code arguments list.
 */
void InstructionOperandArgument(Instruction * draft, index8 argumentIndex);

/**
 * Add a register operand.
 */
void InstructionOperandRegister(Instruction * draft, index8 registerIndex);

/**
 * Add a constant operand.
 */
void InstructionOperandConstant(Instruction * draft, index8 constantIndex);

Atom InstructionEnd(Instruction * draft);

/**
 * View the instruction data structure
 */
Instruction InstructionGetData(Atom instruction);


// NOTE: this can be removed
OpCode InstructionGetOpCode(Atom instruction);

void PrintInstruction(Atom instruction);


#endif	// INSTRUCTION_H

