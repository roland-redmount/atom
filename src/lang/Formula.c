
#include "lang/Variable.h"
#include "kernel/ifact.h"
#include "kernel/list.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/typedtuple.h"
#include "lang/Form.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/ClauseForm.h"
#include "lang/ConjunctionForm.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"

#include "util/hashing.h"
#include "util/sort.h"


void FormulaSetTuple(TypedTuple * tuple, TypedAtom formula, TypedAtom form, TypedAtom actorsList)
{
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_FORMULA),
		formula
	);
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_FORM),
		form
	);
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_ACTORS),
		actorsList
	);
}


Atom CreateFormula(Atom form, Atom actorsList)
{
	IFactDraft draft;
	IFactBegin(&draft);

	IFactBeginConjunction(
		&draft,
		GetCorePredicateForm(FORM_FORMULA_FORM_ACTORS),
		RegistryGetCoreBTreeService(FORM_FORMULA_FORM_ACTORS),
		CorePredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_FORMULA)
	);

	TypedTuple * tuple = CreateTypedTuple(3);
	FormulaSetTuple(tuple, invalidAtom, CreateTypedAtom(AT_ID, form), CreateTypedAtom(AT_ID, actorsList));
	IFactAddClause(&draft, tuple);
	FreeTypedTuple(tuple);
	IFactEndConjunction(&draft);	

	return IFactEnd(&draft);
}

/**
 * Create a form from an array of actors. The array must have as least as
 * many elements as the arity of the given form.
 */
Atom CreateFormulaFromArray(Atom form, TypedAtom * actors)
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
	return IsConjunctionForm(FormulaGetForm(formula));
}


index32 FormulaRoleIndex(Atom formula, Atom name)
{
	// TODO: currently this only supports predicates.
	// Need to implement GetClauseRoleIndex() &c
	ASSERT(FormulaIsPredicate(formula))
	return PredicateRoleIndex(formula, name);
}


Atom CreatePredicate(Atom const * roles, TypedAtom * actors, size8 arity)
{
	Atom predicateForm = CreatePredicateForm(roles, arity);

	index8 roleOrder[arity];
	// need to convert to atoms for MultisetIterationOrder()
	TypedAtom roleAtoms[arity];
	for(index8 i = 0; i < arity; i++)
		roleAtoms[i] = (TypedAtom) {.type = AT_NAME, .atom = roles[i]};
	MultisetIterationOrder(predicateForm, roleAtoms, roleOrder, arity);

	TypedAtom actorsOrdered[arity];
	CopyMemory(actors, actorsOrdered, arity * sizeof(TypedAtom));
	ReorderArray(actorsOrdered, roleOrder, arity, sizeof(TypedAtom));

	Atom predicate = CreateFormulaFromArray(
		predicateForm,
		actorsOrdered
	);
	IFactRelease(predicateForm);
	return predicate;	
}


Atom CreateTerm(Atom predicate, bool sign)
{
	ASSERT(FormulaIsPredicate(predicate));
	Atom predicateForm = FormulaGetForm(predicate);
	Atom termForm = CreateTermForm(predicateForm, sign);
	Atom term = CreateFormula(
		termForm,
		FormulaGetActors(predicate)
	);
	IFactRelease(termForm);
	return term;
}


TypedAtom TermGetRoleActor(Atom termForm, TypedTuple const * termActors, const char * role, uint8 m)
{
	ASSERT(m > 0)
	Atom predicateForm = TermFormGetPredicateForm(termForm);
	Atom roleName = CreateNameFromCString(role);
	index8 actorIndex = PredicateRoleIndex(predicateForm, roleName) + (m - 1);
	NameRelease(roleName);
	return TypedTupleGetElement(termActors, actorIndex);
}


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
	TypedAtom actors[clauseArity];
	for(index8 i = 0, k = 0; i < nTerms; i++) {
		Atom actorsList = FormulaGetActors(terms[i]);
		for(index8 j = 0; j < termArities[i]; j++)
			actors[k++] = ListGetElement(actorsList, j + 1);
	}

	// reorder actors to match the name order of clauseForm
	index8 termOrder[nTerms]; 
	// need term forms as typed atoms for MultisetIterationOrder()
	TypedAtom termFormsTyped[nTerms];
	for(index8 i = 0; i < nTerms; i++)
		termFormsTyped[i] = CreateTypedAtom(AT_ID, termForms[i]);
	// find ordering
	MultisetIterationOrder(clauseForm, termFormsTyped, termOrder, nTerms);
	// reorder actors
	size32 blockSizes[nTerms];
	for(index8 i = 0; i < nTerms; i++)
		blockSizes[i] = termArities[i] * sizeof(TypedAtom);
	ReorderRaggedArray(actors, termOrder, blockSizes, nTerms);

	Atom clause = CreateFormulaFromArray(clauseForm, actors);
	IFactRelease(clauseForm);
	return clause;
}


