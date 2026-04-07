
#include "kernel/ifact.h"
#include "kernel/string.h"
#include "kernel/multiset.h"
#include "lang/Formula.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "parser/PredicateBuilder.h"
#include "parser/PartBuilder.h"
#include "parser/Tokenizer.h"
#include "util/sort.h"



#define INITIAL_N_ACTORS	5

void InitializePredicateBuilder(PredicateBuilder * builder)
{
	InitializePartBuilder(&(builder->partBuilder));
	CreateResizingArray(&(builder->roles), sizeof(Datum), INITIAL_N_ACTORS);
	CreateResizingArray(&(builder->actors), sizeof(TypedAtom), INITIAL_N_ACTORS);
	builder->isValid = false;
}


bool PredicateBuilderPush(PredicateBuilder * builder, Token token)
{
	if(PartBuilderPush(&(builder->partBuilder), token)) {
		if(PartBuilderComplete(&(builder->partBuilder))) {
			Datum role = PartBuilderGetRole(&(builder->partBuilder));
			NameAcquire(role);
			ResizingArrayAppend(&(builder->roles), &role);
			
			TypedAtom actor = PartBuilderGetActor(&(builder->partBuilder));
			AcquireTypedAtom(actor);
			ResizingArrayAppend(&(builder->actors), &actor);

			PartBuilderReset(&(builder->partBuilder));
			builder->isValid = true;
		}
		else
			builder->isValid = false;
		return true;
	}
	else
		return false;
}


// arity is always equal to number of actors
static size8 predicateArity(PredicateBuilder const * builder)
{
	ASSERT(builder->isValid);
	size32 size = ResizingArrayNElements(&(builder->actors));
	ASSERT(size <= 255);
	return size;
}


bool PredicateBuilderIsValid(PredicateBuilder const * builder)
{
	return builder->isValid;
}

bool PredicateBuilderIsEmpty(PredicateBuilder const * builder)
{
	return (
		(predicateArity(builder) == 0) &&
		PartBuilderIsEmpty(&(builder->partBuilder))
	);
}


/**
 * Create a DT_FORMULA from the lists of roles and actors stored in this builder.
 */
Datum PredicateBuilderCreateFormula(PredicateBuilder const * builder)
{
	size8 arity = predicateArity(builder);
	Datum const * roles = ResizingArrayGetMemory(&(builder->roles));
	Datum form = CreatePredicateForm(roles, arity);

	// determine the order of roles used by multiset
	TypedAtom roleAtoms[arity];
	for(index8 i = 0; i < arity; i++)
		roleAtoms[i] = (TypedAtom) {.type = DT_NAME, .datum = roles[i]};
	index8 order[arity]; 
	MultisetIterationOrder(form, roleAtoms, order, arity);

	// ordered list of atoms
	TypedAtom actors[arity];
	for(index8 i = 0; i < arity; i++) {
		TypedAtom actor = *((TypedAtom *) ResizingArrayGetElement(&(builder->actors), order[i]));
		actors[i] = actor;
	}
	
	Datum formula = CreateFormulaFromArray(form, actors);
	IFactRelease(form);
	return formula;
}


// a builder can be reset in any state
void PredicateBuilderReset(PredicateBuilder * builder)
{
	PartBuilderReset(&(builder->partBuilder));
	size32 nElements = ResizingArrayNElements(&(builder->roles));

	for(index8 i = 0; i < nElements; i++) {
		Datum role = *((Datum *) ResizingArrayGetElement(&(builder->roles), i));
		NameRelease(role);
	}
	ResizingArrayReset(&(builder->roles));

	for(index8 i = 0; i < nElements; i++) {
		TypedAtom actor = *((TypedAtom *) ResizingArrayGetElement(&(builder->actors), i));
		ReleaseTypedAtom(actor);
	}
	ResizingArrayReset(&(builder->actors));

	builder->isValid = false;
}


void CleanupPredicateBuilder(PredicateBuilder * builder)
{
	PredicateBuilderReset(builder);
	FreeResizingArray(&(builder->roles));
	FreeResizingArray(&(builder->actors));
}


Datum CStringToPredicate(char const * string)
{
	size32 length = CStringLength(string);
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer);
	PredicateBuilder builder;
	InitializePredicateBuilder(&builder);
	// NOTE: including the 0 terminator
	for(index32 i = 0; i <= length; i++) {
		ASSERT(TokenizerPush(&tokenizer, string[i]));
		if(TokenizerComplete(&tokenizer)) {
			Token token = TokenizerGetToken(&tokenizer);
			ASSERT(PredicateBuilderPush(&builder, token));
			ReleaseToken(token);
			TokenizerReset(&tokenizer);
		}
	}
	ASSERT(PredicateBuilderIsValid(&builder));
	Datum predicate = PredicateBuilderCreateFormula(&builder);
	
	CleanupPredicateBuilder(&builder);
	TokenizerCleanup(&tokenizer);
	return predicate;
}

