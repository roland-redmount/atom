
#include "datumtypes/Variable.h"
#include "lang/Quote.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "lang/TypedAtom.h"
#include "lang/Formula.h"



static void quoteSetTuple(TypedAtom * tuple, TypedAtom quote, TypedAtom quoted)
{
	tuple[CorePredicateRoleIndex(FORM_QUOTE_QUOTED, ROLE_QUOTE)] = quote;
	tuple[CorePredicateRoleIndex(FORM_QUOTE_QUOTED, ROLE_QUOTED)] = quoted;
}


Datum CreateQuote(Datum quoted)
{
	ASSERT(IsFormula(quoted));

	IFactDraft draft;
	IFactBegin(&draft);

	Datum form = GetCorePredicateForm(FORM_QUOTE_QUOTED);

	IFactBeginConjunction(
		&draft, form,
		CorePredicateRoleIndex(FORM_QUOTE_QUOTED, ROLE_QUOTE)
	);
	
	TypedAtom tuple[2];
	quoteSetTuple(tuple, invalidAtom, CreateTypedAtom(DT_ID, quoted));
	IFactAddClause(&draft, tuple);
	IFactEndConjunction(&draft);
	
	return IFactEnd(&draft);
}


bool IsQuote(Datum atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_QUOTE_QUOTED),
		GetCoreRoleName(ROLE_QUOTE)
	);
}


Datum QuoteGetQuoted(Datum quote)
{
	BTree * tree = RegistryGetCoreTable(FORM_QUOTE_QUOTED);

	TypedAtom query[2];
	quoteSetTuple(query, CreateTypedAtom(DT_ID, quote), anonymousVariable);
	TypedAtom tuple[2];
	RelationBTreeQuerySingle(tree, query, tuple);

	return tuple[CorePredicateRoleIndex(FORM_QUOTE_QUOTED, ROLE_QUOTED)].datum;
}


void PrintQuoted(Datum quoted)
{
	PrintChar('\'');
	PrintFormula(QuoteGetQuoted(quoted));
}
