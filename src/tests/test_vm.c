
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
	Atom registers;		// a list
	Atom service;
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


static void teardownBytecodeFixture1(BytecodeServiceFixture fixture)
{
	RegistryRemoveService(fixture.service);
	IFactRelease(fixture.bytecode);
	IFactRelease(fixture.registers);
}


void testBytecodeProgram1(void)
{
	BytecodeServiceFixture fixture = setupBytecodeFixture1();
	ServiceRecord record = RegistryGetServiceRecord(fixture.service);

	ASSERT_INT32_EQUAL(record.type, SERVICE_BYTECODE)
	ASSERT_TRUE(IsPredicateForm(record.form))
	ASSERT_TRUE(IsList(record.parameters))
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

	teardownBytecodeFixture1(fixture);
}


void testExecuteByteCode1(void)
{
	BytecodeServiceFixture fixture = setupBytecodeFixture1();
	ServiceRecord record = RegistryGetServiceRecord(fixture.service);
	PrintPredicateForm(record.form);
	PrintChar('\n');
	
	// NOTE: arguments must be in canonical order
	Atom arguments[2] = {CreateInt(3).atom,  CreateInt(0).atom};
	BytecodeContext * rootContext = VMCreateRootContext(&record, arguments);
	VMExecute(rootContext);
	Atom * results = ContextArguments(rootContext);
	// results should be 3 * 3 
	ASSERT_UINT32_EQUAL(results[1], 9);

	FreeContext(rootContext);

	teardownBytecodeFixture1(fixture);
}

typedef struct {
	BytecodeServiceFixture childFixture;
	Atom bytecode;
	Atom registers;		// a list
	Atom service;
} BytecodeServiceFixture2;


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
BytecodeServiceFixture2 setupBytecodeFixture2(void)
{
	BytecodeServiceFixture2 fixture;
	fixture.childFixture = setupBytecodeFixture1();

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
	// TODO: the AT_SERVICE is not reference counted, so the ServiceRegistry
	// has no way of knowing if there exist bytecode methods calling it ...
	BytecodeOperandConstant(
		&bytecodeDraft, OPERAND_LEFT,
		CreateTypedAtom(AT_SERVICE, fixture.childFixture.service)
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

	// create service
	Atom signature = CStringToPredicate("number @INT quadruple $INT");
	fixture.service = RegistryAddBytecodeService(
		signature,
		fixture.bytecode
	);
	IFactRelease(signature);
	return fixture;
}


static void teardownBytecodeFixture2(BytecodeServiceFixture2 fixture)
{
	RegistryRemoveService(fixture.service);
	IFactRelease(fixture.bytecode);
	IFactRelease(fixture.registers);
	teardownBytecodeFixture1(fixture.childFixture);
}


void testExecuteByteCode2(void)
{
	BytecodeServiceFixture2 fixture = setupBytecodeFixture2();
	ServiceRecord record = RegistryGetServiceRecord(fixture.service);
	PrintPredicateForm(record.form);
	PrintChar('\n');
	
	// NOTE: arguments must be in canonical order
	Atom arguments[2] = {CreateInt(3).atom,  CreateInt(0).atom};
	BytecodeContext * rootContext = VMCreateRootContext(&record, arguments);

	VMExecute(rootContext);
	Atom * results = ContextArguments(rootContext);
	ASSERT_UINT32_EQUAL(results[1], 3 * 4);

	FreeContext(rootContext);

	teardownBytecodeFixture2(fixture);
}


/**
 * Create a relation table (B-tree) service 
 */
Atom setupTableService(void)
{
	// form (foo barbar)
	Atom roles[2] = {CreateNameFromCString("foo"), CreateNameFromCString("bar")};
	Atom form = CreatePredicateForm(roles, 2);
	IFactRelease(roles[0]);
	IFactRelease(roles[1]);
	// create the service
	BTree * btree = CreateRelationBTree(2);
	Atom service = RegistryAddBTreeService(form, btree);

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


void teardownTableService(Atom service)
{
	ServiceRecord record = RegistryGetServiceRecord(service);
	ASSERT(record.type == SERVICE_BTREE)
	// Retracting the last fact of this form will remove the service
	RetractAllFacts(record.form);
}


/**
 * Example program 3, calling a B-tree service (foo bar).
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
	Atom tableService = setupTableService();
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

	BytecodeBeginInstruction(&bytecodeDraft, OP_BCTX);
	BytecodeOperandConstant(
		&bytecodeDraft, OPERAND_LEFT,
		CreateTypedAtom(AT_SERVICE, tableService)
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
	Atom tableService = setupTableService();
	BytecodeServiceFixture fixture = setupBytecodeFixture3();

	// Do stuff

	teardownTableService(tableService);
	teardownBytecodeFixture1(fixture);
}


int main(int argc, char * argv[])
{
	KernelInitialize();

	ExecuteTest(testBytecodeProgram1);
	ExecuteTest(testExecuteByteCode1);
	ExecuteTest(testExecuteByteCode2);
	ExecuteTest(testExecuteBytecode3);

	TestSummary();

	KernelShutdown();
}
