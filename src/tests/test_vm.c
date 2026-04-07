
#include "datumtypes/id.h"
#include "datumtypes/Parameter.h"
#include "datumtypes/instruction.h"
#include "datumtypes/Int.h"
#include "datumtypes/Variable.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "lang/name.h"
#include "kernel/string.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "lang/PredicateForm.h"
#include "parser/PredicateBuilder.h"
#include "vm/bytecode.h"
#include "vm/vm.h"

#include "testing/testing.h"


typedef struct {
	Datum bytecode;
	Datum signature;		// a formula
	Datum registers;		// a list
} BytecodeFixture;


/**
 * Example program 1, computing $1 = 2*@1 + @1
 * This program tests use of parameters, registers and constants.
 * 
 * number @INT triple $INT
 * #1:INT = 0
 *   COPY @1 $2
 * 	 COPY @1 #1
 *   MUL 2 #1    // multiply with constant
 *   ADD #1 $2
 *   YIELD
 */

BytecodeFixture setupBytecodeFixture1(void)
{
	BytecodeFixture fixture;

	// Bytecode signature
	// TODO: here we assume the form is in canonical order,
	// so that we can refer to in/out parameters by index.
	// Indices in the signature syntax must correspond to this order.
	fixture.signature = CStringToPredicate("number @INT triple $INT");

	// list of registers with initial values
	fixture.registers = CreateListFromArray(
		(Atom []) {CreateInt(0)},
		1
	);

	// create bytecode draft
	BytecodeDraft bytecodeDraft;
	BytecodeBegin(&bytecodeDraft, fixture.signature, fixture.registers);
	
	// add instructions
	// COPY @1 $2
	BytecodeBeginInstruction(&bytecodeDraft, OP_COPY);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_LEFT, 1);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_RIGHT, 2);
	BytecodeEndInstruction(&bytecodeDraft);

	// COPY @1 #1
	BytecodeBeginInstruction(&bytecodeDraft, OP_COPY);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_LEFT, 1);
	BytecodeOperandRegister(&bytecodeDraft, OPERAND_RIGHT, 1);
	BytecodeEndInstruction(&bytecodeDraft);

	// MUL 2 #1
	BytecodeBeginInstruction(&bytecodeDraft, OP_MUL);
	BytecodeOperandConstant(&bytecodeDraft, OPERAND_LEFT, CreateInt(2));
	BytecodeOperandRegister(&bytecodeDraft, OPERAND_RIGHT, 1);
	BytecodeEndInstruction(&bytecodeDraft);

	// ADD #1 @2
	BytecodeBeginInstruction(&bytecodeDraft, OP_ADD);
	BytecodeOperandRegister(&bytecodeDraft, OPERAND_LEFT, 1);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_RIGHT, 2);
	BytecodeEndInstruction(&bytecodeDraft);

	// YIELD
	BytecodeBeginInstruction(&bytecodeDraft, OP_YIELD);
	BytecodeEndInstruction(&bytecodeDraft);

	// finalize bytecode and create atom
	fixture.bytecode = BytecodeEnd(&bytecodeDraft);

	return fixture;
}


static void teardownBytecodeFixture(BytecodeFixture fixture)
{
	IFactRelease(fixture.bytecode);
	IFactRelease(fixture.signature);
	IFactRelease(fixture.registers);
}


void testBytecodeProgram1(void)
{
	BytecodeFixture fixture = setupBytecodeFixture1();
	
	ASSERT_TRUE(IsBytecode(fixture.bytecode))
	// ASSERT_TRUE(SameAtoms(BytecodeGetSignature(fixture.bytecode), fixture.signature))

	ASSERT_DATA64_EQUAL(BytecodeGetRegisters(fixture.bytecode), fixture.registers)

	Datum program = BytecodeGetProgram(fixture.bytecode);
	ASSERT_UINT32_EQUAL(ListLength(program), 5);

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
	ASSERT_UINT32_EQUAL(
		InstructionGetOpCode(ListGetElement(program, 5)),
		OP_YIELD
	)

	teardownBytecodeFixture(fixture);
}


void testExecuteByteCode1(void)
{
	BytecodeFixture fixture = setupBytecodeFixture1();
	PrintFormula(fixture.signature);
	PrintChar('\n');
	
	// NOTE: arguments must be in canonical order
	Datum arguments[2] = {CreateInt(3).datum,  CreateInt(0).datum};
	BytecodeContext * rootContext = VMCreateRootContext(fixture.bytecode, arguments);
	VMExecute(rootContext);
	Datum * results = ContextArguments(rootContext);
	// results should be 3 * 3 
	ASSERT_UINT32_EQUAL(results[1], 9);

	FreeContext(rootContext);

	teardownBytecodeFixture(fixture);
}

