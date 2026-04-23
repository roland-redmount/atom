
#include "kernel/Parameter.h"
#include "lang/Variable.h"
#include "kernel/UInt.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/list.h"
#include "kernel/lookup.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "parser/PredicateBuilder.h"
#include "vm/bytecode.h"


// (bytecode program)
static void setBytecodeProgram(IFactDraft * draft, Atom program)
{
	index8 bytecodeIndex = CorePredicateRoleIndex(FORM_BYTECODE_PROGRAM, ROLE_BYTECODE);
	index8 programIndex = CorePredicateRoleIndex(FORM_BYTECODE_PROGRAM, ROLE_PROGRAM);

	IFactBeginConjunction(
		draft,
		GetCorePredicateForm(FORM_BYTECODE_PROGRAM),
		RegistryGetCoreTable(FORM_BYTECODE_PROGRAM),
		bytecodeIndex
	);
	Tuple * tuple = CreateTuple(2);
	TupleSetElement(tuple, programIndex, CreateTypedAtom(AT_ID, program));
	IFactAddClause(draft, tuple);
	FreeTuple(tuple);
	IFactEndConjunction(draft);
}


// (bytecode parameters)
static void setBytecodeParameters(IFactDraft * draft, Atom parametersList)
{
	index8 bytecodeIndex = CorePredicateRoleIndex(FORM_BYTECODE_PARAMETERS, ROLE_BYTECODE);
	index8 parametersIndex = CorePredicateRoleIndex(FORM_BYTECODE_PARAMETERS, ROLE_PARAMETERS);

	IFactBeginConjunction(
		draft,
		GetCorePredicateForm(FORM_BYTECODE_PARAMETERS),
		RegistryGetCoreTable(FORM_BYTECODE_PARAMETERS),
		bytecodeIndex
	);
	Tuple * tuple = CreateTuple(2);
	TupleSetElement(tuple, parametersIndex, CreateTypedAtom(AT_ID, parametersList));
	IFactAddClause(draft, tuple);
	FreeTuple(tuple);
	IFactEndConjunction(draft);
}


// (bytecode registers)
static void setBytecodeRegisters(IFactDraft * draft, Atom registersList)
{
	index8 bytecodeIndex = CorePredicateRoleIndex(FORM_BYTECODE_REGISTERS, ROLE_BYTECODE);
	index8 registersIndex = CorePredicateRoleIndex(FORM_BYTECODE_REGISTERS, ROLE_REGISTERS);

	IFactBeginConjunction(
		draft,
		GetCorePredicateForm(FORM_BYTECODE_REGISTERS),
		RegistryGetCoreTable(FORM_BYTECODE_REGISTERS),
		bytecodeIndex
	);
	Tuple * tuple = CreateTuple(2);
	TupleSetElement(tuple, registersIndex, CreateTypedAtom(AT_ID, registersList));
	IFactAddClause(draft, tuple);
	FreeTuple(tuple);
	IFactEndConjunction(draft);
}


// (bytecode constants)
static void setBytecodeConstants(IFactDraft * draft, Atom constantsList)
{
	index8 bytecodeIndex = CorePredicateRoleIndex(FORM_BYTECODE_CONSTANTS, ROLE_BYTECODE);
	index8 constantsIndex = CorePredicateRoleIndex(FORM_BYTECODE_CONSTANTS, ROLE_CONSTANTS);

	IFactBeginConjunction(
		draft,
		GetCorePredicateForm(FORM_BYTECODE_CONSTANTS),
		RegistryGetCoreTable(FORM_BYTECODE_CONSTANTS),
		bytecodeIndex
	);
	Tuple * tuple = CreateTuple(2);
	TupleSetElement(tuple, constantsIndex, CreateTypedAtom(AT_ID, constantsList));
	IFactAddClause(draft, tuple);
	FreeTuple(tuple);
	IFactEndConjunction(draft);
}

/**
 * TODO: For the instructions list, we want to use
 * a dense array implementation for performance when executing bytecode.
 * If we let the form (list position element) be implemented by multiple storage
 * engines (B-tree, dense array, ...) then we need to query across all of them.
 * That is a fair amount of added complexity. List iteration must then gather
 * facts across storage engines  ...  We should probably specify in the query
 * to only search array-based storage (we would know this in advance).
 * 
 * For now we stick with the B-tree list implementation, but we should revisit this.
 */

void BytecodeBegin(BytecodeDraft * draft, Atom parameters, Atom registers)
{
	ASSERT(IsList(parameters));
	draft->parameters = parameters;
	ASSERT(IsList(registers));
	draft->registers = registers;

	// Draft lists, modified when adding instructions
	ListBegin(&(draft->constantsDraft));
	ListBegin(&(draft->programDraft));
}


void BytecodeBeginInstruction(BytecodeDraft * draft, byte opcode)
{
	InstructionBegin(&(draft->instructionDraft), opcode);
}


void BytecodeOperandParameter(BytecodeDraft * draft, Operand operand, index8 index)
{
	InstructionSetOperand(&(draft->instructionDraft), operand, index, ACCESS_PARAMETER);
}


void BytecodeOperandRegister(BytecodeDraft * draft, Operand operand, index8 registerIndex)
{
	ASSERT(registerIndex <= ListLength(draft->registers))
	InstructionSetOperand(&(draft->instructionDraft), operand, registerIndex, ACCESS_REGISTER);
}


