
#include "lang/Variable.h"
#include "kernel/UInt.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/pair.h"
#include "kernel/Parameter.h"
#include "kernel/ServiceRegistry.h"
#include "lang/TermForm.h"


static void termFormSetTuple(Atom * tuple, Atom termForm, Atom predicateForm, Atom sign)
{
	tuple[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_TERM_FORM)] = termForm;
	tuple[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_PREDICATE_FORM)] = predicateForm;
	tuple[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_SIGN)] = sign;
}

/**
 * Construct a parameter list for the (term-form predicate-form sign) table
 * with the given I/O for each parameter
 */
// static void termFormSetParameters(Atom * parameters, byte termIO, byte predicateIO, byte signIO)
// {
// 	index8 termIndex = CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_TERM_FORM);
// 	parameters[termIndex] = (Atom) {.parameter = {.number = termIndex + 1, .atomType = AT_ID, .io = termIO}};

// 	index8 predicateIndex = CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_PREDICATE_FORM);
// 	parameters[predicateIndex] = (Atom) {.parameter = {.number = predicateIndex + 1, .atomType = AT_ID, .io = predicateIO}};

// 	index8 signIndex = CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_SIGN);
// 	parameters[signIndex] = (Atom) {.parameter = {.number = signIndex + 1, .atomType = AT_ID, .io = signIO}};
// }


Atom CreateTermForm(Atom predicateForm, bool sign)
{
	IFactDraft draft;
	IFactBegin(&draft);

	RelationTable termFormTable = GetCoreRelationTable(FORM_TERM_FORM);
	IFactBeginConjunction(&draft, &termFormTable, CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_TERM_FORM));
	Atom tuple[3];
	termFormSetTuple(tuple, termFormTable.form, predicateForm, (Atom) {._uint = sign ? 1 : 0});
	IFactAddTuple(&draft, tuple);
	IFactEndConjunction(&draft);

	return IFactEnd(&draft);
}


bool IsTermForm(Atom atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_TERM_FORM),
		GetCoreRoleName(ROLE_TERM_FORM)
	);
}


void termFormGetTuple(Atom termForm, Atom result[])
{
	// NOTE: we could do this with a service call but it is rather involved
	// Atom parameters[3];
	// termFormSetParameters(parameters, PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT);
	// Service const * service = RegistryFindService(
	// 	GetCorePredicateForm(FORM_TERM_FORM),
	// 	parameters
	// );
	// ServiceContext * context = ServiceCreateContext(service, query);

	RelationTable termFormTable = FindRelationTable(
		GetCorePredicateForm(FORM_TERM_FORM),
		GetCorePredicateAtomTypes(FORM_TERM_FORM)
	);

	Atom query[3];
	termFormSetTuple(query, termForm, (Atom) {0}, (Atom) {0});
	// TODO: this assumes the term-form role is column position 1
	// We would need B-tree tables to maintain a specific column order,
	// or explicity specify the index columns (which is the same thing)
	RelationBTreeQuerySingle(&termFormTable, query, 1, result);	
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
	Atom sign = result[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_PREDICATE_FORM)];
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
