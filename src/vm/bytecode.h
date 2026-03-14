/**
 * A bytecode program consists of a signature, registers,
 * constants, and an instruction list (program).
 */

#ifndef BYTECODE_H
#define BYTECODE_H

#include "kernel/ifact.h"
#include "datumtypes/instruction.h"


typedef struct s_BytecodeDraft {
	Atom signature;
	Atom registers;
	IFactDraft constantsDraft;
	IFactDraft programDraft;
	Instruction instructionDraft;
	size32 callCounter;
} BytecodeDraft;

/**
 * Initialize a bytecode block from a DT_FORMULA signature,
 * and an array of initial values for registers.
 */
void BytecodeBegin(BytecodeDraft * draft, Atom signature, Atom registers);

/**
 * Structure specifying a bytecode argument or operand
 */
typedef struct {
	enum {ARG_PARAMETER, ARG_REGISTER, ARG_CONSTANT} type;
	union {
		Atom parameter;
		index8 registerIndex;
		Atom constant;
	} value;
} BytecodeArgument;



/**
 * Being a new bytecode instruction, to be appended to the program
 */
void BytecodeBeginInstruction(BytecodeDraft * draft, byte opcode);

void BytecodeAddOperand(BytecodeDraft * draft, BytecodeArgument argument);


void BytecodeOperandConstant(BytecodeDraft * draft, Atom constant);
void BytecodeOperandParameter(BytecodeDraft * draft, Atom parameter);
void BytecodeOperandRegister(BytecodeDraft * draft, index8 registerIndex);
void BytecodeEndInstruction(BytecodeDraft * draft);

/** 
 * Generate sequence of call instructions
 * 
 *   PUSH argument_N
 *   ...
 *   PUSH argument_1
 *   CALL <service>
 * 
 * Actors in the query formula may be eiher variables referring to
 * the calling bytecode's parameters, or integers referring to registers.
 * (We should probably have a better representation of this.)
 * This function retrieves the bytecode service and adds the PUSH and
 * CALL instructions to the bytecode draft.
 */
void BytecodeGenerateCall(BytecodeDraft * draft, Atom bytecode, Atom query);

/**
 * Finalize bytecode and create IFact atom. This creates the relations
 * 
 * (bytecode @b signature f) where f is a DT_FORMULA
 * 
 * (bytecode @b registers r)
 * where r is a list of initial values for registers (atoms),
 * which also determines each register's datum type.
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
 * Returns a formula
 */
Atom BytecodeGetSignature(Atom bytecode);

/**
 * Read a instruction from a bytecode block.
 */
Atom BytecodeGetInstruction(Atom bytecode, index32 pc);

/**
 * Get a list of registers (initial values)
 */
Atom BytecodeGetRegisters(Atom bytecode);

/**
 * Get a list of constants
 */
Atom BytecodeGetConstants(Atom bytecode);

/**
 * Returns the number of CALL instructions in the bytecode,
 * which equals the largest number of child contexts that
 * may be needed during execution.
 */
size32 BytecodeNChildContexts(Atom bytecode);

/**
 * Create core bytecode services, such as arithmetic operations.
 */
void SetupCoreServices(void);

void TeardownCoreServices(void);

#endif	// BYTECODE_H

