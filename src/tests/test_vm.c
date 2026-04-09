
#include "datumtypes/Parameter.h"
#include "datumtypes/instruction.h"
#include "datumtypes/Int.h"
#include "datumtypes/Variable.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/string.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "parser/PredicateBuilder.h"
#include "vm/bytecode.h"
#include "vm/vm.h"

#include "testing/testing.h"


typedef struct {
	Atom bytecode;
	// Atom form;
	// Atom parameters;
	Atom registers;		// a list
	Service service;
} BytecodeServiceFixture;


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

BytecodeServiceFixture setupBytecodeFixture1(void)
{
	BytecodeServiceFixture fixture;

	// list of registers with initial values
	fixture.registers = CreateListFromArray(
		(TypedAtom []) {CreateInt(0)},
		1
	);

	// create bytecode draft
	BytecodeDraft bytecodeDraft;
	BytecodeBegin(&bytecodeDraft, fixture.registers);
	
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
	// NOTE: do constants really need to be typed?
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

	// Bytecode signature
	// TODO: find some better way to initalize this
	Atom signature = CStringToPredicate("number @INT triple $INT");

	// create service
	fixture.service = RegistryAddBytecodeService(
		signature, fixture.bytecode
	);
	IFactRelease(signature);
	return fixture;
}


static void teardownBytecodeFixture(BytecodeServiceFixture fixture)
{
	RegistryRemoveService(fixture.service);
	IFactRelease(fixture.bytecode);
	IFactRelease(fixture.registers);
}


void testBytecodeProgram1(void)
{
	BytecodeServiceFixture fixture = setupBytecodeFixture1();
	ASSERT_INT32_EQUAL(fixture.service.type, SERVICE_BYTECODE)
	ASSERT_TRUE(IsPredicateForm(fixture.service.form))
	ASSERT_TRUE(IsList(fixture.service.parameters))
	ASSERT_TRUE(IsBytecode(fixture.bytecode))
	ASSERT_TRUE(IsList(fixture.registers))
	
	ASSERT_DATA64_EQUAL(BytecodeGetRegisters(fixture.bytecode), fixture.registers)

	Atom program = BytecodeGetProgram(fixture.bytecode);
	ASSERT_TRUE(IsList(program))
	ASSERT_UINT32_EQUAL(ListLength(program), 5)

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
	BytecodeServiceFixture fixture = setupBytecodeFixture1();
	PrintPredicateForm(fixture.service.form);
	PrintChar('\n');
	
	// NOTE: arguments must be in canonical order
	Atom arguments[2] = {CreateInt(3).atom,  CreateInt(0).atom};
	BytecodeContext * rootContext = VMCreateRootContext(&fixture.service, arguments);
	VMExecute(rootContext);
	Atom * results = ContextArguments(rootContext);
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
BytecodeServiceFixture setupBytecodeFixture2(void)
{
	BytecodeServiceFixture childFixture = setupBytecodeFixture1();
	BytecodeServiceFixture fixture;

	// list of register with initial values
	// Registers storing contexts must be initially set to 0
	fixture.registers = CreateListFromArray(
		(TypedAtom []) {
			CreateInt(0),
			CreateTypedAtom(AT_CONTEXT, 0)
		},
		2
	);

	// create bytecode draft
	BytecodeDraft bytecodeDraft;
	BytecodeBegin(&bytecodeDraft, fixture.registers);

	// COPY @1 $1
	BytecodeBeginInstruction(&bytecodeDraft, OP_COPY);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_LEFT, 1);	// read from @1
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_RIGHT, 2);	// write to $2
	BytecodeEndInstruction(&bytecodeDraft);

	// CTX <number triple> #2
	BytecodeBeginInstruction(&bytecodeDraft, OP_BCTX);
	// We could use the service signature (formula), but who keeps the reference?
	BytecodeOperandConstant(
		&bytecodeDraft, OPERAND_LEFT,
		CreateTypedAtom(AT_ID, ServiceCreateSignature(&childFixture.service))
	);
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

	// create service
	Atom signature = CStringToPredicate("number @INT quadruple $INT");
	fixture.service = RegistryAddBytecodeService(
		signature,
		fixture.bytecode
	);
	return fixture;
}


void testExecuteByteCode2(void)
{
	BytecodeServiceFixture fixture = setupBytecodeFixture2();
	PrintPredicateForm(fixture.service.form);
	PrintChar('\n');
	
	// NOTE: arguments must be in canonical order
	Atom arguments[2] = {CreateInt(3).atom,  CreateInt(0).atom};
	BytecodeContext * rootContext = VMCreateRootContext(&fixture.service, arguments);

	VMExecute(rootContext);
	Atom * results = ContextArguments(rootContext);
	ASSERT_UINT32_EQUAL(results[1], 3 * 4);

	FreeContext(rootContext);

	teardownBytecodeFixture(fixture);
}


/**
 * Create a service 
 * TODO: the service needs atom types in order to interface with bytecode
 */
Service setupTableService(void)
{
	// form (foo barbar)
	Atom roles[2] = {CreateNameFromCString("foo"), CreateNameFromCString("bar")};
	Atom form = CreatePredicateForm(roles, 2);
	IFactRelease(roles[0]);
	IFactRelease(roles[1]);
	// create the service
	BTree * btree = CreateRelationBTree(2);
	Service service = RegistryAddBTreeService(form, btree);

	// Assert facts
	TypedAtom actors1[2] = {
		CreateTypedAtom(AT_ID, CreateStringFromCString("baz")),
		CreateInt(42)
	};
	AssertFact(form, actors1);
	ReleaseTypedAtom(actors1[0]);

	TypedAtom actors2[2]= {
		CreateTypedAtom(AT_ID, CreateStringFromCString("zzz")),
		CreateInt(-1)
	};
	AssertFact(form, actors2);
	ReleaseTypedAtom(actors2[0]);
	IFactRelease(form);

	return service;
}


void teardownTableService(Service service)
{
	ASSERT(service.type == SERVICE_BTREE)
	
	// TODO: need a way to retract all facts encoded by the service
	
}


/**
 * Example program 3, calling a B-tree service (foo bar).
 * 
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
BytecodeServiceFixture setupBytecodeFixture3(void)
{
	// setup bytecode fixture
	BytecodeServiceFixture fixture;

	// list of register with initial values
	// Registers storing contexts must be initially set to 0
	fixture.registers = CreateListFromArray(
		(TypedAtom []) {CreateInt(0), {.type = AT_CONTEXT, .atom = 0}},
		2
	);

	// create bytecode draft
	BytecodeDraft bytecodeDraft;
	BytecodeBegin(&bytecodeDraft, fixture.registers);

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

	// create service
	Atom signature = CStringToPredicate("foo @ID barbar $INT");
	fixture.service = RegistryAddBytecodeService(
		signature, fixture.bytecode
	);
	return fixture;
}


void testExecuteBytecode3(void)
{
	Service tableService = setupTableService();
	BytecodeServiceFixture fixture = setupBytecodeFixture3();

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
