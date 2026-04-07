
#include "datumtypes/Variable.h"
#include "datumtypes/UInt.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/pair.h"
#include "kernel/ServiceRegistry.h"
#include "lang/TermForm.h"


void TermFormSetTuple(TypedAtom * tuple, TypedAtom termForm, TypedAtom predicateForm, TypedAtom sign)
{
	tuple[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_TERM_FORM)] = termForm;
	tuple[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_PREDICATE_FORM)] = predicateForm;
	tuple[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_SIGN)] = sign;
}

/**
 * The term form is negated if sign is false
 */
Atom CreateTermForm(Atom predicateForm, bool sign)
{
	IFactDraft draft;
	IFactBegin(&draft);

	Atom termForm = GetCorePredicateForm(FORM_TERM_FORM);

	IFactBeginConjunction(
		&draft, termForm,
		CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_TERM_FORM)
	);
	TypedAtom tuple[3];
	TermFormSetTuple(tuple,
		CreateTypedAtom(AT_ID, termForm),	CreateTypedAtom(AT_ID, predicateForm),
		CreateUInt(sign ? 1 : 0)
	);
	IFactAddClause(&draft, tuple);
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


Atom GetPredicateForm(Atom termForm)
{
	BTree * tree = RegistryGetCoreTable(FORM_TERM_FORM);

	TypedAtom query[3];
	TermFormSetTuple(query,
		CreateTypedAtom(AT_ID, termForm), anonymousVariable, anonymousVariable);
	TypedAtom result[3];
	RelationBTreeQuerySingle(tree, query, result);

	return result[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_PREDICATE_FORM)].atom;
}


bool TermFormGetSign(Atom termForm)
{
	BTree * tree = RegistryGetCoreTable(FORM_TERM_FORM);

	TypedAtom query[3];
	TermFormSetTuple(query, CreateTypedAtom(AT_ID, termForm), anonymousVariable, anonymousVariable);
	TypedAtom tuple[3];
	RelationBTreeQuerySingle(tree, query, tuple);
	TypedAtom sign = tuple[CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_SIGN)];

	return (sign.atom == 1);
}


void PrintTermForm(Atom termForm)
{	
	if(!TermFormGetSign(termForm))
		PrintChar('!');
	PrintPredicateForm(GetPredicateForm(termForm));
}


size8 TermFormArity(Atom termForm)
{
	return PredicateArity(GetPredicateForm(termForm));
}
