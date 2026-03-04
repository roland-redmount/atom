
#include "datumtypes/UInt.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/multiset.h"
#include "lang/Atom.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "util/utilities.h"



Atom CreatePredicateForm(Atom const * roles, size8 nRoles)
{
	// reduce to unique roles
	Atom uniqueRoles[nRoles];
	CopyMemory(roles, uniqueRoles, nRoles * sizeof(Atom));
	SortAtoms(uniqueRoles, nRoles);
	uint32 multiplicities[nRoles];
	size8 nUniqueRoles = ReduceAtomArray(uniqueRoles, multiplicities, nRoles);

	IFactDraft draft;
	IFactBegin(&draft);

	AddMultisetToIFactFromArrays(&draft, uniqueRoles, multiplicities, nUniqueRoles);

	// add (predicate-form @predicate) to ifact
	IFactBeginConjunction(
		&draft,
		GetCorePredicateForm(FORM_PREDICATE_FORM),
		0
	);
	Atom form = invalidAtom;
	IFactAddClause(&draft, &form);
	IFactEndConjunction(&draft);

	return IFactEnd(&draft);
}


bool IsPredicateForm(Atom atom)
{
	// special case for (multiset element multiple) form, for bootstrapping
	if(atom.datum == GetCorePredicateForm(FORM_MULTISET_ELEMENT_MULTIPLE).datum)
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


// TODO: this could use MultisetIterationOrder() instead
index8 PredicateRoleIndex(Atom predicateForm, Atom role)
{
	ASSERT(role.type == DT_NAME)
	MultisetIterator iterator;
	MultisetIterate(predicateForm, &iterator);

	index8 index = 0;
	bool found = false;
	while(MultisetIteratorHasNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		if(SameAtoms(elementMultiple.element, role)) {
			found = true;
			break;
		}
		index += elementMultiple.multiple;
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
	ASSERT(found);
	return index;
}


void PrintPredicateForm(Atom predicateForm)
{	
	MultisetIterator iterator;
	MultisetIterate(predicateForm, &iterator);

	PrintChar('(');
	while(MultisetIteratorHasNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < elementMultiple.multiple; j++) {
			PrintName(elementMultiple.element);
			PrintChar(' ');
		}
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
	PrintChar(')');
}