/**
 * Example program 2, calling program 1
 * 
 * To call a bytecode program, we first create a context
 * with CTX and then CALL the bytecode service (number triple).
 * 
 * number @INT quadruple $INT
 * #1:INT #2:CONTEXT
 *   COPY   @1 $2
 *   CTX    <number triple> #2		// (number @1 triple #1)
 *   COPY	@1 #2@1					// set @1 in context #2
 *   CALL   #2
 *   COPY	#2$2 #1					// copy output $2 from context #2
 *   ADD    #1 $2
 *   YIELD
 */
BytecodeFixture setupBytecodeFixture2(void)
{
	BytecodeFixture childFixture = setupBytecodeFixture1();
	BytecodeFixture fixture;

	/**
	 * Bytecode signature.
	 * The indices for parameters are determined by their position
	 * in the signature actors list, in canonical order. We here 
	 * assume that the form is in canonical order so the indices are correct.
	 * This can be verified while building the predicate.
	 * 
	 * TODO: handle the case where the same parameter appears in multiple
	 * positions in the signature.
	 */
	fixture.signature = CStringToPredicate("number @INT quadruple $INT");

	// list of register with initial values
	// Registers storing contexts must be initially set to 0
	fixture.registers = CreateListFromArray(
		(Atom []) {CreateInt(0), {.type = DT_CONTEXT, .datum = 0}},
		2
	);

	// create bytecode draft
	BytecodeDraft bytecodeDraft;
	BytecodeBegin(&bytecodeDraft, fixture.signature, fixture.registers);

	// COPY @1 $1
	BytecodeBeginInstruction(&bytecodeDraft, OP_COPY);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_LEFT, 1);	// read from @1
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_RIGHT, 2);	// write to $2
	BytecodeEndInstruction(&bytecodeDraft);

	// CTX <number triple> #2
	BytecodeBeginInstruction(&bytecodeDraft, OP_BCTX);
	// NOTE: do constants really need to be typed?
	BytecodeOperandConstant(&bytecodeDraft, OPERAND_LEFT, CreateID(childFixture.bytecode));
	BytecodeOperandRegister(&bytecodeDraft, OPERAND_RIGHT, 2);
	BytecodeEndInstruction(&bytecodeDraft);

	// COPY	@1 #2@1
	BytecodeBeginInstruction(&bytecodeDraft, OP_COPY);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_LEFT, 1);
	BytecodeOperandSetContext(&bytecodeDraft, OPERAND_RIGHT, 2);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_RIGHT, 1);
	BytecodeEndInstruction(&bytecodeDraft);

	// CALL #2
	BytecodeBeginInstruction(&bytecodeDraft, OP_CALL);
	BytecodeOperandRegister(&bytecodeDraft, OPERAND_LEFT, 2);
	BytecodeEndInstruction(&bytecodeDraft);

	// COPY #2$2 #1
	BytecodeBeginInstruction(&bytecodeDraft, OP_COPY);
	BytecodeOperandSetContext(&bytecodeDraft, OPERAND_LEFT, 2);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_LEFT, 2);
	BytecodeOperandRegister(&bytecodeDraft, OPERAND_RIGHT, 1);
	BytecodeEndInstruction(&bytecodeDraft);

	// ADD  #1 $2
	BytecodeBeginInstruction(&bytecodeDraft, OP_ADD);
	BytecodeOperandRegister(&bytecodeDraft, OPERAND_LEFT, 1);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_RIGHT, 2);
	BytecodeEndInstruction(&bytecodeDraft);

	// YIELD
	BytecodeBeginInstruction(&bytecodeDraft, OP_YIELD);
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
	
	// NOTE: arguments must be in canonical order
	Datum arguments[2] = {CreateInt(3).datum,  CreateInt(0).datum};
	BytecodeContext * rootContext = VMCreateRootContext(fixture.bytecode, arguments);

	VMExecute(rootContext);
	Datum * results = ContextArguments(rootContext);
	ASSERT_UINT32_EQUAL(results[1], 3 * 4);

	FreeContext(rootContext);

	teardownBytecodeFixture(fixture);
}


/**
 * Create a service 
 * TODO: the service needs datum types in order to interface with bytecode
 */
