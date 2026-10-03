
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/tuple.h"
#include "lang/Atom.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "util/utilities.h"


/**
 * Returns true if the sorted unique roles are exactly the core roles given by roleIds,
 * each occurring once.
 */
static bool hasCoreRoles(
	Atom const uniqueRoles[], uint32 const multiplicities[], size8 nUniqueRoles,
	index32 const roleIds[], size8 nRoleIds)
{
	if(nUniqueRoles != nRoleIds)
		return false;
	Atom coreRoles[nRoleIds];
	for(index8 i = 0; i < nRoleIds; i++)
		coreRoles[i] = GetCoreRoleName(roleIds[i]);
	SortAtoms(coreRoles, nRoleIds);
	for(index8 i = 0; i < nRoleIds; i++) {
		if((multiplicities[i] != 1) || !SameAtoms(uniqueRoles[i], coreRoles[i]))
			return false;
	}
	return true;
}


/**
 * Return the bootstrapped predicate form with the given sorted unique roles,
 * or the zero atom if there is none.
 */
static Atom findBootstrapPredicateForm(
	Atom const uniqueRoles[], uint32 const multiplicities[], size8 nUniqueRoles)
{
	static index32 const multisetRoleIds[] = {ROLE_MULTISET, ROLE_ELEMENT, ROLE_MULTIPLE};
	static index32 const predicateFormRoleIds[] = {ROLE_PREDICATE_FORM};

	if(hasCoreRoles(uniqueRoles, multiplicities, nUniqueRoles, multisetRoleIds, 3))
		return GetCorePredicateForm(FORM_MULTISET_ELEMENT_MULTIPLE);
	if(hasCoreRoles(uniqueRoles, multiplicities, nUniqueRoles, predicateFormRoleIds, 1))
		return GetCorePredicateForm(FORM_PREDICATE_FORM);
	return (Atom) {0};
}


Atom CreatePredicateForm(Atom const roles[], size8 nRoles)
{
	// reduce to unique roles, typed for use with multiset
	Atom uniqueRoles[nRoles];
	TupleCopy(roles, uniqueRoles, nRoles);
	SortAtoms(uniqueRoles, nRoles);
	uint32 multiplicities[nRoles];
	size8 nUniqueRoles = ReduceAtomsArray(uniqueRoles, multiplicities, nRoles);

	// Check for a bootstrapped predicate form, which has a fixed hash
	Atom bootstrapForm = findBootstrapPredicateForm(uniqueRoles, multiplicities, nUniqueRoles);
	if(bootstrapForm.hash) {
		IFactAcquire(bootstrapForm);
		return bootstrapForm;
	}

	IFactDraft draft;
	IFactBegin(&draft);

	AddMultisetToIFactFromArrays(&draft, uniqueRoles, multiplicities, nUniqueRoles, AT_NAME);

	// add (predicate-form @predicate) to ifact
	TupleStore * predicateFormTupleStore = GetCoreTupleStore(RELATION_PREDICATE_FORM);
	IFactBeginConjunction(&draft, predicateFormTupleStore, 0);
	IFactAddTuple(&draft, (Atom[]) {(Atom) {0}});
	IFactEndConjunction(&draft);

	return IFactEnd(&draft);
}


bool IsPredicateForm(Atom atom)
{
	// special case for (multiset element multiple) form, for bootstrapping
	if(SameAtoms(atom, GetCorePredicateForm(FORM_MULTISET_ELEMENT_MULTIPLE)))
		return true;

	Operator * op = GetCoreOperator(SERVICE_PREDICATE_FORM);
	Atom arguments[1] = {atom};
	return OperatorCallOnce(op, arguments);
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
		if(SameAtoms(elementMultiple.element, roleName)) {
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

