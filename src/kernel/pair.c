
#include "lang/Variable.h"
#include "kernel/pair.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "lang/PredicateForm.h"

static void pairSetTuple(Tuple * tuple, TypedAtom pair, TypedAtom left, TypedAtom right)
{
	TupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_PAIR),
		pair
	);
	TupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_LEFT),
		left
	);
	TupleSetElement(
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
		RegistryGetCoreTable(FORM_PAIR_LEFT_RIGHT),
		CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_PAIR)
	);
	
	Tuple * tuple = CreateTuple(3);
	pairSetTuple(tuple, (TypedAtom) {0}, left, right);
	IFactAddClause(draft, tuple);
	FreeTuple(tuple);
	IFactEndConjunction(draft);
}


bool IsPair(Atom atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_PAIR_LEFT_RIGHT),
		GetCoreRoleName(ROLE_PAIR)
	);
}


static void getPairTuple(Atom pair, Tuple * tuple)
{
	BTree * tree = RegistryGetCoreTable(FORM_PAIR_LEFT_RIGHT);

	Tuple * query = CreateTuple(3);
	pairSetTuple(
		query,
		CreateTypedAtom(AT_ID, pair), anonymousVariable, anonymousVariable
	);
	RelationBTreeQuerySingle(tree, query, tuple);
	FreeTuple(query);
}


TypedAtom PairGetElement(Atom pair, uint8 element)
{
	Tuple * tuple = CreateTuple(3);
	getPairTuple(pair, tuple);
	TypedAtom result;
	switch(element) {
	case PAIR_LEFT:
		result = TupleGetElement(
			tuple,
			CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_LEFT)
		);
		break;

	case PAIR_RIGHT:
		result = TupleGetElement(
			tuple,
			CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_RIGHT)
		);
		break;

	default:
		ASSERT(false);
	}
	FreeTuple(tuple);
	return result;
}


void PrintPair(Atom pair)
{
	Tuple * tuple = CreateTuple(3);
	getPairTuple(pair, tuple);
	PrintChar('[');
	TypedAtom left = TupleGetElement(
		tuple,
		CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_LEFT)
	);
	PrintTypedAtom(left);
	PrintChar(' ');
	TypedAtom right = TupleGetElement(
		tuple,
		CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_RIGHT)
	);
	PrintTypedAtom(right);
	PrintChar(']');
	FreeTuple(tuple);
}

