
#include "lang/Quote.h"
#include "datumtypes/Variable.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "lang/Atom.h"
#include "lang/Formula.h"


Atom CreateQuote(Atom quoted)
{
	ASSERT(quoted.type == DT_VARIABLE || IsFormula(quoted));

	IFactDraft draft;
	IFactBegin(&draft);

	Atom form = GetCorePredicateForm(FORM_QUOTE_QUOTED);

	IFactBeginConjunction(
		&draft, form,
		CorePredicateRoleIndex(FORM_QUOTE_QUOTED, ROLE_QUOTE)
	);
	
	Atom tuple[2];
	QuoteSetTuple(tuple, invalidAtom, quoted);
	IFactAddClause(&draft, tuple);
	IFactEndConjunction(&draft);
	
	return IFactEnd(&draft);
}


void QuoteSetTuple(Atom * tuple, Atom quote, Atom quoted)
{
	tuple[CorePredicateRoleIndex(FORM_QUOTE_QUOTED, ROLE_QUOTE)] = quote;
	tuple[CorePredicateRoleIndex(FORM_QUOTE_QUOTED, ROLE_QUOTED)] = quoted;
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

	Atom query[2];
	QuoteSetTuple(query, quote, anonymousVariable);
	Atom tuple[2];
	RelationBTreeQuerySingle(tree, query, tuple);

	return tuple[CorePredicateRoleIndex(FORM_QUOTE_QUOTED, ROLE_QUOTED)];
}


void PrintQuoted(Atom quoted)
{
	PrintChar('\'');
	PrintAtom(QuoteGetQuoted(quoted));
}
