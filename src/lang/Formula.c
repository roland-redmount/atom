
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
	tuple[GetPredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_FORMULA)] = formula;
	tuple[GetPredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_FORM)] = form;
	tuple[GetPredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_ACTORS)] = actorsList;
}


Atom CreateFormula(Atom form, Atom actorsList)
{
	IFactDraft draft;
	IFactBegin(&draft);

	IFactBeginConjunction(
		&draft,
		GetCorePredicateForm(FORM_FORMULA_FORM_ACTORS),
		GetPredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_FORMULA)
	);

	Atom tuple[3];
	FormulaSetTuple(tuple, invalidAtom, form, actorsList);
	IFactAddClause(&draft, tuple);
	IFactEndConjunction(&draft);	

	return IFactEnd(&draft);
}

/**
 * Create a form from an array of actors. The array must have as least as
 * many elements as the arity of the given form.
 */
Atom CreateFormulaFromArray(Atom form, Atom * actors)
{
	size8 arity = FormArity(form);
	Atom actorsList = CreateListFromArray(actors, arity);
	Atom formula = CreateFormula(form, actorsList);
	IFactRelease(actorsList);
	return formula;
}


bool IsFormula(Atom atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_FORMULA_FORM_ACTORS),
		GetCoreRoleName(ROLE_FORMULA)
	);
}


bool FormulaIsPredicate(Atom formula)
{
	return IsPredicateForm(FormulaGetForm(formula));
}


bool FormulaIsTerm(Atom formula)
{
	return IsTermForm(FormulaGetForm(formula));
}


bool FormulaIsClause(Atom formula)
{
	return IsClauseForm(FormulaGetForm(formula));
}


bool FormulaIsConjunction(Atom formula)
{
	return IsPredicateForm(FormulaGetForm(formula));
}


index32 FormulaRoleIndex(Atom formula, Atom name)
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
Atom CreatePredicate(Atom const * roles, Atom * actors, size8 arity)
{
	Atom predicateForm = CreatePredicateForm(roles, arity);

	index8 roleOrder[arity]; 
	MultisetIterationOrder(predicateForm, roles, roleOrder, arity);

	Atom actorsOrdered[arity];
	CopyMemory(actors, actorsOrdered, arity * sizeof(Atom));
	ReorderArray(actorsOrdered, roleOrder, arity, sizeof(Atom));

	Atom predicate = CreateFormulaFromArray(
		predicateForm,
		actorsOrdered
	);
	IFactRelease(predicateForm);
	return predicate;	
}


/**
 * Create a term from a predicate and sign
 */
Atom CreateTerm(Atom predicate, bool sign)
{
	Atom predicateForm = FormulaGetForm(predicate);
	ASSERT(FormulaIsPredicate(predicate));
	Atom termForm = CreateTermForm(predicateForm, sign);
	Atom term = CreateFormula(
		termForm,
		FormulaGetActors(predicate)
	);
	IFactRelease(termForm);
	return term;
}


/**
 * Create a clause from a list of terms.
 */
Atom CreateClause(Atom const * terms, size8 nTerms)
{
	// collect term forms and their arities
	Atom termForms[nTerms];
	size8 termArities[nTerms];
	size8 clauseArity = 0;
	for(index8 i = 0; i < nTerms; i++) {
		termForms[i] = FormulaGetForm(terms[i]);
		termArities[i] = TermFormArity(termForms[i]);
		ASSERT(clauseArity < 255 - termArities[i]);
		clauseArity += termArities[i];
	}
	Atom clauseForm = CreateClauseForm(termForms, nTerms);

	// collect actors from terms into a single array
	Atom actors[clauseArity];
	for(index8 i = 0, k = 0; i < nTerms; i++) {
		Atom actorsList = FormulaGetActors(terms[i]);
		for(index8 j = 0; j < termArities[i]; j++)
			actors[k++] = ListGetElement(actorsList, j + 1);
	}

	// reorder actors to match "system" ordering
	index8 termOrder[nTerms]; 
	MultisetIterationOrder(clauseForm, termForms, termOrder, nTerms);

	size32 blockSizes[nTerms];
	for(index8 i = 0; i < nTerms; i++)
		blockSizes[i] = termArities[i] * sizeof(Atom);
	ReorderRaggedArray(actors, termOrder, blockSizes, nTerms);

	Atom clause = CreateFormulaFromArray(clauseForm, actors);
	IFactRelease(clauseForm);
	return clause;
}


