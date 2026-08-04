
#include "lang/Variable.h"
#include "kernel/UInt.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "lang/TermForm.h"


static void termFormSetTuple(Atom * tuple, Atom termForm, Atom predicateForm, Atom sign)
{
	tuple[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_TERM_FORM)] = termForm;
	tuple[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_PREDICATE_FORM)] = predicateForm;
	tuple[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_SIGN)] = sign;
}


Atom CreateTermForm(Atom predicateForm, bool sign)
{
	IFactDraft draft;
	IFactBegin(&draft);

	RelationTable const * termFormTable = GetCoreRelationTable(RELATION_TERM_FORM);
	IFactBeginConjunction(&draft, termFormTable, CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_TERM_FORM));
	Atom tuple[3];
	// TODO: make this a kernel function CoreFormSetTuple()
	termFormSetTuple(tuple, (Atom) {0}, predicateForm, (Atom) {._uint = sign ? 1 : 0});
	IFactAddTuple(&draft, tuple);
	IFactEndConjunction(&draft);

	return IFactEnd(&draft);
}


bool IsTermForm(Atom atom)
{
	return AtomHasRole(
		atom,
		GetCoreRelationTable(RELATION_TERM_FORM),
		GetCoreRoleName(ROLE_TERM_FORM)
	);
}

// Retrieve the (unique) tuple from the (term-form predicate-form sign) relation
// matching the given term form atom
static void termFormGetTuple(Atom termForm, Atom tuple[])
{
	Service const * service = GetCoreService(SERVICE_TERM_FORM);
	CoreFormSetTuple(
		FORM_TERM_FORM,
		(Atom[]) {termForm, (Atom) {0}, (Atom) {0}},
		tuple
	);
	ServiceCallOnce(service, tuple);
}

Atom TermFormGetPredicateForm(Atom termForm)
{
	Atom result[3];
	termFormGetTuple(termForm, result);
	return result[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_PREDICATE_FORM)];
}


bool TermFormGetSign(Atom termForm)
{
	Atom result[3];
	termFormGetTuple(termForm, result);
	Atom sign = result[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_SIGN)];
	return (sign._uint == 1);
}


void PrintTermForm(Atom termForm)
{	
	if(!TermFormGetSign(termForm))
		PrintChar('!');
	PrintPredicateForm(TermFormGetPredicateForm(termForm));
}


size8 TermFormArity(Atom termForm)
{
	return PredicateArity(TermFormGetPredicateForm(termForm));
}
