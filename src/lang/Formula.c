#include "kernel/ifact.h"
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
#include "memory/allocator.h"
#include "util/hashing.h"
#include "util/sort.h"


Formula * CreateFormula(Atom form, TypedTuple const * actors)
{
	Formula * formula = Allocate(sizeof(Formula));
	formula->form = form;
	formula->actors = CreateTypedTuple(actors->nAtoms);
	TypedTupleCopy(actors, formula->actors);
	IFactAcquire(form);
	return formula;
}


Formula * CreateFormulaFromArray(Atom form, TypedAtom const * actors)
{
	Formula * formula = Allocate(sizeof(Formula));
	formula->form = form;
	formula->actors = CreateTypedTupleFromArray(actors, FormArity(form));
	IFactAcquire(form);
	return formula;
}


bool FormulaEqual(Formula const * formula1, Formula const * formula2)
{
	return (formula1->form.hash == formula2->form.hash) &&
		TypedTupleEqual(formula1->actors, formula2->actors);
}


void FreeFormula(Formula * formula)
{
	IFactRelease(formula->form);
	FreeTypedTuple(formula->actors);
}


bool FormulaIsPredicate(Formula const * formula)
{
	return IsPredicateForm(formula->form);
}


bool FormulaIsTerm(Formula const * formula)
{
	return IsTermForm(formula->form);
}


bool FormulaIsClause(Formula const * formula)
{
	return IsClauseForm(formula->form);
}


bool FormulaIsConjunction(Formula const * formula)
{
	return IsConjunctionForm(formula->form);
}


index32 FormulaRoleIndex(Formula const * formula, Atom roleName)
{
	// TODO: currently this only supports predicates.
	// Need to implement GetClauseRoleIndex() &c
	ASSERT(FormulaIsPredicate(formula))
	return PredicateRoleIndex(formula->form, roleName);
}


Formula * CreatePredicate(Atom const * roleNames, TypedAtom * actors, size8 arity)
{
	Atom predicateForm = CreatePredicateForm(roleNames, arity);

	index8 roleOrder[arity];
	MultisetIterationOrder(predicateForm, AT_NAME, roleNames, roleOrder, arity);

	TypedAtom actorsOrdered[arity];
	CopyMemory(actors, actorsOrdered, arity * sizeof(TypedAtom));
	ReorderArray(actorsOrdered, roleOrder, arity, sizeof(TypedAtom));

	Formula * predicate = CreateFormulaFromArray(predicateForm, actorsOrdered);
	IFactRelease(predicateForm);
	return predicate;
}


Formula * CreateTerm(Formula const * predicate, bool sign)
{
	ASSERT(FormulaIsPredicate(predicate));
	Atom termForm = CreateTermForm(predicate->form, sign);

	Formula * term = CreateFormula(termForm, predicate->actors);
	IFactRelease(termForm);
	return term;
}


Atom TermGetRoleActor(Atom termForm, Atom const termActors[], const char * role, uint8 m)
{
	ASSERT(m > 0)
	Atom predicateForm = TermFormGetPredicateForm(termForm);
	Atom roleName = CreateNameFromCString(role);
	index8 actorIndex = PredicateRoleIndex(predicateForm, roleName) + (m - 1);
	NameRelease(roleName);
	return termActors[actorIndex];
}


Formula * CreateClause(Formula const ** terms, size8 nTerms)
{
	// collect term forms and their arities
	Atom termForms[nTerms];
	size8 termArities[nTerms];
	size8 clauseArity = 0;
	for(index8 i = 0; i < nTerms; i++) {
		termForms[i] = terms[i]->form;
		termArities[i] = terms[i]->actors->nAtoms;
		ASSERT(clauseArity < 255 - termArities[i]);
		clauseArity += termArities[i];
	}
	Atom clauseForm = CreateClauseForm(termForms, nTerms);

	// Collect actors from terms into a single array
	TypedAtom actors[clauseArity];
	for(index8 i = 0, k = 0; i < nTerms; i++) {
		for(index8 j = 0; j < termArities[i]; j++)
			actors[k++] = TypedTupleGetElement(terms[i]->actors, j);
	}

	// reorder actors to match the name order of clauseForm
	index8 termOrder[nTerms]; 
	// find ordering
	MultisetIterationOrder(clauseForm, AT_ID, termForms, termOrder, nTerms);
	// reorder actors
	size32 blockSizes[nTerms];
	for(index8 i = 0; i < nTerms; i++)
		blockSizes[i] = termArities[i] * sizeof(TypedAtom);
	ReorderRaggedArray(actors, termOrder, blockSizes, nTerms);

	Formula * clause = CreateFormulaFromArray(clauseForm, actors);
	IFactRelease(clauseForm);
	return clause;
}