uint8 FormulaArity(Atom formula)
{
	return FormArity(FormulaGetForm(formula));
}


Atom FormulaGetForm(Atom formula)
{
	BTree * tree = RegistryGetCoreTable(FORM_FORMULA_FORM_ACTORS);
	Atom query[3];
	FormulaSetTuple(query, formula, anonymousVariable, anonymousVariable);
	Atom result[3];
	RelationBTreeQuerySingle(tree, query, result);
	return result[GetPredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_FORM)];
}


Atom FormulaGetActors(Atom formula)
{
	BTree * tree = RegistryGetCoreTable(FORM_FORMULA_FORM_ACTORS);
	Atom query[3];
	FormulaSetTuple(query, formula, anonymousVariable, anonymousVariable);
	Atom result[3];
	RelationBTreeQuerySingle(tree, query, result);
	return result[GetPredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_ACTORS)];
}


/**
 * Print a predicate with actors in the order given by atomIndex
 */
static void printPredicate(Atom predicateForm, Atom atomsList, index8 * atomIndex)
{	
	MultisetIterator iterator;
	MultisetIterate(predicateForm, &iterator);
	while(MultisetIteratorHasNext(&iterator)) {	
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			PrintName(em.element);
			PrintChar(' ');
			PrintAtom(ListGetElement(atomsList, *atomIndex + 1));
			PrintChar(' ');
			(*atomIndex)++;
		}
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
}


static void printTerm(Atom termForm, Atom atomsList, index8 * atomIndex)
{
	bool sign = TermFormGetSign(termForm);
	if(!sign)
		PrintChar('!');
	printPredicate(GetPredicateForm(termForm), atomsList, atomIndex);
}


static void printClause(Atom clauseForm, Atom atomsList, index8 * atomIndex)
{	
	MultisetIterator iterator;
	MultisetIterate(clauseForm, &iterator);

	while(MultisetIteratorHasNext(&iterator)) {	
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		MultisetIteratorNext(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			printTerm(em.element, atomsList, atomIndex);
			if((j < em.multiple - 1) | MultisetIteratorHasNext(&iterator))
				PrintCString(" | ");
		}
	}
	MultisetIteratorEnd(&iterator);
}


static void printConjunction(Atom form, Atom atomsList, index8* atomIndex)
{
	MultisetIterator iterator;
	MultisetIterate(form, &iterator);

	while(MultisetIteratorHasNext(&iterator)) {	
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		MultisetIteratorNext(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			printClause(em.element, atomsList, atomIndex);
			if((j < em.multiple - 1) | MultisetIteratorHasNext(&iterator))
				PrintCString(" & ");
		}
	}
	MultisetIteratorEnd(&iterator);
}


/**
 * Traverse and print a formula
 */
void PrintFormula(Atom formula)
{
	// atom index
	index8 atomIndex = 0;
	Atom form = FormulaGetForm(formula);
	Atom actorsTuple = FormulaGetActors(formula);

	if(FormulaIsPredicate(formula))
		printPredicate(form, actorsTuple, &atomIndex);
	else if(FormulaIsTerm(formula))
		printTerm(form, actorsTuple, &atomIndex);
	else if(FormulaIsClause(formula))
		printClause(form, actorsTuple, &atomIndex);
	else if(FormulaIsConjunction(formula))
		printConjunction(form, actorsTuple, &atomIndex);
	else
		ASSERT(false);
}


data64 FormulaHashFormActors(data64 formHash, Atom const * actors, size32 nActors, data64 initialHash)
{
	data64 hash = DJB2DoubleHashAdd(&formHash, sizeof(data64), initialHash);
	return DJB2DoubleHashAdd(actors, sizeof(Atom) * nActors, hash);
}


size8 FormulaUniqueVariables(Atom formula, Atom * variables)
{
	Atom actorsList = FormulaGetActors(formula);
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

