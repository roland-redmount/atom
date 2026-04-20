/**
 * A bytecode instruction, AT_INSTRUCTION.
 */

#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "lang/TypedAtom.h"

/**
 * A bytecode instruction is coded as a one-byte opcode,
 * optionally an access mode byte, and one or two operand bytes,
 * for a maximum size fo 4 bytes. We store each instruction in a Atom.
 * 
 * Instructions with zero operands (NOT, END, etc) use only the opcode byte.
 * For two-operand instructions, operand 1 is always the source, operand 2 is destination.
 */


/**
 * Opcodes
 */

// general purpose instructions, any atom type
#define OP_NOP			0
#define	OP_COPY			0x01
#define	OP_EQ			0x02

// program control
#define	OP_NOT			0x10
#define	OP_MARK			0x11
#define	OP_JUMP			0x12		// should we use an instruction offset?
#define	OP_ENDIF		0x13
#define	OP_YES			0x14
#define	OP_YESIF		0x15
#define OP_BCTX			0x16		// create bytecode context
#define OP_CCTX			0x17		// create C context
#define OP_BCALL		0x18		// call bytecode context
#define OP_CCALL		0x19		// call C service
#define OP_YIELD		0x1A
#define OP_END			0x1B

// integer arithmetic
#define	OP_ADD			0x20
#define	OP_SUB			0x21
#define	OP_INC			0x22
#define	OP_DEC			0x23
#define OP_MUL			0x24
#define	OP_LESS			0x25
#define	OP_LESSEQ		0x26

// floating points arithmetic
// NOTE: we have distinct opcodes from integer arithmetic
// since floating point operations are approximate and
// conversion to/from integers should be explicit
#define	OP_FADD			0x30
#define	OP_FSUB			0x31
#define	OP_FMUL			0x32
#define	OP_FDIV			0x33
#define	OP_FLESS		0x34
#define	OP_FLESSEQ		0x35


/**
 * Structure stored in a 64-bit instruction atom.
 * Each operand is an argument, a register, or a constant.
 * We may prefix each argument with a register holding
 * a context object, from which the operand is read.
 * (By default the operand is read from the current context.)
 * 
 * TODO: for JUMP instruction we need a line number instead
 * of operands. This can be a union.
 */
typedef union {
	struct {
		byte opcode;
		struct {
			byte op1 : 2;
			byte op2 : 2;
		} accessMode;
		index8 op1ContextRegister;
		index8 op1Index;		// 1-based indices into operand
		index8 op2ContextRegister;
		index8 op2Index;
	} fields;
	data64 value;
} Instruction;

#define	ACCESS_NONE			0		// operand not used by instruction
#define	ACCESS_PARAMETER	0x1
#define	ACCESS_REGISTER		0x2
#define	ACCESS_CONSTANT		0x3


typedef enum { OPERAND_LEFT, OPERAND_RIGHT } Operand;


/**
 * Create an instruction, specifying operands with indices.
 * Operands not used by the opcode are ignored.
 */
void InstructionBegin(Instruction * draft, byte opcode);

/**
 * Add an argument operand represented by an index into the
 * parameters, register or constant lists.
 */
void InstructionSetOperand(Instruction * draft, Operand operand, index8 opIndex, byte accessMode);


void InstructionSetContext(Instruction * draft, Operand operand, index8 registerIndex);


Atom InstructionEnd(Instruction * draft);

/**
 * View the instruction data structure
 */
Instruction InstructionGetData(Atom instruction);

/**
 * Return the instruction opcode.
 */
byte InstructionGetOpCode(Atom instruction);

void PrintInstruction(Atom instruction);


#endif	// INSTRUCTION_H

