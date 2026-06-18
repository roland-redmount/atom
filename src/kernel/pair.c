
#include "lang/Variable.h"
#include "kernel/pair.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "lang/PredicateForm.h"

static void pairSetTuple(TypedTuple * tuple, TypedAtom pair, TypedAtom left, TypedAtom right)
{
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_PAIR),
		pair
	);
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_LEFT),
		left
	);
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_RIGHT),
		right
	);
}

Atom CreatePair(TypedAtom left, TypedAtom right)
{
	IFactDraft draft;
	IFactBegin(&draft);
	AddPairToIFact(&draft, left, right);
	return IFactEnd(&draft);
}


void AddPairToIFact(IFactDraft * draft, TypedAtom left, TypedAtom right)
{
	// assert (pair left right)
	IFactBeginConjunction(
		draft,
		GetCorePredicateForm(FORM_PAIR_LEFT_RIGHT),
		RegistryGetCoreBTreeService(FORM_PAIR_LEFT_RIGHT),
		CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_PAIR)
	);
	
	TypedTuple * tuple = CreateTypedTuple(3);
	pairSetTuple(tuple, (TypedAtom) {0}, left, right);
	IFactAddTuple(draft, tuple);
	FreeTypedTuple(tuple);
	IFactEndPredicateForm(draft);
}


bool IsPair(Atom atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_PAIR_LEFT_RIGHT),
		GetCoreRoleName(ROLE_PAIR)
	);
}


static void getPairTuple(Atom pair, TypedTuple * tuple)
{
	BTree * tree = RegistryGetCoreBTreeService(FORM_PAIR_LEFT_RIGHT);

	TypedTuple * query = CreateTypedTuple(3);
	pairSetTuple(
		query,
		CreateTypedAtom(AT_ID, pair), anonymousVariable, anonymousVariable
	);
	RelationBTreeQuerySingle(tree, query, tuple);
	FreeTypedTuple(query);
}


TypedAtom PairGetElement(Atom pair, uint8 element)
{
	TypedTuple * tuple = CreateTypedTuple(3);
	getPairTuple(pair, tuple);
	TypedAtom result;
	switch(element) {
	case PAIR_LEFT:
		result = TypedTupleGetElement(
			tuple,
			CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_LEFT)
		);
		break;

	case PAIR_RIGHT:
		result = TypedTupleGetElement(
			tuple,
			CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_RIGHT)
		);
		break;

	default:
		result = invalidAtom;
		ASSERT(false);
	}
	FreeTypedTuple(tuple);
	return result;
}


void PrintPair(Atom pair)
{
	TypedTuple * tuple = CreateTypedTuple(3);
	getPairTuple(pair, tuple);
	PrintChar('[');
	TypedAtom left = TypedTupleGetElement(
		tuple,
		CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_LEFT)
	);
	PrintTypedAtom(left);
	PrintChar(' ');
	TypedAtom right = TypedTupleGetElement(
		tuple,
		CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_RIGHT)
	);
	PrintTypedAtom(right);
	PrintChar(']');
	FreeTypedTuple(tuple);
}