// NOTE: this is very similar to CreateClause, could be refactored
Formula * CreateConjunction(Formula const ** clauses, size8 nClauses)
{
	// collect clause forms and their arities
	Atom clauseForms[nClauses];
	size8 clauseArities[nClauses];
	size8 conjunctionArity = 0;
	for(index8 i = 0; i < nClauses; i++) {
		clauseForms[i] = clauses[i]->form;
		clauseArities[i] = clauses[i]->actors->nAtoms;
		ASSERT(conjunctionArity < 255 - clauseArities[i]);
		conjunctionArity += clauseArities[i];
	}
	Atom conjunctionForm = CreateConjunctionForm(clauseForms, nClauses);

	// collect actors from terms into a single array
	TypedAtom actors[conjunctionArity];
	for(index8 i = 0, k = 0; i < nClauses; i++) {
		for(index8 j = 0; j < clauseArities[i]; j++)
			actors[k++] = TypedTupleGetElement(clauses[i]->actors, j);
	}

	// reorder actors to match the name order of clauseForm
	index8 clauseOrder[nClauses]; 
	// find ordering
	MultisetIterationOrder(conjunctionForm, AT_ID, clauseForms, clauseOrder, nClauses);
	// reorder actors
	size32 blockSizes[nClauses];
	for(index8 i = 0; i < nClauses; i++)
		blockSizes[i] = clauseArities[i] * sizeof(TypedAtom);
	ReorderRaggedArray(actors, clauseOrder, blockSizes, nClauses);

	Formula * conjunction = CreateFormulaFromArray(conjunctionForm, actors);
	IFactRelease(conjunctionForm);
	return conjunction;
}


index8 ClauseGetTermIndex(Atom clauseForm, Atom termForm, uint8 m)
{
	// iterate over terms in the clause to compoute the index
	MultisetIterator iterator;
	MultisetIterate(clauseForm, AT_ID, &iterator);

	index8 index = 0;
	bool found = false;
	ElementMultiple elementMultiple;
	while(MultisetIteratorNext(&iterator)) {
		elementMultiple = MultisetIteratorGetElement(&iterator);
		if(elementMultiple.element.hash == termForm.hash) {
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
	MultisetIterate(clauseForm, AT_ID, &iterator);

	index8 index = 0;
	bool found = false;
	ElementMultiple elementMultiple;
	while(MultisetIteratorNext(&iterator)) {
		elementMultiple = MultisetIteratorGetElement(&iterator);
		if(elementMultiple.element.hash == termForm.hash) {
			found = true;
			break;
		}
		size8 termArity = TermFormArity(elementMultiple.element);
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
	MultisetIterate(clauseForm, AT_ID, &iterator);

	index8 k = 0;
	termActorsIndices[k] = 0;
	ElementMultiple elementMultiple;
	while(MultisetIteratorNext(&iterator)) {
		elementMultiple = MultisetIteratorGetElement(&iterator);
		size8 termArity = TermFormArity(elementMultiple.element);
		for(index8 i = 0; i < elementMultiple.multiple; i++) {
			termActorsIndices[k + 1] = termActorsIndices[k] + termArity;
			k++;
		}
	}
	MultisetIteratorEnd(&iterator);
}


uint8 FormulaArity(Formula const * formula)
{
	return FormArity(formula->form);
}


/**
 * Print a predicate with actors in the order given by atomIndex
 */
static void printPredicate(Atom predicateForm, TypedTuple const * actors, index8 * atomIndex)
{	
	MultisetIterator iterator;
	MultisetIterate(predicateForm, AT_NAME, &iterator);

	size8 nRoles = PredicateNRoles(predicateForm);
	for(index8 i = 0; i < nRoles; i++) {	
		ASSERT(MultisetIteratorNext(&iterator))
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			PrintName(em.element);
			PrintChar(' ');
			PrintTypedAtom(TypedTupleGetElement(actors, *atomIndex));
			if((i < nRoles - 1) || (j < em.multiple - 1))
				PrintChar(' ');
			(*atomIndex)++;
		}
	}
	MultisetIteratorEnd(&iterator);
}


static void printTerm(Atom termForm, TypedTuple const * actors, index8 * atomIndex)
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
	MultisetIterate(clauseForm, AT_ID, &iterator);

	size8 nTermForms = ClauseFormNTermForms(clauseForm);
	for(index8 i = 0; i < nTermForms; i++) {	
		ASSERT(MultisetIteratorNext(&iterator))
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			printTerm(em.element, actors, atomIndex);
			if((j < em.multiple - 1) || (i < nTermForms - 1))
				PrintCString(" | ");
		}
	}
	MultisetIteratorEnd(&iterator);
}


static void printConjunction(Atom conjunctionForm, TypedTuple const * actors, index8* atomIndex)
{
	MultisetIterator iterator;
	MultisetIterate(conjunctionForm, AT_ID, &iterator);

	size8 nClauseForms = ConjunctionFormNUniqueClauseForms(conjunctionForm);
	for(index8 i = 0; i < nClauseForms; i++) {	
		ASSERT(MultisetIteratorNext(&iterator))
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		for(index8 j = 0; j < em.multiple; j++) {
			printClause(em.element, actors, atomIndex);
			if((j < em.multiple - 1) || (i < nClauseForms - 1))
				PrintCString(" & ");
		}
	}
	MultisetIteratorEnd(&iterator);
}


/**
 * Traverse and print a formula
 */
void PrintFormula(Formula const * formula)
{
	PrintFormActorsAsFormula(formula->form, formula->actors);
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
