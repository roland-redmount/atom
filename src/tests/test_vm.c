
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


typedef struct {
	Atom bytecode;
	Atom signature;		// a formula
	Atom registers;		// a list
} BytecodeFixture;


/**
 * Example program 1, computing t = x + 2*x
 * This program tests use of parameters, registers and constants.
 * As this program has no CALL instruction, it 
 * 
 * number x>INT triple t<INT
 * #1:INT = 0
 * #2: INT = 2   // register used as a constant
 *   COPY x t
 * 	 COPY x #1
 *   MUL #2 #1
 *   ADD #1 t
 */

BytecodeFixture setupBytecodeFixture1(void)
{
	BytecodeFixture fixture;

	// Bytecode signature
	// TODO: should have two distinct datum types, make t a UINT ?
	// NOTE: we need the $ delimiter here since the tokenized currently
	// does not have look-ahead.
	fixture.signature = CStringToPredicate("number $x<INT triple $t>INT");

	Atom x = CreateParameter('x', PARAMETER_IN, DT_INT);
	Atom t = CreateParameter('t', PARAMETER_OUT, DT_INT);

	// list of registers with initial values
	fixture.registers = CreateListFromArray(
		(Atom []) {CreateInt(0), CreateInt(2)},
		2
	);

	// create bytecode draft
	BytecodeDraft bytecodeDraft;
	BytecodeBegin(&bytecodeDraft, fixture.signature, fixture.registers);
	
	// add instructions
	// COPY x t
	BytecodeBeginInstruction(&bytecodeDraft, OP_COPY);
	BytecodeOperandParameter(&bytecodeDraft, x);
	BytecodeOperandParameter(&bytecodeDraft, t);
	BytecodeEndInstruction(&bytecodeDraft);

	// COPY x #1
	BytecodeBeginInstruction(&bytecodeDraft, OP_COPY);
	BytecodeOperandParameter(&bytecodeDraft, x);
	BytecodeOperandRegister(&bytecodeDraft, 1);
	BytecodeEndInstruction(&bytecodeDraft);

	// MUL 2 #1
	BytecodeBeginInstruction(&bytecodeDraft, OP_MUL);
	BytecodeOperandRegister(&bytecodeDraft, 2);
	BytecodeOperandRegister(&bytecodeDraft, 1);
	BytecodeEndInstruction(&bytecodeDraft);

	// ADD #1 t
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


void testBytecodeProgram1(void)
{
	BytecodeFixture fixture = setupBytecodeFixture1();
	
	ASSERT_TRUE(IsBytecode(fixture.bytecode))
	ASSERT_TRUE(SameAtoms(BytecodeGetSignature(fixture.bytecode), fixture.signature))

	ASSERT_TRUE(SameAtoms(BytecodeGetRegisters(fixture.bytecode), fixture.registers))

	Atom program = BytecodeGetProgram(fixture.bytecode);
	ASSERT_UINT32_EQUAL(ListLength(program), 4);

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


void testExecuteByteCode1(void)
{
	BytecodeFixture fixture = setupBytecodeFixture1();
	PrintFormula(fixture.signature);
	PrintChar('\n');
	
	// TODO: this just happens to be the correct argument order ...
	Datum x = CreateInt(3).datum;
	Datum y;

	Datum * actors[2] = {&x, &y};
	VMStart(fixture.bytecode, actors);
	// results should be 3 * 3 
	ASSERT_UINT32_EQUAL(*actors[1], 9);

	teardownBytecodeFixture(fixture);
}

/**
 * Example program 2, calling program 1
 * 
 * To call a bytecode program, we must push its
 * arguments on the stack in canonical order and
 * CALL the bytecode service  (number triple).
 * This program tests pushing both a parameter
 * and a registe as arguments to the CALLed program.
 * 
 * number $x>INT quadruple $t<INT
 * #1: INT
 *   COPY   x #1
 *   PUSH	t		// push output
 *   PUSH	#1		// push input
 *   CALL   <number triple>		// (number #1 triple t)
 *   ADD    x t
 *   YIELD
 *   RESUME 
 */
BytecodeFixture setupBytecodeFixture2(void)
{
	BytecodeFixture childFixture = setupBytecodeFixture1();

	BytecodeFixture fixture;

	// Bytecode signature
	
	// TODO: should have two distinct datum types, make t a UINT ?
	
	// NOTE: we need the $ delimiter here since the tokenized currently
	// does not have look-ahead.

	/**
	 * TODO: we could avoid introduing parameter syntax $x<INT by instead using
	 * variables (number x sextuple t) and then performing variable
	 * substitution x -> $x<INT, t -> $t>INT on the formula's actor list
	 * to obtain the parameter list.
	 * This also avoid the possibility of inconsistent parameter assignments,
	 * as in foo x<FLOAT bar x>INT
	 */
	fixture.signature = CStringToPredicate("number $x<INT quadruple $t>INT");

	Atom x = CreateParameter('x', PARAMETER_IN, DT_INT);
	Atom t = CreateParameter('t', PARAMETER_OUT, DT_INT);

	// list of register initial values
	fixture.registers = CreateListFromArray((Atom []) {CreateInt(0)}, 1);

	// create bytecode draft
	BytecodeDraft bytecodeDraft;
	BytecodeBegin(&bytecodeDraft, fixture.signature, fixture.registers);

	// COPY x #1
	BytecodeBeginInstruction(&bytecodeDraft, OP_COPY);
	BytecodeOperandParameter(&bytecodeDraft, x);
	BytecodeOperandRegister(&bytecodeDraft, 1);
	BytecodeEndInstruction(&bytecodeDraft);

	/** 
	 * Generate sequence of call instructions
	 *   PUSH x
	 *   PUSH t
	 *   CALL (number triple)
	 * ensuring that PUSH instructions are in correct order.
	 * First, we generate a query formula to get actors in the correct order.
	 * Then, BytecodeGenerateCall() retrieves the bytecode service and
	 * generates the PUSH and CALL instructions.
	 */

	// Generate an array of arguments for call to (number #1 triple t).
	// We must pair each argument with a role name. The best option is probably
	// to parse out the *form* (not formula) from a string "number triple" ??


	Atom query = CStringToPredicate("number #1 triple $t>INT");
	PrintFormula(query);
	PrintChar('\n');
	BytecodeGenerateCall(&bytecodeDraft, childFixture.bytecode, query);


	// ADD x t
	BytecodeBeginInstruction(&bytecodeDraft, OP_ADD);
	BytecodeOperandParameter(&bytecodeDraft, x);
	BytecodeOperandParameter(&bytecodeDraft, t);
	BytecodeEndInstruction(&bytecodeDraft);

	// finalize bytecode and create atom
	fixture.bytecode = BytecodeEnd(&bytecodeDraft);
	teardownBytecodeFixture(childFixture);

	return fixture;
}


void testExecuteByteCode2(void)
{
	BytecodeFixture fixture = setupBytecodeFixture2();
	PrintFormula(fixture.signature);
	PrintChar('\n');
	
	Datum x = CreateInt(3).datum;
	Datum y;

	Datum * actors[2] = {&x, &y};
	VMStart(fixture.bytecode, actors);
	// results should be 3 * 4 
	ASSERT_UINT32_EQUAL(*actors[1], 12);

	teardownBytecodeFixture(fixture);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	testBytecodeProgram1();
	testExecuteByteCode1();
	testExecuteByteCode2();

	TestSummary();

	KernelShutdown();
}
