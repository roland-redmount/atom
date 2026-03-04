
#include "datumtypes/Variable.h"
#include "kernel/pair.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "lang/PredicateForm.h"


Atom CreatePair(Atom left, Atom right)
{
	IFactDraft draft;
	IFactBegin(&draft);
	AddPairToIFact(&draft, left, right);
	return IFactEnd(&draft);
}


void AddPairToIFact(IFactDraft * draft, Atom left, Atom right)
{
	// assert (pair left right) fact
	Atom form = GetCorePredicateForm(FORM_PAIR_LEFT_RIGHT);

	index8 pairIndex = GetPredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_PAIR);
	index8 leftIndex = GetPredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_LEFT);
	index8 rightIndex = GetPredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_RIGHT);

	IFactBeginConjunction(draft, form, pairIndex);
	
	Atom tuple[3];
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


static void getPairTuple(Atom pair, Atom * tuple)
{
	BTree * tree = RegistryGetCoreTable(FORM_PAIR_LEFT_RIGHT);

	Atom query[3];
	index8 pairIndex = GetPredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_PAIR);
	index8 leftIndex = GetPredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_LEFT);
	index8 rightIndex = GetPredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_RIGHT);

	query[pairIndex] = pair;
	query[leftIndex] = anonymousVariable;
	query[rightIndex] = anonymousVariable;

	RelationBTreeQuerySingle(tree, query, tuple);
}


Atom PairGetElement(Atom pair, uint8 element)
{
	Atom pairTuple[3];
	getPairTuple(pair, pairTuple);
	switch(element) {
	case PAIR_LEFT:
		return pairTuple[GetPredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_LEFT)];

	case PAIR_RIGHT:
		return pairTuple[GetPredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_RIGHT)];

	default:
		ASSERT(false);
		return invalidAtom;		// dummy
	}
}


void PrintPair(Atom pair)
{
	Atom pairTuple[3];
	getPairTuple(pair, pairTuple);
	PrintChar('[');
	PrintAtom(pairTuple[GetPredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_LEFT)]);
	PrintChar(' ');
	PrintAtom(pairTuple[GetPredicateRoleIndex(FORM_PAIR_LEFT_RIGHT, ROLE_RIGHT)]);
	PrintChar(']');
}

