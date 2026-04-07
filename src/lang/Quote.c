
#include "datumtypes/id.h"
#include "datumtypes/Variable.h"
#include "lang/Quote.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Atom.h"
#include "lang/Formula.h"



static void quoteSetTuple(Atom * tuple, Atom quote, Atom quoted)
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
	
	Atom tuple[2];
	quoteSetTuple(tuple, invalidAtom, CreateID(quoted));
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

	Atom query[2];
	quoteSetTuple(query, CreateID(quote), anonymousVariable);
	Atom tuple[2];
	RelationBTreeQuerySingle(tree, query, tuple);

	return tuple[CorePredicateRoleIndex(FORM_QUOTE_QUOTED, ROLE_QUOTED)].datum;
}


void PrintQuoted(Datum quoted)
{
	PrintChar('\'');
	PrintFormula(QuoteGetQuoted(quoted));
}
