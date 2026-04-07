
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


void FormulaSetTuple(TypedAtom * tuple, TypedAtom formula, TypedAtom form, TypedAtom actorsList)
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

	TypedAtom tuple[3];
	FormulaSetTuple(tuple, invalidAtom, CreateTypedAtom(DT_ID, form), CreateTypedAtom(DT_ID, actorsList));
	IFactAddClause(&draft, tuple);
	IFactEndConjunction(&draft);	

	return IFactEnd(&draft);
}

/**
 * Create a form from an array of actors. The array must have as least as
 * many elements as the arity of the given form.
 */
Datum CreateFormulaFromArray(Datum form, TypedAtom * actors)
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
Datum CreatePredicate(Datum const * roles, TypedAtom * actors, size8 arity)
{
	Datum predicateForm = CreatePredicateForm(roles, arity);

	index8 roleOrder[arity];
	// need to convert to atoms for MultisetIterationOrder()
	TypedAtom roleAtoms[arity];
	for(index8 i = 0; i < arity; i++)
		roleAtoms[i] = (TypedAtom) {.type = DT_NAME, .datum = roles[i]};
	MultisetIterationOrder(predicateForm, roleAtoms, roleOrder, arity);

	TypedAtom actorsOrdered[arity];
	CopyMemory(actors, actorsOrdered, arity * sizeof(TypedAtom));
	ReorderArray(actorsOrdered, roleOrder, arity, sizeof(TypedAtom));

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
	TypedAtom actors[clauseArity];
	for(index8 i = 0, k = 0; i < nTerms; i++) {
		Datum actorsList = FormulaGetActors(terms[i]);
		for(index8 j = 0; j < termArities[i]; j++)
			actors[k++] = ListGetElement(actorsList, j + 1);
	}

	// reorder actors to match the name order of clauseForm
	index8 termOrder[nTerms]; 
	// need term forms as typed atoms for MultisetIterationOrder()
	TypedAtom termFormAtoms[nTerms];
	for(index8 i = 0; i < nTerms; i++)
		termFormAtoms[i] = CreateTypedAtom(DT_ID, termForms[i]);
	// find ordering
	MultisetIterationOrder(clauseForm, termFormAtoms, termOrder, nTerms);
	// reorder actors
	size32 blockSizes[nTerms];
	for(index8 i = 0; i < nTerms; i++)
		blockSizes[i] = termArities[i] * sizeof(TypedAtom);
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
	TypedAtom query[3];
	FormulaSetTuple(query, CreateTypedAtom(DT_ID, formula), anonymousVariable, anonymousVariable);
	TypedAtom result[3];
	RelationBTreeQuerySingle(tree, query, result);
	return result[CorePredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_FORM)].datum;
}


Datum FormulaGetActors(Datum formula)
{
	BTree * tree = RegistryGetCoreTable(FORM_FORMULA_FORM_ACTORS);
	TypedAtom query[3];
	FormulaSetTuple(query, CreateTypedAtom(DT_ID, formula), anonymousVariable, anonymousVariable);
	TypedAtom result[3];
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
			PrintTypedAtom(ListGetElement(atomsList, *atomIndex + 1));
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


data64 FormulaHashFormActors(data64 formHash, TypedAtom const * actors, size32 nActors, data64 initialHash)
{
	data64 hash = DJB2DoubleHashAdd(&formHash, sizeof(data64), initialHash);
	return DJB2DoubleHashAdd(actors, sizeof(TypedAtom) * nActors, hash);
}


size8 FormulaUniqueVariables(Datum formula, TypedAtom * variables)
{
	Datum actorsList = FormulaGetActors(formula);
	ListIterator iterator;
	ListIterate(actorsList, &iterator);
	index8 i = 0;
	while(ListIteratorHasNext(&iterator)) {
		TypedAtom atom = ListIteratorGetElement(&iterator);
		if(IsVariable(atom) && !TupleContainsAtom(variables, i, atom))
			variables[i++] = atom;	
		ListIteratorNext(&iterator);
	}
	ListIteratorEnd(&iterator);
	return i;
}

