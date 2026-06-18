
#include "lang/Variable.h"
#include "kernel/UInt.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/pair.h"
#include "kernel/ServiceRegistry.h"
#include "lang/TermForm.h"


void TermFormSetTuple(TypedTuple * tuple, TypedAtom termForm, TypedAtom predicateForm, TypedAtom sign)
{
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_TERM_FORM),
		termForm
	);
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_PREDICATE_FORM),
		predicateForm
	);
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_SIGN),
		sign
	);
}


Atom CreateTermForm(Atom predicateForm, bool sign)
{
	IFactDraft draft;
	IFactBegin(&draft);

	Atom termForm = GetCorePredicateForm(FORM_TERM_FORM);

	IFactBeginConjunction(
		&draft,
		termForm,
		RegistryGetCoreBTreeService(FORM_TERM_FORM),
		CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_TERM_FORM)
	);
	TypedTuple * tuple = CreateTypedTuple(3);
	TermFormSetTuple(tuple,
		CreateTypedAtom(AT_ID, termForm),
		CreateTypedAtom(AT_ID, predicateForm),
		CreateTypedAtom(AT_UINT, (Atom) {._uint = sign ? 1 : 0})
	);
	IFactAddTuple(&draft, tuple);
	FreeTypedTuple(tuple);
	IFactEndPredicateForm(&draft);

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


Atom TermFormGetPredicateForm(Atom termForm)
{
	BTree * tree = RegistryGetCoreBTreeService(FORM_TERM_FORM);

	TypedTuple * query = CreateTypedTuple(3);
	TermFormSetTuple(query,
		CreateTypedAtom(AT_ID, termForm), anonymousVariable, anonymousVariable);
	TypedAtom predicateForm = RelationBTreeQuerySingleAtom(
		tree, query, CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_PREDICATE_FORM)
	);
	FreeTypedTuple(query);
	return predicateForm.atom;
}


bool TermFormGetSign(Atom termForm)
{
	BTree * tree = RegistryGetCoreBTreeService(FORM_TERM_FORM);

	TypedTuple * query = CreateTypedTuple(3);
	TermFormSetTuple(query, CreateTypedAtom(AT_ID, termForm), anonymousVariable, anonymousVariable);
	TypedAtom sign = RelationBTreeQuerySingleAtom(
		tree, query, CorePredicateRoleIndex(FORM_TERM_FORM, ROLE_SIGN));
	FreeTypedTuple(query);
	return (sign.atom._uint == 1);
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