void BytecodeOperandConstant(BytecodeDraft * draft, Operand operand, TypedAtom constant)
{
	// TODO: constants should be a set
	index8 position = ListAddElement(&(draft->constantsDraft), constant);
	InstructionSetOperand(&(draft->instructionDraft), operand, position, ACCESS_CONSTANT);
}


void BytecodeOperandSetContext(BytecodeDraft * draft, Operand operand, index8 registerIndex)
{
	InstructionSetContext(&(draft->instructionDraft), operand, registerIndex);
}



void BytecodeEndInstruction(BytecodeDraft * draft)
{
	Atom instruction = InstructionEnd(&(draft->instructionDraft));
	ListAddElement(&(draft->programDraft), CreateTypedAtom(AT_INSTRUCTION, instruction));
}


Atom BytecodeEnd(BytecodeDraft * draft)
{
	IFactDraft bytecodeDraft;
	IFactBegin(&bytecodeDraft);

	// (bytecode parameters)
	setBytecodeParameters(&bytecodeDraft, draft->parameters);

	// (bytecode registers)
	setBytecodeRegisters(&bytecodeDraft, draft->registers);

	// (bytecode program)
	Atom program = ListEnd(&(draft->programDraft));
	setBytecodeProgram(&bytecodeDraft, program);

	// (bytecode constants)
	Atom constants = ListEnd(&(draft->constantsDraft));
	setBytecodeConstants(&bytecodeDraft, constants);

	Atom bytecode = IFactEnd(&bytecodeDraft);
	IFactRelease(program);
	IFactRelease(constants);
	SetMemory(draft, sizeof(BytecodeDraft), 0);

	return bytecode;
}


bool IsBytecode(Atom atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_BYTECODE_PROGRAM),
		GetCoreRoleName(ROLE_BYTECODE)
	);
}


static Atom bytecodeGetProperty(
	Atom bytecode, index32 formId, index32 propertyRoleId)
{
	BTree * tree = RegistryGetCoreTable(formId);
	index8 bytecodeIndex = CorePredicateRoleIndex(formId, ROLE_BYTECODE);
	index8 propertyIndex = CorePredicateRoleIndex(formId, propertyRoleId);

	Tuple * query = CreateTuple(2);
	TupleSetElement(query, bytecodeIndex, CreateTypedAtom(AT_ID, bytecode));
	TupleSetElement(query, propertyIndex, anonymousVariable);

	TypedAtom property = RelationBTreeQuerySingleAtom(tree, query, propertyIndex);
	FreeTuple(query);
	return property.atom;
}


Atom BytecodeGetProgram(Atom bytecode)
{
	return bytecodeGetProperty(bytecode, FORM_BYTECODE_PROGRAM, ROLE_PROGRAM);
}


Atom BytecodeGetParameters(Atom bytecode)
{
	return bytecodeGetProperty(bytecode, FORM_BYTECODE_PARAMETERS, ROLE_PARAMETERS);
}


Atom BytecodeGetRegisters(Atom bytecode)
{
	return bytecodeGetProperty(bytecode, FORM_BYTECODE_REGISTERS, ROLE_REGISTERS);
}


Atom BytecodeGetConstants(Atom bytecode)
{
	return bytecodeGetProperty(bytecode, FORM_BYTECODE_CONSTANTS, ROLE_CONSTANTS);
}


// TODO: temporary solution to be able to locate
// and remove the service (+ + =) to test for memory leaks.
// In practise we would persist these services, not remove them.

static Atom additionService;


/**
 * The service (+ x>INT + y>INT = z<INT)
 */
static void createAdditionService(void)
{
	Atom signature = CStringToPredicate("= $INT + @INT + @INT");
	PrintFormula(signature);
	PrintChar('\n');
	Atom form = FormulaGetForm(signature);
	Atom parameters = FormulaGetActors(signature);

	Atom registers = CreateListFromArray(0, 0);		// the empty list

	// create bytecode draft
	BytecodeDraft bytecodeDraft;
	BytecodeBegin(&bytecodeDraft, parameters, registers);
	
	// COPY @2 $1
	BytecodeBeginInstruction(&bytecodeDraft, OP_COPY);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_LEFT, 2);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_RIGHT, 1);
	BytecodeEndInstruction(&bytecodeDraft);

	// ADD @3 $1
	BytecodeBeginInstruction(&bytecodeDraft, OP_ADD);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_LEFT, 3);
	BytecodeOperandParameter(&bytecodeDraft, OPERAND_RIGHT, 1);
	BytecodeEndInstruction(&bytecodeDraft);

	Atom bytecode = BytecodeEnd(&bytecodeDraft);
	IFactRelease(registers);

	// TODO: create the service properly
	additionService = RegistryAddBytecodeService(form, bytecode);
	IFactRelease(signature);
	IFactRelease(bytecode);
}


/**
 * TODO: This should become part of the "standard library" of services.
 * This is not "core" services in the sense of being required for the system to function.
 */
void SetupServiceLibrary(void)
{
	createAdditionService();
}


void TeardownCoreServices(void)
{
	RegistryRemoveService(additionService);
}


void PrintBytecode(Atom bytecode)
{	
	Atom program = BytecodeGetProgram(bytecode);
	PrintF("Bytecode[%u instructions]", ListLength(program));
}
