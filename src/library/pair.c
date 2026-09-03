
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "lang/name.h"
#include "lang/formula.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "library/pair.h"
#include "storage/RelationBTree.h"


static Atom pairPredicateForm;
static Atom pairTermForm;

// the role names (pair left right)
static Atom pairRoleNames[3];

// the canonical order indexes for roles (pair left right)
static index8 pairTermRoleIndex[3];

static Relation const * pairRelation;
static RelationTable * pairRelationTable;
static Operator * pairOperator;


static void pairSetTuple(Atom tuple[], Atom pair, Atom left, Atom right)
{
	tuple[pairTermRoleIndex[0]] = pair;
	tuple[pairTermRoleIndex[1]] = left;
	tuple[pairTermRoleIndex[2]] = right;
}

// This is assert (pair * left <left> right <right)
Atom CreatePair(Atom left, Atom right)
{
	IFactDraft draft;
	IFactBegin(&draft);
	AddPairToIFact(&draft, left, right);
	return IFactEnd(&draft);
}


void AddPairToIFact(IFactDraft * draft, Atom left, Atom right)
{
	// assert (pair left right)
	IFactBeginConjunction(draft, pairRelationTable, pairTermRoleIndex[0]);
	
	Atom tuple[3];
	pairSetTuple(tuple, (Atom) {0}, left, right);
	IFactAddTuple(draft, tuple);
	IFactEndConjunction(draft);
}


bool IsPair(Atom atom)
{
	return AtomHasRole(atom, pairRelation, pairRoleNames[0]);
}


static void getPairTuple(Atom pair, Atom tuple[])
{
	tuple[pairTermRoleIndex[0]] = pair;

	OperatorContext * context = OperatorCreateContext(pairOperator, tuple);

	ASSERT(OperatorCall(context));
	OperatorFreeContext(context);
}


Atom PairGetElement(Atom pair, uint8 element)
{
	Atom tuple[3];
	getPairTuple(pair, tuple);
	switch(element) {
	case PAIR_LEFT:
		return tuple[pairTermRoleIndex[1]];

	case PAIR_RIGHT:
		return tuple[pairTermRoleIndex[2]];

	default:
		ASSERT(false);
		return (Atom) {0};
	}
}


void PrintPair(Atom pair)
{
	Atom tuple[3];
	getPairTuple(pair, tuple);
	PrintChar('[');
	IFactPrint(tuple[pairTermRoleIndex[1]]);	// left atom
	PrintChar(' ');
	IFactPrint(tuple[pairTermRoleIndex[2]]);	// right atom
	PrintChar(']');
}


void PairSetup(void)
{
	pairRoleNames[0] = CreateNameFromCString("pair");
	pairRoleNames[1] = CreateNameFromCString("left");
	pairRoleNames[2] = CreateNameFromCString("right");

	// Create the (pair left right) predicate form
	pairPredicateForm = CreatePredicateForm(pairRoleNames,	3);
	for(index8 i = 0; i < 3; i++)
		pairTermRoleIndex[i] = PredicateRoleIndex(pairPredicateForm, pairRoleNames[i]);
	for(index8 i = 0; i < 3; i++)
		NameRelease(pairRoleNames[i]);
	// Create the (pair left right) term form
	pairTermForm = CreateTermForm(pairPredicateForm, true);
	IFactRelease(pairPredicateForm);
	
	// Create the (pair:ID left:ID right:ID) relation, with B-tree provider
	TypeSignature typeSignature = {0};
	CopyBytesPermuted(
		(byte[]) {AT_ID, AT_ID, AT_ID}, typeSignature.atomTypes, pairTermRoleIndex, 3);		
	pairRelation = CreateRelation(pairTermForm, 3, typeSignature);
	IFactRelease(pairTermForm);
	
	pairRelationTable = CreateRelationTable(pairRelation, &btreeStorageProvider, pairTermRoleIndex);
	ReleaseRelation(pairRelation);

	// Store a pointer to the (pair<ID left>ID right<ID) service,
	// created by the B-tree provider.
	IOSignature ioSignature = {0};
	CopyBytesPermuted(
		(byte[]) {PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT},
		ioSignature.parameterIO, pairTermRoleIndex, 3);
	pairOperator = ServiceRegistryFind(pairRelation, ioSignature);
	ASSERT(pairOperator);
}


void PairShutdown(void)
{
	ReleaseRelationTable(pairRelationTable);
}
