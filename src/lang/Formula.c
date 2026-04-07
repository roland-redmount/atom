
#include "datumtypes/id.h"
#include "datumtypes/Variable.h"
#include "kernel/ifact.h"
#include "kernel/list.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/tuples.h"
#include "lang/Form.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/ClauseForm.h"
#include "lang/FullForm.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"

#include "util/hashing.h"
#include "util/sort.h"


void FormulaSetTuple(Atom * tuple, Atom formula, Atom form, Atom actorsList)
{
	tuple[CorePredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_FORMULA)] = formula;
	tuple[CorePredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_FORM)] = form;
	tuple[CorePredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_ACTORS)] = actorsList;
}


Datum CreateFormula(Datum form, Datum actorsList)
{
	IFactDraft draft;
	IFactBegin(&draft);

	IFactBeginConjunction(
		&draft,
		GetCorePredicateForm(FORM_FORMULA_FORM_ACTORS),
		CorePredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_FORMULA)
	);

	Atom tuple[3];
	FormulaSetTuple(tuple, invalidAtom, CreateID(form), CreateID(actorsList));
	IFactAddClause(&draft, tuple);
	IFactEndConjunction(&draft);	

	return IFactEnd(&draft);
}

/**
 * Create a form from an array of actors. The array must have as least as
 * many elements as the arity of the given form.
 */
Datum CreateFormulaFromArray(Datum form, Atom * actors)
{
	size8 arity = FormArity(form);
	Datum actorsList = CreateListFromArray(actors, arity);
	Datum formula = CreateFormula(form, actorsList);
	IFactRelease(actorsList);
	return formula;
}


bool IsFormula(Datum atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_FORMULA_FORM_ACTORS),
		GetCoreRoleName(ROLE_FORMULA)
	);
}


bool FormulaIsPredicate(Datum formula)
{
	return IsPredicateForm(FormulaGetForm(formula));
}


bool FormulaIsTerm(Datum formula)
{
	return IsTermForm(FormulaGetForm(formula));
}


bool FormulaIsClause(Datum formula)
{
	return IsClauseForm(FormulaGetForm(formula));
}


bool FormulaIsConjunction(Datum formula)
{
	return IsPredicateForm(FormulaGetForm(formula));
}


index32 FormulaRoleIndex(Datum formula, Datum name)
{
	// TODO: currently this only supports predicates.
	// Need to implement GetClauseRoleIndex() &c
	ASSERT(FormulaIsPredicate(formula))
	return PredicateRoleIndex(formula, name);
}


/**
 * Convenience method to create a predicate from two arrays
 * of role names (DT_NAME) and actors, both of the same length arity.
 */
Datum CreatePredicate(Datum const * roles, Atom * actors, size8 arity)
{
	Datum predicateForm = CreatePredicateForm(roles, arity);

	index8 roleOrder[arity];
	// need to convert to atoms for MultisetIterationOrder()
	Atom roleAtoms[arity];
	for(index8 i = 0; i < arity; i++)
		roleAtoms[i] = (Atom) {.type = DT_NAME, .datum = roles[i]};
	MultisetIterationOrder(predicateForm, roleAtoms, roleOrder, arity);

	Atom actorsOrdered[arity];
	CopyMemory(actors, actorsOrdered, arity * sizeof(Atom));
	ReorderArray(actorsOrdered, roleOrder, arity, sizeof(Atom));

	Datum predicate = CreateFormulaFromArray(
		predicateForm,
		actorsOrdered
	);
	IFactRelease(predicateForm);
	return predicate;	
}


/**
 * Create a term from a predicate and sign
 */
Datum CreateTerm(Datum predicate, bool sign)
{
	ASSERT(FormulaIsPredicate(predicate));
	Datum predicateForm = FormulaGetForm(predicate);
	Datum termForm = CreateTermForm(predicateForm, sign);
	Datum term = CreateFormula(
		termForm,
		FormulaGetActors(predicate)
	);
	IFactRelease(termForm);
	return term;
}


Datum CreateClause(Datum const * terms, size8 nTerms)
{
	// collect term forms and their arities
	Datum termForms[nTerms];
	size8 termArities[nTerms];
	size8 clauseArity = 0;
	for(index8 i = 0; i < nTerms; i++) {
		termForms[i] = FormulaGetForm(terms[i]);
		termArities[i] = TermFormArity(termForms[i]);
		ASSERT(clauseArity < 255 - termArities[i]);
		clauseArity += termArities[i];
	}
	Datum clauseForm = CreateClauseForm(termForms, nTerms);

	// collect actors from terms into a single array
	Atom actors[clauseArity];
	for(index8 i = 0, k = 0; i < nTerms; i++) {
		Datum actorsList = FormulaGetActors(terms[i]);
		for(index8 j = 0; j < termArities[i]; j++)
			actors[k++] = ListGetElement(actorsList, j + 1);
	}

	// reorder actors to match the name order of clauseForm
	index8 termOrder[nTerms]; 
	// need term forms as typed atoms for MultisetIterationOrder()
	Atom termFormAtoms[nTerms];
	for(index8 i = 0; i < nTerms; i++)
		termFormAtoms[i] = CreateAtom(DT_ID, termForms[i]);
	// find ordering
	MultisetIterationOrder(clauseForm, termFormAtoms, termOrder, nTerms);
	// reorder actors
	size32 blockSizes[nTerms];
	for(index8 i = 0; i < nTerms; i++)
		blockSizes[i] = termArities[i] * sizeof(Atom);
	ReorderRaggedArray(actors, termOrder, blockSizes, nTerms);

	Datum clause = CreateFormulaFromArray(clauseForm, actors);
	IFactRelease(clauseForm);
	return clause;
}