// NOTE: this is very similar to CreateClause, could be refactored
Atom CreateConjunction(Atom const * clauses, size8 nClauses)
{
	// collect clause forms and their arities
	Atom clauseForms[nClauses];
	size8 clauseArities[nClauses];
	size8 conjunctionArity = 0;
	for(index8 i = 0; i < nClauses; i++) {
		clauseForms[i] = FormulaGetForm(clauses[i]);
		clauseArities[i] = ClauseArity(clauseForms[i]);
		ASSERT(conjunctionArity < 255 - clauseArities[i]);
		conjunctionArity += clauseArities[i];
	}
	Atom conjunctionForm = CreateConjunctionForm(clauseForms, nClauses);

	// collect actors from terms into a single array
	TypedAtom actors[conjunctionArity];
	for(index8 i = 0, k = 0; i < nClauses; i++) {
		Atom actorsList = FormulaGetActors(clauses[i]);
		for(index8 j = 0; j < clauseArities[i]; j++)
			actors[k++] = ListGetElement(actorsList, j + 1);
	}

	// reorder actors to match the name order of clauseForm
	index8 clauseOrder[nClauses]; 
	// need clause forms as typed atoms for MultisetIterationOrder()
	TypedAtom clauseFormsTyped[nClauses];
	for(index8 i = 0; i < nClauses; i++)
		clauseFormsTyped[i] = CreateTypedAtom(AT_ID, clauseForms[i]);
	// find ordering
	MultisetIterationOrder(conjunctionForm, clauseFormsTyped, clauseOrder, nClauses);
	// reorder actors
	size32 blockSizes[nClauses];
	for(index8 i = 0; i < nClauses; i++)
		blockSizes[i] = clauseArities[i] * sizeof(TypedAtom);
	ReorderRaggedArray(actors, clauseOrder, blockSizes, nClauses);

	Atom conjunction = CreateFormulaFromArray(conjunctionForm, actors);
	IFactRelease(conjunctionForm);
	return conjunction;
}


index8 ClauseGetTermIndex(Atom clauseForm, Atom termForm, uint8 m)
{
	// iterate over terms in the clause to compoute the index
	MultisetIterator iterator;
	MultisetIterate(clauseForm, &iterator);

	index8 index = 0;
	bool found = false;
	ElementMultiple elementMultiple;
	while(MultisetIteratorNext(&iterator)) {
		elementMultiple = MultisetIteratorGetElement(&iterator);
		if(elementMultiple.element.atom.hash == termForm.hash) {
			found = true;
			break;
		}
		index += elementMultiple.multiple;
	}
	MultisetIteratorEnd(&iterator);
	ASSERT(found);
	// Select the k'th occurence of the term form
	// (all terms of the same form must be contiguous in the clause form)
	ASSERT((m > 0) && (m <= elementMultiple.multiple))
	index += m - 1;
	return index;	
}


index8 ClauseGetTermActorsIndex(Atom clauseForm, Atom termForm, uint8 m)
{
	// iterate over terms in the clause to compoute the index
	MultisetIterator iterator;
	MultisetIterate(clauseForm, &iterator);

	index8 index = 0;
	bool found = false;
	ElementMultiple elementMultiple;
	while(MultisetIteratorNext(&iterator)) {
		elementMultiple = MultisetIteratorGetElement(&iterator);
		if(elementMultiple.element.atom.hash == termForm.hash) {
			found = true;
			break;
		}
		size8 termArity = TermFormArity(elementMultiple.element.atom);
		index += elementMultiple.multiple * termArity;
	}
	MultisetIteratorEnd(&iterator);
	ASSERT(found);
	// Select the k'th occurence of the term form
	// (all terms of the same form must be contiguous in the clause form)
	ASSERT((m > 0) && (m <= elementMultiple.multiple))
	size8 termArity = FormArity(termForm);
	index += (m - 1) * termArity;
	return index;
}


void ClauseGetTermActorsIndices(Atom clauseForm, index8 * termActorsIndices)
{
	// iterate over terms in the clause and compute indices
	MultisetIterator iterator;
	MultisetIterate(clauseForm, &iterator);

	index8 k = 0;
	termActorsIndices[k] = 0;
	ElementMultiple elementMultiple;
	while(MultisetIteratorNext(&iterator)) {
		elementMultiple = MultisetIteratorGetElement(&iterator);
		size8 termArity = TermFormArity(elementMultiple.element.atom);
		for(index8 i = 0; i < elementMultiple.multiple; i++) {
			termActorsIndices[k + 1] = termActorsIndices[k] + termArity;
			k++;
		}
	}
	MultisetIteratorEnd(&iterator);
}


uint8 FormulaArity(Atom formula)
{
	return FormArity(FormulaGetForm(formula));
}


