
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



Datum CreatePredicateForm(Datum const * roles, size8 nRoles)
{
	// reduce to unique roles, typed for use with multiset
	Atom uniqueRoles[nRoles];
	for(index8 i = 0; i < nRoles; i++)
		uniqueRoles[i] = (Atom) {.type = DT_NAME, .datum = roles[i]};
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


bool IsPredicateForm(Datum atom)
{
	// special case for (multiset element multiple) form, for bootstrapping
	if(atom == GetCorePredicateForm(FORM_MULTISET_ELEMENT_MULTIPLE))
		return true;

	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_PREDICATE_FORM),
		GetCoreRoleName(ROLE_PREDICATE_FORM)
	);
}


size8 PredicateNRoles(Datum predicateForm)
{
	return MultisetNUniqueElements(predicateForm);
}


size8 PredicateArity(Datum predicateForm)
{
	return MultisetSize(predicateForm);
}


// TODO: this could use MultisetIterationOrder() instead
index8 PredicateRoleIndex(Datum predicateForm, Datum role)
{
	ASSERT(IsPredicateForm(predicateForm));
	MultisetIterator iterator;
	MultisetIterate(predicateForm, &iterator);

	index8 index = 0;
	bool found = false;
	while(MultisetIteratorHasNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		if(elementMultiple.element.datum == role) {
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


void PrintPredicateForm(Datum predicateForm)
{	
	MultisetIterator iterator;
	MultisetIterate(predicateForm, &iterator);

	PrintChar('(');
	while(MultisetIteratorHasNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < elementMultiple.multiple; j++) {
			PrintName(elementMultiple.element.datum);
			PrintChar(' ');
		}
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
	PrintChar(')');
}