Service setupTableService(void)
{
	// form (foo barbar)
	Datum roles[2] = {CreateNameFromCString("foo"), CreateNameFromCString("bar")};
	Datum form = CreatePredicateForm(roles, 2);
	IFactRelease(roles[0]);
	IFactRelease(roles[1]);
	// create the service
	BTree * btree = CreateRelationBTree(2);
	Service service = RegistryAddBTreeService(form, btree);

	// Assert facts
	Atom actors1[2] = {
		CreateID(CreateStringFromCString("baz")),
		CreateInt(42)
	};
	AssertFact(form, actors1);
	ReleaseAtom(actors1[0]);

	Atom actors2[2]= {
		CreateID(CreateStringFromCString("zzz")),
		CreateInt(-1)
	};
	AssertFact(form, actors2);
	ReleaseAtom(actors2[0]);
	IFactRelease(form);

	return service;
}


void teardownTableService(Service service)
{
	ASSERT(service.type == SERVICE_BTREE)
	
	// TODO: need a way to retract all facts encoded by the service
	
}


/**
 * Example program 3, calling a table service
 * (foo bar)
 * 
 * TODO
 * 
 * foo @ID barbar $INT
 * #1:INT #2:CONTEXT
 *   CTX    <foo bar> #2			// (number @1 triple #1)
 *   COPY	@1 #2@1					// set @1 in context #2
 *   CALL   #2
 *   COPY	#2$2 $2					// copy output $2 from context #2
 *   MUL    2 $2
 *   YIELD
* ... 
 */
BytecodeFixture setupBytecodeFixture3(void)
{
	// setup bytecode fixture
	BytecodeFixture fixture;
	fixture.signature = CStringToPredicate("foo @ID barbar $INT");

	// list of register with initial values
	// Registers storing contexts must be initially set to 0
	fixture.registers = CreateListFromArray(
		(Atom []) {CreateInt(0), {.type = DT_CONTEXT, .datum = 0}},
		2
	);

	// create bytecode draft
	BytecodeDraft bytecodeDraft;
	BytecodeBegin(&bytecodeDraft, fixture.signature, fixture.registers);

	BytecodeBeginInstruction(&bytecodeDraft, OP_BCTX);
	// TODO: BytecodeOperandConstant(&bytecodeDraft, OPERAND_LEFT, );
	BytecodeOperandRegister(&bytecodeDraft, OPERAND_RIGHT, 2);
	BytecodeEndInstruction(&bytecodeDraft);

	// COPY	@1 #2@1
	BytecodeBeginInstruction(&bytecodeDraft, OP_COPY);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_LEFT, 1);
	BytecodeOperandSetContext(&bytecodeDraft, OPERAND_RIGHT, 2);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_RIGHT, 1);
	BytecodeEndInstruction(&bytecodeDraft);

	// CALL #2
	BytecodeBeginInstruction(&bytecodeDraft, OP_CALL);
	BytecodeOperandRegister(&bytecodeDraft, OPERAND_LEFT, 2);
	BytecodeEndInstruction(&bytecodeDraft);

	// COPY #2$2 $2
	BytecodeBeginInstruction(&bytecodeDraft, OP_COPY);
	BytecodeOperandSetContext(&bytecodeDraft, OPERAND_LEFT, 2);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_LEFT, 2);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_RIGHT, 2);
	BytecodeEndInstruction(&bytecodeDraft);

	// MUL 2 $2
	BytecodeBeginInstruction(&bytecodeDraft, OP_MUL);
	BytecodeOperandConstant(&bytecodeDraft, OPERAND_LEFT, CreateInt(2));
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_RIGHT, 2);
	BytecodeEndInstruction(&bytecodeDraft);

	// YIELD
	BytecodeBeginInstruction(&bytecodeDraft, OP_YIELD);
	BytecodeEndInstruction(&bytecodeDraft);

	// finalize bytecode and create atom
	fixture.bytecode = BytecodeEnd(&bytecodeDraft);

	return fixture;
}


void testExecuteBytecode3(void)
{
	Service tableService = setupTableService();
	BytecodeFixture fixture = setupBytecodeFixture3();

	// Do stuff

	teardownTableService(tableService);
	teardownBytecodeFixture(fixture);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testBytecodeProgram1);
	ExecuteTest(testExecuteByteCode1);
	ExecuteTest(testExecuteByteCode2);

	TestSummary();

	KernelShutdown();
}
