

#include "lang/Form.h"
#include "lang/FullForm.h"
#include "lang/ClauseForm.h"
#include "lang/TermForm.h"
#include "lang/PredicateForm.h"


size8 FormArity(Atom form)
{
	if(IsPredicateForm(form))
		return PredicateArity(form);
	else if(IsTermForm(form))
		return TermFormArity(form);
	else if(IsClauseForm(form))
		return ClauseArity(form);
	else if(IsConjunctionForm(form))
		return FullFormArity(form);
	else {
		ASSERT(false);
		return 0;
	}
}

