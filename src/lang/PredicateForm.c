
#include "kernel/UInt.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/multiset.h"
#include "lang/TypedAtom.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "util/utilities.h"



Atom CreatePredicateForm(Atom const * roles, size8 nRoles)
{
	// reduce to unique roles, typed for use with multiset
	TypedAtom uniqueRoles[nRoles];
	for(index8 i = 0; i < nRoles; i++)
		uniqueRoles[i] = (TypedAtom) {.type = AT_NAME, .atom = roles[i]};
	SortTypedAtoms(uniqueRoles, nRoles);
	uint32 multiplicities[nRoles];
	size8 nUniqueRoles = ReduceTypedAtomsArray(uniqueRoles, multiplicities, nRoles);

	IFactDraft draft;
	IFactBegin(&draft);

	AddMultisetToIFactFromArrays(&draft, uniqueRoles, multiplicities, nUniqueRoles);

	// add (predicate-form @predicate) to ifact
	IFactBeginConjunction(
		&draft,
		GetCorePredicateForm(FORM_PREDICATE_FORM),
		(byte[]) {AT_ID},
		0
	);
	IFactAddTuple(&draft, (Atom[]) {(Atom) {0}});
	IFactEndPredicateForm(&draft);

	return IFactEnd(&draft);
}


bool IsPredicateForm(Atom atom)
{
	// special case for (multiset element multiple) form, for bootstrapping
	if(atom.hash == GetCorePredicateForm(FORM_MULTISET_ELEMENT_MULTIPLE).hash)
		return true;

	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_PREDICATE_FORM),
		GetCoreRoleName(ROLE_PREDICATE_FORM)
	);
}


size8 PredicateNRoles(Atom predicateForm)
{
	return MultisetNUniqueElements(predicateForm);
}


size8 PredicateArity(Atom predicateForm)
{
	return MultisetSize(predicateForm);
}


index8 PredicateRoleIndex(Atom predicateForm, Atom roleName)
{
	ASSERT(IsPredicateForm(predicateForm));
	MultisetIterator iterator;
	MultisetIterate(predicateForm, &iterator);

	index8 index = 0;
	bool found = false;
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		if(elementMultiple.element.atom.hash == roleName.hash) {
			found = true;
			break;
		}
		index += elementMultiple.multiple;
	}
	MultisetIteratorEnd(&iterator);
	ASSERT(found);
	return index;
}


void PrintPredicateForm(Atom predicateForm)
{	
	ASSERT(IsPredicateForm(predicateForm))
	MultisetIterator iterator;
	MultisetIterate(predicateForm, &iterator);

	PrintChar('(');
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < elementMultiple.multiple; j++) {
			PrintName(elementMultiple.element.atom);
			PrintChar(' ');
		}
	}
	MultisetIteratorEnd(&iterator);
	PrintChar(')');
}

