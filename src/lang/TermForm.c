
#include "lang/Variable.h"
#include "kernel/ifact.h"
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "lang/TermForm.h"


static void termFormSetTuple(Atom * tuple, Atom termForm, Atom predicateForm, Atom sign)
{
	tuple[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_TERM_FORM)] = termForm;
	tuple[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_PREDICATE_FORM)] = predicateForm;
	tuple[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_SIGN)] = sign;
}


/* CLAUDE: The positive term forms of (multiset element multiple), (predicate-form) and
   (term-form predicate-form sign) are created during bootstrap with a fixed hash, as for
   findBootstrapPredicateForm() in PredicateForm.c. Return the bootstrapped term form for the
   predicate form and sign, or the zero atom if there is none. */
static Atom findBootstrapTermForm(Atom predicateForm, bool sign)
{
	static index32 const bootstrapFormIds[] = {
		FORM_MULTISET_ELEMENT_MULTIPLE, FORM_PREDICATE_FORM, FORM_TERM_FORM
	};
	if(!sign)
		return (Atom) {0};
	for(index8 i = 0; i < 3; i++) {
		if(SameAtoms(predicateForm, GetCorePredicateForm(bootstrapFormIds[i])))
			return GetCoreTermForm(bootstrapFormIds[i]);
	}
	return (Atom) {0};
}


Atom CreateTermForm(Atom predicateForm, bool sign)
{
	// CLAUDE: a bootstrapped term form has a fixed hash; see findBootstrapTermForm()
	Atom bootstrapForm = findBootstrapTermForm(predicateForm, sign);
	if(bootstrapForm.hash) {
		IFactAcquire(bootstrapForm);
		return bootstrapForm;
	}

	IFactDraft draft;
	IFactBegin(&draft);

	TupleStore * termFormTupleStore = GetCoreTupleStore(RELATION_TERM_FORM);
	IFactBeginConjunction(&draft, termFormTupleStore, CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_TERM_FORM));
	Atom tuple[3];
	// TODO: make this a kernel function CoreFormSetTuple()
	termFormSetTuple(tuple, (Atom) {0}, predicateForm, (Atom) {._int = sign ? 1 : 0});
	IFactAddTuple(&draft, tuple);
	IFactEndConjunction(&draft);

	return IFactEnd(&draft);
}


bool IsTermForm(Atom atom)
{
	Operator * op = GetCoreOperator(SERVICE_TERM_FORM);
	Atom arguments[3];
	CoreFormSetTuple(
		FORM_TERM_FORM,
		(Atom[]) {atom, (Atom) {0}, (Atom) {0}},
		arguments
	);
	return OperatorCallOnce(op, arguments);
}


Atom TermFormCreateOppositeForm(Atom termForm)
{
	bool sign = TermFormGetSign(termForm);
	return CreateTermForm(TermFormGetPredicateForm(termForm), !sign);	
}


// Retrieve the (unique) tuple from the (term-form predicate-form sign) relation
// matching the given term form atom
static void termFormGetTuple(Atom termForm, Atom tuple[])
{
	Operator const * op = GetCoreOperator(SERVICE_TERM_FORM);
	CoreFormSetTuple(
		FORM_TERM_FORM,
		(Atom[]) {termForm, (Atom) {0}, (Atom) {0}},
		tuple
	);
	OperatorCallOnce(op, tuple);
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
	return (sign._int == 1);
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
