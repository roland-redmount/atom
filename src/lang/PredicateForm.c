
#include "kernel/UInt.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/multiset.h"
#include "kernel/tuple.h"
#include "lang/Atom.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "util/utilities.h"


Atom CreatePredicateForm(Atom const roles[], size8 nRoles)
{
	// reduce to unique roles, typed for use with multiset
	Atom uniqueRoles[nRoles];
	TupleCopy(roles, uniqueRoles, nRoles);
	SortAtoms(uniqueRoles, nRoles);
	uint32 multiplicities[nRoles];
	size8 nUniqueRoles = ReduceAtomsArray(uniqueRoles, multiplicities, nRoles);

	IFactDraft draft;
	IFactBegin(&draft);

	AddMultisetToIFactFromArrays(&draft, uniqueRoles, multiplicities, nUniqueRoles, AT_NAME);

	// add (predicate-form @predicate) to ifact
	RelationTable const * predicateFormTable = GetCoreRelationTable(RELATION_PREDICATE_FORM);
	IFactBeginConjunction(&draft, predicateFormTable, 0);
	IFactAddTuple(&draft, (Atom[]) {(Atom) {0}});
	IFactEndConjunction(&draft);

	return IFactEnd(&draft);
}


bool IsPredicateForm(Atom atom)
{
	// special case for (multiset element multiple) form, for bootstrapping
	if(atom.hash == GetCorePredicateForm(FORM_MULTISET_ELEMENT_MULTIPLE).hash)
		return true;

	return AtomHasRole(
		atom,
		GetCoreRelationTable(RELATION_PREDICATE_FORM),
		GetCoreRoleName(ROLE_PREDICATE_FORM)
	);
}


size8 PredicateNRoles(Atom predicateForm)
{
	return MultisetNUniqueElements(predicateForm, AT_NAME);
}


size8 PredicateArity(Atom predicateForm)
{
	return MultisetSize(predicateForm, AT_NAME);
}


index8 PredicateRoleIndex(Atom predicateForm, Atom roleName)
{
	ASSERT(IsPredicateForm(predicateForm));
	MultisetIterator iterator;
	MultisetIterate(predicateForm, AT_NAME, &iterator);

	index8 index = 0;
	bool found = false;
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		if(elementMultiple.element.hash == roleName.hash) {
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
	MultisetIterate(predicateForm, AT_NAME, &iterator);

	PrintChar('(');
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < elementMultiple.multiple; j++) {
			PrintName(elementMultiple.element);
			PrintChar(' ');
		}
	}
	MultisetIteratorEnd(&iterator);
	PrintChar(')');
}

