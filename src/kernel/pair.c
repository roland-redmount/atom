
#include "datumtypes/Variable.h"
#include "kernel/pair.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "lang/PredicateForm.h"


Atom CreatePair(TypedAtom left, TypedAtom right)
{
	IFactDraft draft;
	IFactBegin(&draft);
	AddPairToIFact(&draft, left, right);
	return IFactEnd(&draft);
}


void AddPairToIFact(IFactDraft * draft, TypedAtom left, TypedAtom right)
{
	// assert (pair left right) fact
	Atom form = GetCorePredicateForm(FORM_PAIR_LEFT_RIGHT);

	index8 pairIndex = CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_PAIR);
	index8 leftIndex = CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_LEFT);
	index8 rightIndex = CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_RIGHT);

	IFactBeginConjunction(draft, form, pairIndex);
	
	TypedAtom tuple[3];
	tuple[leftIndex] = left;
	tuple[rightIndex] = right;
	IFactAddClause(draft, tuple);
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


static void getPairTuple(Atom pair, TypedAtom * tuple)
{
	BTree * tree = RegistryGetCoreTable(FORM_PAIR_LEFT_RIGHT);

	TypedAtom query[3];
	index8 pairIndex = CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_PAIR);
	index8 leftIndex = CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_LEFT);
	index8 rightIndex = CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_RIGHT);

	query[pairIndex] = CreateTypedAtom(DT_ID, pair);
	query[leftIndex] = anonymousVariable;
	query[rightIndex] = anonymousVariable;

	RelationBTreeQuerySingle(tree, query, tuple);
}


TypedAtom PairGetElement(Atom pair, uint8 element)
{
	TypedAtom pairTuple[3];
	getPairTuple(pair, pairTuple);
	switch(element) {
	case PAIR_LEFT:
		return pairTuple[CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_LEFT)];

	case PAIR_RIGHT:
		return pairTuple[CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_RIGHT)];

	default:
		ASSERT(false);
		return invalidAtom;		// dummy
	}
}


void PrintPair(Atom pair)
{
	TypedAtom pairTuple[3];
	getPairTuple(pair, pairTuple);
	PrintChar('[');
	PrintTypedAtom(pairTuple[CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_LEFT)]);
	PrintChar(' ');
	PrintTypedAtom(pairTuple[CorePredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_RIGHT)]);
	PrintChar(']');
}

