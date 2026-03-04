
#include "datumtypes/Parameter.h"
#include "datumtypes/instruction.h"
#include "datumtypes/Int.h"
#include "datumtypes/Variable.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "parser/PredicateBuilder.h"
#include "vm/bytecode.h"
#include "vm/vm.h"


#include "testing/testing.h"


/**
 * Create an example bytecode program, computing t = x + 2*x
 * 
 * number $x>INT triple $t<INT
 * #1:INT = 0
 *   COPY x t
 * 	 COPY x #1
 *   MUL 2 #1
 *   ADD #1 t
 */

#define N_PARAMETERS 2
#define N_INSTRUCTIONS 4
#define N_REGISTERS 1


typedef struct {
	Atom bytecode;
	Atom signature;
	Atom registers;
} BytecodeFixture;


BytecodeFixture setupBytecodeFixture(void)
{
	BytecodeFixture fixture;

	// Bytecode signature
	// TODO: should have two distinct datum types, make t a UINT ?

	fixture.signature = CStringToPredicate("number $x>INT triple $t<INT");

	Atom x = CreateParameter('x', PARAMETER_IN, DT_INT);
	Atom t = CreateParameter('t', PARAMETER_OUT, DT_INT);

	// Number of registers and their initial values
	fixture.registers = CreateListFromArray((Atom []) {CreateInt(0)}, N_REGISTERS);

	// create bytecode draft
	BytecodeDraft bytecodeDraft;
	BytecodeBegin(&bytecodeDraft, fixture.signature, fixture.registers);
	
	// add instructions
	// COPY x t
	BytecodeBeginInstruction(&bytecodeDraft, OP_COPY);
	BytecodeOperandParameter(&bytecodeDraft, x);
	BytecodeOperandParameter(&bytecodeDraft, t);
	BytecodeEndInstruction(&bytecodeDraft);

	// COPY x $0
	BytecodeBeginInstruction(&bytecodeDraft, OP_COPY);
	BytecodeOperandParameter(&bytecodeDraft, x);
	BytecodeOperandRegister(&bytecodeDraft, 1);
	BytecodeEndInstruction(&bytecodeDraft);

	// MUL 2 $0
	BytecodeBeginInstruction(&bytecodeDraft, OP_MUL);
	BytecodeOperandConstant(&bytecodeDraft, CreateInt(2));
	BytecodeOperandRegister(&bytecodeDraft, 1);
	BytecodeEndInstruction(&bytecodeDraft);

	// ADD @0 t
	BytecodeBeginInstruction(&bytecodeDraft, OP_ADD);
	BytecodeOperandRegister(&bytecodeDraft, 1);
	BytecodeOperandParameter(&bytecodeDraft, t);
	BytecodeEndInstruction(&bytecodeDraft);

	// finalize bytecode and create atom
	fixture.bytecode = BytecodeEnd(&bytecodeDraft);

	return fixture;
}


static void teardownBytecodeFixture(BytecodeFixture fixture)
{
	ReleaseAtom(fixture.bytecode);
	ReleaseAtom(fixture.signature);
	ReleaseAtom(fixture.registers);
}


void testCreateBytecode(void)
{
	BytecodeFixture fixture = setupBytecodeFixture();
	
	ASSERT_TRUE(IsBytecode(fixture.bytecode))
	ASSERT_TRUE(SameAtoms(BytecodeGetSignature(fixture.bytecode), fixture.signature))

	ASSERT_TRUE(SameAtoms(BytecodeGetRegisters(fixture.bytecode), fixture.registers))

	// the constant 2 used in the 3rd MOVE instruction
	Atom constants = BytecodeGetConstants(fixture.bytecode);
	ASSERT_UINT32_EQUAL(ListLength(constants), 1);
	ASSERT_UINT32_EQUAL(ListGetElement(constants, 1).datum, 2);

	Atom program = BytecodeGetProgram(fixture.bytecode);
	ASSERT_UINT32_EQUAL(ListLength(program), N_INSTRUCTIONS);

	ASSERT_UINT32_EQUAL(
		InstructionGetOpCode(ListGetElement(program, 1)),
		OP_COPY
	)
	ASSERT_UINT32_EQUAL(
		InstructionGetOpCode(ListGetElement(program, 2)),
		OP_COPY
	)
	ASSERT_UINT32_EQUAL(
		InstructionGetOpCode(ListGetElement(program, 3)),
		OP_MUL
	)
	ASSERT_UINT32_EQUAL(
		InstructionGetOpCode(ListGetElement(program, 4)),
		OP_ADD
	)

	teardownBytecodeFixture(fixture);
}


void testExecuteByteCode(void)
{
	BytecodeFixture fixture = setupBytecodeFixture();
	// PrintFormula(fixture.signature);
	// PrintChar('\n');
	
	Datum actors[] = {CreateInt(3).datum, 0};

	VMExecute(fixture.bytecode, actors);
	// results should be 3 * 3 
	ASSERT_UINT32_EQUAL(actors[1], 9);

	teardownBytecodeFixture(fixture);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	testCreateBytecode();
	testExecuteByteCode();

	TestSummary();

	KernelShutdown();
}
