
#include "lang/Variable.h"
#include "lang/Quote.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "lang/TypedAtom.h"
#include "lang/Formula.h"



static void quoteSetTuple(Tuple * tuple, TypedAtom quote, TypedAtom quoted)
{
	TupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_QUOTE_QUOTED, ROLE_QUOTE),
		 quote
	);
	TupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_QUOTE_QUOTED, ROLE_QUOTED),
		quoted
	);
}


Atom CreateQuote(Atom quoted)
{
	ASSERT(IsFormula(quoted));

	IFactDraft draft;
	IFactBegin(&draft);

	IFactBeginConjunction(
		&draft,
		GetCorePredicateForm(FORM_QUOTE_QUOTED),
		RegistryGetCoreTable(FORM_QUOTE_QUOTED),
		CorePredicateRoleIndex(FORM_QUOTE_QUOTED, ROLE_QUOTE)
	);
	
	Tuple * tuple = CreateTuple(2);
	quoteSetTuple(tuple, invalidAtom, CreateTypedAtom(AT_ID, quoted));
	IFactAddClause(&draft, tuple);
	FreeTuple(tuple);
	IFactEndConjunction(&draft);

	return IFactEnd(&draft);
}


bool IsQuote(Atom atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_QUOTE_QUOTED),
		GetCoreRoleName(ROLE_QUOTE)
	);
}


Atom QuoteGetQuoted(Atom quote)
{
	BTree * tree = RegistryGetCoreTable(FORM_QUOTE_QUOTED);

	Tuple * query = CreateTuple(2);
	quoteSetTuple(query, CreateTypedAtom(AT_ID, quote), anonymousVariable);
	TypedAtom quoted = RelationBTreeQuerySingleAtom(
		tree, query,
		CorePredicateRoleIndex(FORM_QUOTE_QUOTED, ROLE_QUOTED)
	);
	FreeTuple(query);
	return quoted.atom;
}


void PrintQuoted(Atom quoted)
{
	PrintChar('\'');
	PrintFormula(QuoteGetQuoted(quoted));
}
