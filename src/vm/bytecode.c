
#include "datumtypes/Parameter.h"
#include "datumtypes/register.h"
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
	index8 bytecodeIndex = GetPredicateRoleIndex(FORM_BYTECODE_SIGNATURE, ROLE_BYTECODE);
	index8 signatureIndex = GetPredicateRoleIndex(FORM_BYTECODE_SIGNATURE, ROLE_SIGNATURE);

	IFactBeginConjunction(draft, GetCorePredicateForm(FORM_BYTECODE_SIGNATURE), bytecodeIndex);
	Atom tuple[2];
	tuple[signatureIndex] = signature;
	IFactAddClause(draft, tuple);
	IFactEndConjunction(draft);
}


static void setBytecodeProgram(IFactDraft * draft, Atom program)
{
	index8 bytecodeIndex = GetPredicateRoleIndex(FORM_BYTECODE_PROGRAM, ROLE_BYTECODE);
	index8 programIndex = GetPredicateRoleIndex(FORM_BYTECODE_PROGRAM, ROLE_PROGRAM);

	IFactBeginConjunction(draft, GetCorePredicateForm(FORM_BYTECODE_PROGRAM), bytecodeIndex);
	Atom tuple[2];
	tuple[programIndex] = program;
	IFactAddClause(draft, tuple);
	IFactEndConjunction(draft);
}


// (bytecode registers)
static void setBytecodeRegisters(IFactDraft * draft, Atom registersList)
{
	index8 bytecodeIndex = GetPredicateRoleIndex(FORM_BYTECODE_REGISTERS, ROLE_BYTECODE);
	index8 registersIndex = GetPredicateRoleIndex(FORM_BYTECODE_REGISTERS, ROLE_REGISTERS);

	IFactBeginConjunction(draft, GetCorePredicateForm(FORM_BYTECODE_REGISTERS), bytecodeIndex);
	Atom tuple[2];
	tuple[registersIndex] = registersList;
	IFactAddClause(draft, tuple);
	IFactEndConjunction(draft);
}


static void setBytecodeConstants(IFactDraft * draft, Atom constantsList)
{
	index8 bytecodeIndex = GetPredicateRoleIndex(FORM_BYTECODE_CONSTANTS, ROLE_BYTECODE);
	index8 constantsIndex = GetPredicateRoleIndex(FORM_BYTECODE_CONSTANTS, ROLE_CONSTANTS);

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
	draft->callCounter = 0;

	// draft lists, instructions can add elements
	ListBegin(&(draft->constantsDraft));
	ListBegin(&(draft->programDraft));
}


void BytecodeBeginInstruction(BytecodeDraft * draft, byte opcode)
{
	InstructionBegin(&(draft->instructionDraft), opcode);
}


void BytecodeAddOperand(BytecodeDraft * draft, BytecodeArgument argument)
{
	switch(argument.type) {
		case ARG_CONSTANT:
		BytecodeOperandConstant(draft, argument.value.constant);
		break;

		case ARG_PARAMETER:
		BytecodeOperandParameter(draft, argument.value.parameter);
		break;

		case ARG_REGISTER:
		BytecodeOperandRegister(draft, argument.value.registerIndex);
		break;
	}
}


void BytecodeOperandParameter(BytecodeDraft * draft, Atom parameter)
{
	index8 position = ListGetPosition(
		FormulaGetActors(draft->signature),
		parameter
	);
	ASSERT(position > 0)
	InstructionOperandArgument(&(draft->instructionDraft), position);
}


void BytecodeOperandRegister(BytecodeDraft * draft, index8 registerIndex)
{
	ASSERT(registerIndex <= ListLength(draft->registers))
	InstructionOperandRegister(&(draft->instructionDraft), registerIndex);
}


void BytecodeOperandConstant(BytecodeDraft * draft, Atom constant)
{
	// TODO: constants should be a set
	index8 position = ListAddElement(&(draft->constantsDraft), constant);
	InstructionOperandConstant(&(draft->instructionDraft), position);
}


void BytecodeEndInstruction(BytecodeDraft * draft)
{
	Atom instruction = InstructionEnd(&(draft->instructionDraft));
	ListAddElement(&(draft->programDraft), instruction);
}


