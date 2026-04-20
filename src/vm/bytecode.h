/**
 * A bytecode program consists of registers, constants, and an instruction list.
 */

#ifndef BYTECODE_H
#define BYTECODE_H

#include "kernel/ifact.h"
#include "vm/instruction.h"


typedef struct s_BytecodeDraft {
	Atom registers;
	IFactDraft constantsDraft;
	IFactDraft programDraft;
	Instruction instructionDraft;
} BytecodeDraft;

/**
 * Initialize a bytecode block from a DT_FORMULA signature,
 * and an array of initial values for registers.
 */
void BytecodeBegin(BytecodeDraft * draft, Atom registers);

/**
 * Structure specifying a bytecode argument or operand
 */
typedef struct {
	enum {ARG_PARAMETER, ARG_REGISTER, ARG_CONSTANT} type;
	union {
		TypedAtom parameter;
		index8 registerIndex;
		TypedAtom constant;
	} value;
} BytecodeArgument;



/**
 * Being a new bytecode instruction, to be appended to the program
 */
void BytecodeBeginInstruction(BytecodeDraft * draft, byte opcode);

/**
 * Add a operand reference to a parameter @index or $index.
 * Since read / write access is fully determined by the instruction opcode,
 * we do not need to explitly specify input or output parameter here.
 */
void BytecodeOperandParameter(BytecodeDraft * draft, Operand operand, index8 index);
void BytecodeOperandRegister(BytecodeDraft * draft, Operand operand, index8 registerIndex);

void BytecodeOperandConstant(BytecodeDraft * draft, Operand operand, TypedAtom constant);

void BytecodeOperandSetContext(BytecodeDraft * draft, Operand operand, index8 registerIndex);

void BytecodeEndInstruction(BytecodeDraft * draft);


/**
 * Finalize bytecode and create a AT_ID atom. This creates the relations
 * 
 * (bytecode @b signature f) where f is a formula
 * 
 * (bytecode @b registers r)
 * where r is a list of initial values for registers (atoms),
 * which also determines each register's atom type.
 * 
 * (bytecode @b constants c)
 * where c is a list of constants (atoms).
 *
 * (bytecode @b program i)  where i is a list of instructions
 * 
 */
Atom BytecodeEnd(BytecodeDraft * draft);


bool IsBytecode(Atom atom);

/**
 * Returns a list of instructions
 */
Atom BytecodeGetProgram(Atom bytecode);

/**
 * Read a instruction (AT_INSTRUCTION) from a bytecode block.
 */
Atom BytecodeGetInstruction(Atom bytecode, index32 pc);

/**
 * Get the list of registers (initial values)
 */
Atom BytecodeGetRegisters(Atom bytecode);

/**
 * Get a list of constants
 */
Atom BytecodeGetConstants(Atom bytecode);

/**
 * Create core bytecode services, such as arithmetic operations.
 */
void SetupServiceLibrary(void);

void TeardownCoreServices(void);

void PrintBytecode(Atom bytecode);


#endif	// BYTECODE_H