uint8 FormulaArity(Datum formula)
{
	return FormArity(FormulaGetForm(formula));
}


Datum FormulaGetForm(Datum formula)
{
	BTree * tree = RegistryGetCoreTable(FORM_FORMULA_FORM_ACTORS);
	Atom query[3];
	FormulaSetTuple(query, CreateID(formula), anonymousVariable, anonymousVariable);
	Atom result[3];
	RelationBTreeQuerySingle(tree, query, result);
	return result[CorePredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_FORM)].datum;
}


Datum FormulaGetActors(Datum formula)
{
	BTree * tree = RegistryGetCoreTable(FORM_FORMULA_FORM_ACTORS);
	Atom query[3];
	FormulaSetTuple(query, CreateID(formula), anonymousVariable, anonymousVariable);
	Atom result[3];
	RelationBTreeQuerySingle(tree, query, result);
	return result[CorePredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_ACTORS)].datum;
}


/**
 * Print a predicate with actors in the order given by atomIndex
 */
static void printPredicate(Datum predicateForm, Datum atomsList, index8 * atomIndex)
{	
	MultisetIterator iterator;
	MultisetIterate(predicateForm, &iterator);
	while(MultisetIteratorHasNext(&iterator)) {	
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			PrintName(em.element.datum);
			PrintChar(' ');
			PrintAtom(ListGetElement(atomsList, *atomIndex + 1));
			PrintChar(' ');
			(*atomIndex)++;
		}
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
}


static void printTerm(Datum termForm, Datum atomsList, index8 * atomIndex)
{
	bool sign = TermFormGetSign(termForm);
	if(!sign)
		PrintChar('!');
	printPredicate(GetPredicateForm(termForm), atomsList, atomIndex);
}


static void printClause(Datum clauseForm, Datum atomsList, index8 * atomIndex)
{	
	MultisetIterator iterator;
	MultisetIterate(clauseForm, &iterator);

	while(MultisetIteratorHasNext(&iterator)) {	
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		MultisetIteratorNext(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			printTerm(em.element.datum, atomsList, atomIndex);
			if((j < em.multiple - 1) | MultisetIteratorHasNext(&iterator))
				PrintCString(" | ");
		}
	}
	MultisetIteratorEnd(&iterator);
}


static void printConjunction(Datum form, Datum atomsList, index8* atomIndex)
{
	MultisetIterator iterator;
	MultisetIterate(form, &iterator);

	while(MultisetIteratorHasNext(&iterator)) {	
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		MultisetIteratorNext(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			printClause(em.element.datum, atomsList, atomIndex);
			if((j < em.multiple - 1) | MultisetIteratorHasNext(&iterator))
				PrintCString(" & ");
		}
	}
	MultisetIteratorEnd(&iterator);
}


/**
 * Traverse and print a formula
 */
void PrintFormula(Datum formula)
{
	// atom index
	index8 atomIndex = 0;
	Datum form = FormulaGetForm(formula);
	Datum actorsList = FormulaGetActors(formula);

	if(FormulaIsPredicate(formula))
		printPredicate(form, actorsList, &atomIndex);
	else if(FormulaIsTerm(formula))
		printTerm(form, actorsList, &atomIndex);
	else if(FormulaIsClause(formula))
		printClause(form, actorsList, &atomIndex);
	else if(FormulaIsConjunction(formula))
		printConjunction(form, actorsList, &atomIndex);
	else
		ASSERT(false);
}


data64 FormulaHashFormActors(data64 formHash, Atom const * actors, size32 nActors, data64 initialHash)
{
	data64 hash = DJB2DoubleHashAdd(&formHash, sizeof(data64), initialHash);
	return DJB2DoubleHashAdd(actors, sizeof(Atom) * nActors, hash);
}


size8 FormulaUniqueVariables(Datum formula, Atom * variables)
{
	Datum actorsList = FormulaGetActors(formula);
	ListIterator iterator;
	ListIterate(actorsList, &iterator);
	index8 i = 0;
	while(ListIteratorHasNext(&iterator)) {
		Atom atom = ListIteratorGetElement(&iterator);
		if(IsVariable(atom) && !TupleContainsAtom(variables, i, atom))
			variables[i++] = atom;	
		ListIteratorNext(&iterator);
	}
	ListIteratorEnd(&iterator);
	return i;
}