Atom FormulaGetForm(Atom formula)
{
	BTree * tree = RegistryGetCoreBTreeService(FORM_FORMULA_FORM_ACTORS);
	TypedTuple * query = CreateTypedTuple(3);
	FormulaSetTuple(query, CreateTypedAtom(AT_ID, formula), anonymousVariable, anonymousVariable);
	TypedAtom form = RelationBTreeQuerySingleAtom(
		tree, query,
		CorePredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_FORM)
	);
	FreeTypedTuple(query);
	return form.atom;
}


Atom FormulaGetActors(Atom formula)
{
	BTree * tree = RegistryGetCoreBTreeService(FORM_FORMULA_FORM_ACTORS);
	TypedTuple * query = CreateTypedTuple(3);
	FormulaSetTuple(query, CreateTypedAtom(AT_ID, formula), anonymousVariable, anonymousVariable);
	TypedAtom actorsList = RelationBTreeQuerySingleAtom(
		tree, query,
		CorePredicateRoleIndex(FORM_FORMULA_FORM_ACTORS, ROLE_ACTORS)
	);
	FreeTypedTuple(query);
	return actorsList.atom;
}


/**
 * Print a predicate with actors in the order given by atomIndex
 */
static void printPredicate(Atom predicateForm, TypedTuple const * actors, index8 * atomIndex)
{	
	MultisetIterator iterator;
	MultisetIterate(predicateForm, &iterator);

	size8 nRoles = PredicateNRoles(predicateForm);
	for(index8 i = 0; i < nRoles; i++) {	
		ASSERT(MultisetIteratorNext(&iterator))
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			PrintName(em.element.atom);
			PrintChar(' ');
			PrintTypedAtom(TypedTupleGetElement(actors, *atomIndex));
			if((i < nRoles - 1) || (j < em.multiple - 1))
				PrintChar(' ');
			(*atomIndex)++;
		}
	}
	MultisetIteratorEnd(&iterator);
}


static void printTerm(Atom termForm,TypedTuple const * actors, index8 * atomIndex)
{
	bool sign = TermFormGetSign(termForm);
	if(!sign) {
		PrintChar('!');
		PrintChar(' ');
	}
	printPredicate(TermFormGetPredicateForm(termForm), actors, atomIndex);
}


static void printClause(Atom clauseForm, TypedTuple const * actors, index8 * atomIndex)
{	
	MultisetIterator iterator;
	MultisetIterate(clauseForm, &iterator);

	size8 nTermForms = ClauseFormNTermForms(clauseForm);
	for(index8 i = 0; i < nTermForms; i++) {	
		ASSERT(MultisetIteratorNext(&iterator))
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			printTerm(em.element.atom, actors, atomIndex);
			if((j < em.multiple - 1) || (i < nTermForms - 1))
				PrintCString(" | ");
		}
	}
	MultisetIteratorEnd(&iterator);
}


static void printConjunction(Atom conjunctionForm, TypedTuple const * actors, index8* atomIndex)
{
	MultisetIterator iterator;
	MultisetIterate(conjunctionForm, &iterator);

	size8 nClauseForms = ConjunctionFormNUniqueClauseForms(conjunctionForm);
	for(index8 i = 0; i < nClauseForms; i++) {	
		ASSERT(MultisetIteratorNext(&iterator))
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			printClause(em.element.atom, actors, atomIndex);
			if((j < em.multiple - 1) || (i < nClauseForms - 1))
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
	Atom form = FormulaGetForm(formula);
	Atom actorsList = FormulaGetActors(formula);
	TypedTuple * actors = CreateTypedTuple(ListLength(actorsList));
	CopyListToTuple(actorsList, actors);
	PrintFormActorsAsFormula(form, actors);
	FreeTypedTuple(actors);
}


void PrintFormActorsAsFormula(Atom form, TypedTuple const * actors)
{
	index8 atomIndex = 0;
	if(IsPredicateForm(form))
		printPredicate(form, actors, &atomIndex);
	else if(IsTermForm(form))
		printTerm(form, actors, &atomIndex);
	else if(IsClauseForm(form))
		printClause(form, actors, &atomIndex);
	else if(IsConjunctionForm(form))
		printConjunction(form, actors, &atomIndex);
	else
		ASSERT(false);
}


data64 FormulaHashFormActors(data64 formHash, TypedTuple const * actors, size32 nActors, data64 initialHash)
{
	data64 hash = DJB2DoubleHashAdd(&formHash, sizeof(data64), initialHash);
	return TypedTupleHash(actors, hash);
}

/*
size8 FormulaUniqueVariables(Atom formula, TypedAtom * variables)
{
	Atom actorsList = FormulaGetActors(formula);
	ListIterator iterator;
	ListIterate(actorsList, &iterator);
	index8 i = 0;
	while(ListIteratorHasNext(&iterator)) {
		TypedAtom atom = ListIteratorGetElement(&iterator);
		if(IsVariable(atom) && !TypedTupleContainsAtom(variables, i, atom))
			variables[i++] = atom;	
		ListIteratorNext(&iterator);
	}
	ListIteratorEnd(&iterator);
	return i;
}
*/
