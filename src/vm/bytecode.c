
#include "datumtypes/Parameter.h"
#include "datumtypes/Variable.h"
#include "datumtypes/UInt.h"
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


static void setBytecodeSignature(IFactDraft * draft, Atom signature)
{
	index8 bytecodeIndex = CorePredicateRoleIndex(FORM_BYTECODE_SIGNATURE, ROLE_BYTECODE);
	index8 signatureIndex = CorePredicateRoleIndex(FORM_BYTECODE_SIGNATURE, ROLE_SIGNATURE);

	IFactBeginConjunction(draft, GetCorePredicateForm(FORM_BYTECODE_SIGNATURE), bytecodeIndex);
	Atom tuple[2];
	tuple[signatureIndex] = signature;
	IFactAddClause(draft, tuple);
	IFactEndConjunction(draft);
}


static void setBytecodeProgram(IFactDraft * draft, Atom program)
{
	index8 bytecodeIndex = CorePredicateRoleIndex(FORM_BYTECODE_PROGRAM, ROLE_BYTECODE);
	index8 programIndex = CorePredicateRoleIndex(FORM_BYTECODE_PROGRAM, ROLE_PROGRAM);

	IFactBeginConjunction(draft, GetCorePredicateForm(FORM_BYTECODE_PROGRAM), bytecodeIndex);
	Atom tuple[2];
	tuple[programIndex] = program;
	IFactAddClause(draft, tuple);
	IFactEndConjunction(draft);
}


// (bytecode registers)
static void setBytecodeRegisters(IFactDraft * draft, Atom registersList)
{
	index8 bytecodeIndex = CorePredicateRoleIndex(FORM_BYTECODE_REGISTERS, ROLE_BYTECODE);
	index8 registersIndex = CorePredicateRoleIndex(FORM_BYTECODE_REGISTERS, ROLE_REGISTERS);

	IFactBeginConjunction(draft, GetCorePredicateForm(FORM_BYTECODE_REGISTERS), bytecodeIndex);
	Atom tuple[2];
	tuple[registersIndex] = registersList;
	IFactAddClause(draft, tuple);
	IFactEndConjunction(draft);
}


static void setBytecodeConstants(IFactDraft * draft, Atom constantsList)
{
	index8 bytecodeIndex = CorePredicateRoleIndex(FORM_BYTECODE_CONSTANTS, ROLE_BYTECODE);
	index8 constantsIndex = CorePredicateRoleIndex(FORM_BYTECODE_CONSTANTS, ROLE_CONSTANTS);

	IFactBeginConjunction(draft, GetCorePredicateForm(FORM_BYTECODE_CONSTANTS), bytecodeIndex);
	Atom tuple[2];
	tuple[constantsIndex] = constantsList;
	IFactAddClause(draft, tuple);
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

void BytecodeBegin(BytecodeDraft * draft, Atom signature, Atom registers)
{
	ASSERT(IsFormula(signature));
	draft->signature = signature;
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


void BytecodeOperandConstant(BytecodeDraft * draft, Operand operand, Atom constant)
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
	ListAddElement(&(draft->programDraft), instruction);
}


Atom BytecodeEnd(BytecodeDraft * draft)
{
	IFactDraft bytecodeDraft;
	IFactBegin(&bytecodeDraft);

	// (bytecode signature)
	setBytecodeSignature(&bytecodeDraft, draft->signature);

	// (bytecode registers)
	setBytecodeRegisters(&bytecodeDraft, draft->registers);

	// (bytecode program)
	Atom program = ListEnd(&(draft->programDraft));
	setBytecodeProgram(&bytecodeDraft, program);

	// (bytecode constants)
	Atom constants = ListEnd(&(draft->constantsDraft));
	setBytecodeConstants(&bytecodeDraft, constants);

	Atom bytecode = IFactEnd(&bytecodeDraft);
	ReleaseAtom(program);
	ReleaseAtom(constants);
	SetMemory(draft, sizeof(BytecodeDraft), 0);
	return bytecode;
}


bool IsBytecode(Atom atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_BYTECODE_SIGNATURE),
		GetCoreRoleName(ROLE_BYTECODE)
	);
}


