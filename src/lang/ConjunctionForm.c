
#include "lang/ConjunctionForm.h"
#include "kernel/ifact.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/RelationRegistry.h"


/**
 * A conjunction form is a multiset of clause forms
 */
Atom CreateConjunctionForm(Atom const clauseForms[], size8 nClauseForms)
{
	Atom uniqueClauseForms[nClauseForms];
	CopyMemory(clauseForms, uniqueClauseForms, nClauseForms * sizeof(Atom));
	// reduce to unique roles
	uint32 multiplicities[nClauseForms];
	size8 nUniqueClauseForms = ReduceAtomsArray(uniqueClauseForms, multiplicities, nClauseForms);

	IFactDraft draft;
	IFactBegin(&draft);

	AddMultisetToIFactFromArrays(&draft, uniqueClauseForms, multiplicities, nUniqueClauseForms, AT_ID);

	// (conjunction-form @form)
	RelationTable * conjunctionFormTable = GetCoreRelationTable(RELATION_CONJUNCTION_FORM);
	IFactBeginConjunction(&draft, conjunctionFormTable, 0);
	IFactAddTuple(&draft, (Atom[]) {(Atom) {0}});
	IFactEndConjunction(&draft);	

	return IFactEnd(&draft);
}


bool IsConjunctionForm(Atom atom)
{
	return AtomHasRole(
		atom,
		GetCoreRelation(RELATION_CONJUNCTION_FORM),
		GetCoreRoleName(ROLE_CONJUNCTION_FORM)
	);
}


size8 ConjunctionFormNUniqueClauseForms(Atom form)
{
	return MultisetNUniqueElements(form, AT_ID);
}


size8 ConjunctionFormNClauseFormsTotal(Atom form)
{
	return MultisetSize(form, AT_ID);
}


size8 ConjunctionFormArity(Atom form)
{
	// the arity of a cojunction is the sum of unique terms arity * multiple
	MultisetIterator iterator;
	MultisetIterate(form, AT_ID, &iterator);
	size8 arity = 0;
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		uint8 clauseArity = ClauseArity(elementMultiple.element);
		arity += clauseArity * elementMultiple.multiple;
	}
	MultisetIteratorEnd(&iterator);
	return arity;
}

/**
 * Traverse and print a form to stdout
 */
void PrintConjunctionForm(Atom form)
{
	MultisetIterator iterator;
	MultisetIterate(form, AT_ID, &iterator);

	PrintChar('(');
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple elementMultiple = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < elementMultiple.multiple; j++) {
			PrintClauseForm(elementMultiple.element);
			PrintCString(" & ");
		}
	}
	MultisetIteratorEnd(&iterator);
	PrintChar(')');
}