void BytecodeGenerateCall(BytecodeDraft * draft, Atom bytecode, Atom query)
{
	ASSERT(IsBytecode(bytecode))
	ASSERT(IsFormula(query))
	Atom actorsList = FormulaGetActors(query);
	size8 arity = ListLength(actorsList);

	// generate push instructions for arguments, in reverse order
	for(index8 i = 0; i < arity; i++) {
		BytecodeBeginInstruction(draft, OP_PUSH);
		Atom actor = ListGetElement(actorsList, arity - i);
		switch(actor.type) {
			case DT_PARAMETER:
			BytecodeOperandParameter(draft, actor);
			break;

			case DT_REGISTER:
			BytecodeOperandRegister(draft, RegisterGetIndex(actor));
			break;

			default:
			// any other datum is a bytecode constant
			// NOTE: this acquires the constant atom, as we add it to a list
			BytecodeOperandConstant(draft, actor);
			break;
		}
		BytecodeEndInstruction(draft);
	}

	// generate CALL instruction
	BytecodeBeginInstruction(draft, OP_CALL);
	BytecodeOperandConstant(draft, bytecode);
	// Set the child context index.
	// NOTE: this is pretty ugly ...
	draft->instructionDraft.fields.op2 = ++draft->callCounter;
	BytecodeEndInstruction(draft);
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
	index8 bytecodeIndex = GetPredicateRoleIndex(FORM_BYTECODE_PROGRAM, ROLE_BYTECODE);
	index8 programIndex = GetPredicateRoleIndex(FORM_BYTECODE_PROGRAM, ROLE_PROGRAM);

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
	index8 bytecodeIndex = GetPredicateRoleIndex(FORM_BYTECODE_SIGNATURE, ROLE_BYTECODE);
	index8 signatureIndex = GetPredicateRoleIndex(FORM_BYTECODE_SIGNATURE, ROLE_SIGNATURE);

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
	index8 bytecodeIndex = GetPredicateRoleIndex(FORM_BYTECODE_REGISTERS, ROLE_BYTECODE);
	index8 registersIndex = GetPredicateRoleIndex(FORM_BYTECODE_REGISTERS, ROLE_REGISTERS);

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
	index8 bytecodeIndex = GetPredicateRoleIndex(FORM_BYTECODE_CONSTANTS, ROLE_BYTECODE);
	index8 constantsIndex = GetPredicateRoleIndex(FORM_BYTECODE_CONSTANTS, ROLE_CONSTANTS);

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
		if(InstructionGetOpCode(inst) == OP_CALL)
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
/*
	Atom plus = CreateNameFromCString("+");
	Atom equals = CreateNameFromCString("=");
	Atom form = CreatePredicateForm((Atom []) {plus, plus, equals}, 3);

	// TODO: this will not in general match the form order.
	// we need a way to create formulas correctly without parsing strings.
	Atom actors = CreateListFromArray((Atom []) {x, y, z}, 3);
	Atom signature = CreateFormula(form, actors);
*/

	Atom signature = CStringToPredicate("+ $x<INT + $y<INT = $z>INT");
	PrintFormula(signature);
	PrintChar('\n');
	Atom x = CreateParameter('x', PARAMETER_IN, DT_INT);
	Atom y = CreateParameter('y', PARAMETER_IN, DT_INT);
	Atom z = CreateParameter('z', PARAMETER_OUT, DT_INT);

	Atom registers = CreateListFromArray(0, 0);		// the empty list

	// create bytecode draft
	BytecodeDraft bytecodeDraft;
	BytecodeBegin(&bytecodeDraft, signature, registers);
	
	// COPY x z
	BytecodeBeginInstruction(&bytecodeDraft, OP_COPY);
	BytecodeOperandParameter(&bytecodeDraft, x);
	BytecodeOperandParameter(&bytecodeDraft, z);
	BytecodeEndInstruction(&bytecodeDraft);

	// ADD y z
	BytecodeBeginInstruction(&bytecodeDraft, OP_ADD);
	BytecodeOperandParameter(&bytecodeDraft, y);
	BytecodeOperandParameter(&bytecodeDraft, z);
	BytecodeEndInstruction(&bytecodeDraft);

	Atom bytecode = BytecodeEnd(&bytecodeDraft);
	ReleaseAtom(signature);
	ReleaseAtom(registers);

	// NOTE: the form is now both a registry key and part of the bytecode definition
	additionService = RegistryAddBytecodeService(bytecode);
	ReleaseAtom(bytecode);
}


void SetupCoreServices(void)
{
	createAdditionService();
}


void TeardownCoreServices(void)
{
	RegistryRemoveService(additionService.form);
}