Atom BytecodeGetProgram(Atom bytecode)
{
	BTree * tree = RegistryGetCoreTable(FORM_BYTECODE_PROGRAM);
	index8 bytecodeIndex = CorePredicateRoleIndex(FORM_BYTECODE_PROGRAM, ROLE_BYTECODE);
	index8 programIndex = CorePredicateRoleIndex(FORM_BYTECODE_PROGRAM, ROLE_PROGRAM);

	Atom query[2];
	query[bytecodeIndex] = bytecode;
	query[programIndex] = anonymousVariable;

	Atom tuple[2];
	RelationBTreeQuerySingle(tree, query, tuple);
	return tuple[programIndex];
}


Atom BytecodeGetSignature(Atom bytecode)
{
	BTree * tree = RegistryGetCoreTable(FORM_BYTECODE_SIGNATURE);
	index8 bytecodeIndex = CorePredicateRoleIndex(FORM_BYTECODE_SIGNATURE, ROLE_BYTECODE);
	index8 signatureIndex = CorePredicateRoleIndex(FORM_BYTECODE_SIGNATURE, ROLE_SIGNATURE);

	Atom query[2];
	query[bytecodeIndex] = bytecode;
	query[signatureIndex] = anonymousVariable;

	Atom tuple[2];
	RelationBTreeQuerySingle(tree, query, tuple);
	return tuple[signatureIndex];
}


Atom BytecodeGetRegisters(Atom bytecode)
{
	BTree * tree = RegistryGetCoreTable(FORM_BYTECODE_REGISTERS);
	index8 bytecodeIndex = CorePredicateRoleIndex(FORM_BYTECODE_REGISTERS, ROLE_BYTECODE);
	index8 registersIndex = CorePredicateRoleIndex(FORM_BYTECODE_REGISTERS, ROLE_REGISTERS);

	Atom query[2];
	query[bytecodeIndex] = bytecode;
	query[registersIndex] = anonymousVariable;

	Atom tuple[2];
	RelationBTreeQuerySingle(tree, query, tuple);
	return tuple[registersIndex];
}


Atom BytecodeGetConstants(Atom bytecode)
{
	BTree * tree = RegistryGetCoreTable(FORM_BYTECODE_CONSTANTS);
	index8 bytecodeIndex = CorePredicateRoleIndex(FORM_BYTECODE_CONSTANTS, ROLE_BYTECODE);
	index8 constantsIndex = CorePredicateRoleIndex(FORM_BYTECODE_CONSTANTS, ROLE_CONSTANTS);

	Atom query[2];
	query[bytecodeIndex] = bytecode;
	query[constantsIndex] = anonymousVariable;

	Atom tuple[2];
	RelationBTreeQuerySingle(tree, query, tuple);
	return tuple[constantsIndex];
}


/**
 * Compute the number of child contexts by iterating over the program
 * and counting CALL instructions. We could also precompute this at time of
 * creating the bytecode and assert a fact.
 */
size32 BytecodeNChildContexts(Atom bytecode)
{
	size32 callCount = 0;

	Atom program = BytecodeGetProgram(bytecode);
	ListIterator iterator;
	ListIterate(program, &iterator);
	while(ListIteratorHasNext(&iterator)) {
		Atom inst = ListIteratorGetElement(&iterator);
		if(InstructionGetOpCode(inst) == OP_EXEC)
			callCount++;
		ListIteratorNext(&iterator);
	}
	ListIteratorEnd(&iterator);
	return callCount;
}


// TODO: temporary solution to be able to locate
//  and remove the service (+ + =) to avoid memory leaks.
//  We need a systematic way of deallocating all services,
//  and dismantling the system in general.

static Service additionService;


/**
 * The service (+ x>INT + y>INT = z<INT)
 */
static void createAdditionService(void)
{
	Atom signature = CStringToPredicate("= $INT + @INT + @INT");
	PrintFormula(signature);
	PrintChar('\n');

	Atom registers = CreateListFromArray(0, 0);		// the empty list

	// create bytecode draft
	BytecodeDraft bytecodeDraft;
	BytecodeBegin(&bytecodeDraft, signature, registers);
	
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
	ReleaseAtom(signature);
	ReleaseAtom(registers);

	// NOTE: the form is now both a registry key and part of the bytecode definition
	additionService = RegistryAddBytecodeService(bytecode);
	ReleaseAtom(bytecode);
}


/**
 * TODO: This should become part of the "standard library" of services.
 * This is not "core" services in the sense of being required for the system to function.
 */
void SetupCoreServices(void)
{
	createAdditionService();
}


void TeardownCoreServices(void)
{
	RegistryRemoveService(additionService.form);
}
