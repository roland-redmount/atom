#include "btree/btree.h"
#include "kernel/ClosedRelation.h"
#include "kernel/dictionary.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/ifact.h"
#include "kernel/operator.h"
#include "kernel/TypedRelation.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/TupleStore.h"
#include "kernel/typedtuple.h"
#include "lang/formula.h"
#include "lang/ClauseForm.h"
#include "lang/ConjunctionForm.h"
#include "lang/TermForm.h"
#include "lang/TypedAtom.h"
#include "memory/allocator.h"
#include "memory/paging.h"
#include "memory/references.h"
#include "parser/ClauseBuilder.h"
#include "util/ResizingArray.h"


typedef struct s_Dictionary {
	// B-tree for the clause rules
	BTree * btree;
	// B-tree for the ifact rules, ordered by compareIFactRules()
	BTree * ifactRules;
} Dictionary;


static Dictionary * dictionary = 0;


static int8 compareEntries(FormulaView const * entry, FormulaView const * entryOrKey)
{
	if(entry->form.hash < entryOrKey->form.hash)
		return -1;
	else if(entry->form.hash > entryOrKey->form.hash)
		return 1;
	else {
		if(!entryOrKey->actors) {
			// no tuple provided
			return 0;
		}
		return TypedTupleCompare(entry->actors, entryOrKey->actors);
	}
}


/**
 * Index of the generator in an actors tuple. A generator must be present.
 */
index8 IFactRuleFindGeneratorIndex(TypedTuple const * actors)
{
	for(index8 i = 0; i < actors->nAtoms; i++) {
		if(SameTypedAtoms(TypedTupleGetElement(actors, i), generatorAtom))
			return i;
	}
	ASSERT(false)
	return 0;
}


/**
 * Order ifact rules by term form, then by generator index. A key with no
 * actors matches every ifact rule of its term form.
 */
static int8 compareIFactRules(FormulaView const * entry, FormulaView const * entryOrKey)
{
	if(entry->form.hash < entryOrKey->form.hash)
		return -1;
	if(entry->form.hash > entryOrKey->form.hash)
		return 1;
	if(!entryOrKey->actors)
		return 0;
	index8 generatorIndex = IFactRuleFindGeneratorIndex(entry->actors);
	index8 keyGeneratorIndex = IFactRuleFindGeneratorIndex(entryOrKey->actors);
	if(generatorIndex < keyGeneratorIndex)
		return -1;
	if(generatorIndex > keyGeneratorIndex)
		return 1;
	return 0;
}


static int8 btreeCompareItems(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareEntries(item, itemOrKey);
}


static int8 btreeCompareIFactRules(void const * item, void const * itemOrKey, size32 itemSize)
{
	return compareIFactRules(item, itemOrKey);
}


static void btreeFreeItem(void const * item, size32 itemSize)
{
	FormulaView const * entry = item;
	IFactRelease(entry->form);
	FreeTypedTuple(entry->actors);
}


static NamedFunction const dictionaryFunctions[] = {
	{"dictionary.btreeCompareItems", (AnyFunction) btreeCompareItems},
	{"dictionary.btreeCompareIFactRules", (AnyFunction) btreeCompareIFactRules},
	{"dictionary.btreeFreeItem", (AnyFunction) btreeFreeItem},
};


void RegisterDictionaryFunctions(void)
{
	RegisterFunctions(dictionaryFunctions, sizeof(dictionaryFunctions) / sizeof(NamedFunction));
}


void SetupDictionary(void)
{
	dictionary = Allocate(sizeof(Dictionary));
	SetPersistentState(STATE_KEY_DICTIONARY, dictionary);
	dictionary->btree = BTreeCreate(sizeof(FormulaView), &btreeCompareItems, &btreeFreeItem);
	dictionary->ifactRules = BTreeCreate(sizeof(FormulaView), &btreeCompareIFactRules, &btreeFreeItem);
}


void RestoreDictionary(void)
{
	dictionary = GetPersistentState(STATE_KEY_DICTIONARY);
	ASSERT(dictionary)
}


void TeardownDictionary(void)
{
	BTreeFree(dictionary->btree);
	BTreeFree(dictionary->ifactRules);
	Free(dictionary);
	SetPersistentState(STATE_KEY_DICTIONARY, 0);
	dictionary = 0;
}

/*
 * Setup a formula to be stored in the dictionary.
 * NOTE: this adds a reference to the entry's form and keeps a copy of its actors,
 * unlike a FormulaView obtained from FormulaGetView()
 */
static void setupEntry(FormulaView * entry, Atom clauseForm, TypedTuple const * actors)
{
	entry->form = clauseForm;
	IFactAcquire(clauseForm);
	TypedTuple * tuple = CreateTypedTuple(actors->nAtoms);
	TypedTupleCopy(actors, tuple);
	entry->actors = tuple;
}


/**
 * Invalidate any compiled services and relations whose term form is contains in thegiven clause form.
 * The compiler resolves a query against the clauses whose form contains
 * the query term form, so it is sufficient to invalidate services associated with any of the
 * term forms in the given clause. See compileQueryClauses().
 */
/* CLAUDE: If invalidateDependents is true, the services of forms depending on the term forms
 * are invalidated too; see InvalidateDependentServices(). */
static void invalidateClauseServices(Atom clauseForm, bool invalidateDependents)
{
	// Collect the term forms before invalidating any service: the multiset iterator
	// evaluates a service of its own, and invalidation removes services
	ResizingArray termForms;
	CreateResizingArray(&termForms, sizeof(Atom), 8);
	MultisetIterator iterator;
	MultisetIterate(clauseForm, AT_ID, &iterator);
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple element = MultisetIteratorGetElement(&iterator);
		ResizingArrayAppend(&termForms, &(element.element));
	}
	MultisetIteratorEnd(&iterator);

	// Invalidate all term forms
	for(index32 i = 0; i < termForms.nElements; i++) {
		Atom termForm = *(Atom *) ResizingArrayGetElement(&termForms, i);
		InvalidateTermFormServices(termForm, INVALIDATE_BY_RULE);
	}
	if(invalidateDependents)
		InvalidateDependentServices(ResizingArrayGetMemory(&termForms), termForms.nElements);
	FreeResizingArray(&termForms);
}


/**
 * Find the entry of the given clause, copying it to *entry if one is given, and return
 * whether the dictionary holds it. The key is the clause's own form and actors, which
 * compareEntries() only reads, so no entry has to be built to look one up.
 */
static bool findEntry(Atom clause, FormulaView * entry)
{
	ASSERT(FormulaIsClause(clause))
	FormulaView key = FormulaGetView(clause);
	if(entry)
		return BTreeGetItem(dictionary->btree, &key, entry);
	else
		return BTreeContainsItem(dictionary->btree, &key);
}


bool DictionaryContainsClause(Atom clause)
{
	return findEntry(clause, 0);
}


/*
 * Append each clause form containing the given term form to clauseForms, and return the
 * number of clause forms found. If clauseForms is 0, the scan stops at the first clause form;
 * this can be used to check of matching clauses exist.
 * The scan reads every (multiset element multiple) tuple, as findMatchingClauses() in
 * compiler.c does, for want of an element index; see the TODO there.
 */
static size32 findClauseFormsForTermForm(Atom termForm, ResizingArray * clauseForms)
{
	Operator const * multisetOperator = GetCoreOperator(SERVICE_MULTISET_ID_ALL);
	if(!multisetOperator) {
		// The service does not exist during kernel bootstrapping
		return 0;
	}
	Atom multisetQueryTuple[3];
	OperatorContext * multisetContext = OperatorCreateContext(multisetOperator, multisetQueryTuple);
	size32 nClauseForms = 0;
	while(OperatorCall(multisetContext)) {
		Atom clauseForm = multisetQueryTuple[
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET)];
		if(!IsClauseForm(clauseForm))
			continue;
		Atom element = multisetQueryTuple[
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_ELEMENT)];
		if(!SameAtoms(element, termForm))
			continue;
		nClauseForms++;
		if(!clauseForms)
			break;
		ResizingArrayAppend(clauseForms, &clauseForm);
	}
	OperatorFreeContext(multisetContext);
	return nClauseForms;
}


bool ClauseFormExistsForTermForm(Atom termForm)
{
	return findClauseFormsForTermForm(termForm, 0) > 0;
}


size32 DictionaryFindClauseForms(Atom termForm, ResizingArray * clauseForms)
{
	ASSERT(clauseForms)
	return findClauseFormsForTermForm(termForm, clauseForms);
}


/*
 * CLAUDE: A term form held by a clause form or a conjunction form, read from the relation
 * (multiset element multiple); see InvalidateDependentServices().
 */
typedef struct s_FormMember {
	Atom form;				// the clause form or conjunction form
	bool isClause;			// true for a clause form, false for a conjunction form
	Atom termForm;
	Atom predicateForm;		// the predicate form of termForm
	bool sign;				// the sign of termForm
	size32 multiple;		// the number of terms of the form with termForm
} FormMember;


/*
 * CLAUDE: A form visited by InvalidateDependentServices(). A term form is identified by
 * predicate form and sign, so that an opposite form need not exist as an atom.
 * The form is 0 for an opposite form whose atom is not known.
 */
typedef struct s_DependentForm {
	Atom form;
	bool isConjunction;
	Atom predicateForm;		// for a term form only
	bool sign;				// for a term form only
} DependentForm;


/*
 * CLAUDE: Append every term form held by a clause form or a conjunction form to members.
 */
static void collectFormMembers(ResizingArray * members)
{
	Operator const * multisetOperator = GetCoreOperator(SERVICE_MULTISET_ID_ALL);
	if(!multisetOperator) {
		// The service does not exist during kernel bootstrapping
		return;
	}
	Atom multisetQueryTuple[3];
	OperatorContext * multisetContext = OperatorCreateContext(multisetOperator, multisetQueryTuple);
	while(OperatorCall(multisetContext)) {
		Atom form = multisetQueryTuple[
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET)];
		bool isClause = IsClauseForm(form);
		if(!isClause && !IsConjunctionForm(form))
			continue;
		FormMember member = {
			.form = form,
			.isClause = isClause,
			.termForm = multisetQueryTuple[
				CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_ELEMENT)],
			.multiple = (size32) multisetQueryTuple[
				CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTIPLE)]._int,
		};
		ResizingArrayAppend(members, &member);
	}
	OperatorFreeContext(multisetContext);

	// The predicate forms are found after the scan, which must not be interrupted by
	// other operator calls
	for(index32 i = 0; i < members->nElements; i++) {
		FormMember * member = ResizingArrayGetElement(members, i);
		member->predicateForm = TermFormGetPredicateForm(member->termForm);
		member->sign = TermFormGetSign(member->termForm);
	}
}


static bool isSameDependentForm(DependentForm const * form1, DependentForm const * form2)
{
	if(form1->isConjunction != form2->isConjunction)
		return false;
	if(form1->isConjunction)
		return SameAtoms(form1->form, form2->form);
	return SameAtoms(form1->predicateForm, form2->predicateForm) && (form1->sign == form2->sign);
}


/*
 * CLAUDE: Append the given form to dependentForms, unless it is there already.
 */
static void addDependentForm(ResizingArray * dependentForms, DependentForm const * form)
{
	for(index32 i = 0; i < dependentForms->nElements; i++) {
		if(isSameDependentForm(ResizingArrayGetElement(dependentForms, i), form))
			return;
	}
	ResizingArrayAppend(dependentForms, form);
}


static DependentForm termFormDependent(Atom termForm, Atom predicateForm, bool sign)
{
	return (DependentForm) {
		.form = termForm, .isConjunction = false, .predicateForm = predicateForm, .sign = sign
	};
}


/*
 * CLAUDE: Append the forms depending directly on the given term form to dependentForms;
 * see InvalidateDependentServices().
 */
static void addDirectDependents(
	DependentForm const * termForm, FormMember const members[], size32 nMembers,
	ResizingArray * dependentForms)
{
	for(index32 i = 0; i < nMembers; i++) {
		FormMember const * member = &members[i];
		if(!SameAtoms(member->predicateForm, termForm->predicateForm))
			continue;
		if(!member->isClause) {
			// A conjunction form holding the term form
			if(member->sign == termForm->sign) {
				DependentForm conjunction = {.form = member->form, .isConjunction = true};
				addDependentForm(dependentForms, &conjunction);
			}
			continue;
		}
		if(member->sign == termForm->sign)
			continue;
		// A clause form holding the opposite term form. Each other term form of the clause
		// form reads the term form as a rule body term, and so does the opposite term form
		// if the clause form holds it more than once.
		for(index32 j = 0; j < nMembers; j++) {
			FormMember const * other = &members[j];
			if(!SameAtoms(other->form, member->form))
				continue;
			if((j == i) && (member->multiple < 2))
				continue;
			DependentForm dependent = termFormDependent(other->termForm, other->predicateForm, other->sign);
			addDependentForm(dependentForms, &dependent);
		}
	}

	// The opposite relation of a closed relation is read by INVERT operators
	if(termForm->form.hash && RelationIsClosedForm(termForm->form)) {
		DependentForm opposite = termFormDependent(
			(Atom) {0}, termForm->predicateForm, !termForm->sign);
		// The atom of the opposite form is known if a clause form or conjunction form holds it
		for(index32 i = 0; i < nMembers; i++) {
			if(SameAtoms(members[i].predicateForm, opposite.predicateForm)
				&& (members[i].sign == opposite.sign)) {
				opposite.form = members[i].termForm;
				break;
			}
		}
		addDependentForm(dependentForms, &opposite);
	}
}


void InvalidateDependentServices(Atom const termForms[], size32 nTermForms)
{
	ResizingArray members;
	CreateResizingArray(&members, sizeof(FormMember), 16);
	collectFormMembers(&members);
	if(members.nElements == 0) {
		// No form depends on another. This is always the case during bootstrap,
		// when TermFormGetPredicateForm() is not available.
		FreeResizingArray(&members);
		return;
	}

	// The given term forms come first in dependentForms, and are not invalidated
	ResizingArray dependentForms;
	CreateResizingArray(&dependentForms, sizeof(DependentForm), 16);
	for(index32 i = 0; i < nTermForms; i++) {
		DependentForm termForm = termFormDependent(
			termForms[i], TermFormGetPredicateForm(termForms[i]), TermFormGetSign(termForms[i]));
		addDependentForm(&dependentForms, &termForm);
	}
	size32 nGivenForms = dependentForms.nElements;

	// Each form added to dependentForms is visited once, in order. A conjunction form
	// has no dependents, since rules hold term forms only.
	for(index32 i = 0; i < dependentForms.nElements; i++) {
		DependentForm form = *(DependentForm *) ResizingArrayGetElement(&dependentForms, i);
		if(!form.isConjunction)
			addDirectDependents(&form, ResizingArrayGetMemory(&members), members.nElements, &dependentForms);
	}
	FreeResizingArray(&members);

	// An opposite form whose atom is not known has no services
	for(index32 i = nGivenForms; i < dependentForms.nElements; i++) {
		DependentForm const * form = ResizingArrayGetElement(&dependentForms, i);
		if(form->form.hash)
			InvalidateTermFormServices(form->form, INVALIDATE_BY_RULE);
	}
	FreeResizingArray(&dependentForms);
}


FormulaView DictionaryAddClause(Atom clause)
{
	// A clause the dictionary already holds is left as it is. Building an entry for it
	// would acquire a reference per actor that inserting it would then have to give back,
	// and would leave the entry it replaced with no owner.
	FormulaView entry;
	if(findEntry(clause, &entry))
		return entry;

	FormulaView clauseView = FormulaGetView(clause);
	setupEntry(&entry, clauseView.form, clauseView.actors);
	ASSERT(BTreeInsert(dictionary->btree, &entry) == BTREE_INSERTED)
	invalidateClauseServices(entry.form, true);
	return entry;
}


FormulaView DictionaryAddClauseFromCString(const char * clauseString)
{
	Atom rule = CStringToClause(clauseString);
	FormulaView entry = DictionaryAddClause(rule);
	ReleaseFormula(rule);	
	return entry;
}


void DictionaryRemoveClause(FormulaView * clause)
{
	// Invalidate before the entry goes: the clause form is released with it, and the
	// compiled services are stale either way
	invalidateClauseServices(clause->form, false);
	ASSERT(BTreeDelete(dictionary->btree, clause, 0) == BTREE_DELETED)
}


bool IsIFactRule(Atom formula)
{
	if(!FormulaIsTerm(formula))
		return false;
	FormulaView view = FormulaGetView(formula);
	if(!TermFormGetSign(view.form))
		return false;
	size8 arity = view.actors->nAtoms;
	if(arity < 2)
		return false;

	size8 nGenerators = 0;
	for(index8 i = 0; i < arity; i++) {
		TypedAtom actor = TypedTupleGetElement(view.actors, i);
		if(SameTypedAtoms(actor, generatorAtom)) {
			nGenerators++;
			continue;
		}
		if(actor.type != AT_VARIABLE)
			return false;
		// no variable may be repeated
		for(index8 j = 0; j < i; j++) {
			if(SameTypedAtoms(actor, TypedTupleGetElement(view.actors, j)))
				return false;
		}
	}
	return nGenerators == 1;
}


FormulaView DictionaryAddIFactRule(Atom rule)
{
	ASSERT(IsIFactRule(rule))
	FormulaView key = FormulaGetView(rule);
	FormulaView entry;
	if(BTreeGetItem(dictionary->ifactRules, &key, &entry))
		return entry;

	setupEntry(&entry, key.form, key.actors);
	ASSERT(BTreeInsert(dictionary->ifactRules, &entry) == BTREE_INSERTED)
	InvalidateTermFormServices(entry.form, INVALIDATE_BY_RULE);
	InvalidateDependentServices(&entry.form, 1);
	return entry;
}


bool DictionaryContainsIFactRule(Atom rule)
{
	ASSERT(IsIFactRule(rule))
	FormulaView key = FormulaGetView(rule);
	return BTreeContainsItem(dictionary->ifactRules, &key);
}


bool IFactRuleExistsForTermForm(Atom termForm)
{
	FormulaView key = {.form = termForm, .actors = 0};
	return BTreeContainsItem(dictionary->ifactRules, &key);
}


/**
 * Release the cached ifacts created by IFACT operators compiled from an ifact rule.
 * These are the cached ifacts in the TupleStore of each relation of the term form of the
 * rule, with the ID column at the generator of the rule; see IFactReleaseCached().
 */
static void releaseIFactRuleCache(FormulaView const * ifactRule)
{
	index8 idColumn = IFactRuleFindGeneratorIndex(ifactRule->actors);
	// Collect the relations first, since releasing an ifact may alter the relation registry
	ResizingArray relations;
	CreateResizingArray(&relations, sizeof(TypedRelation), 4);
	RelationIterator iterator;
	RelationRegistryIterate(ifactRule->form, &iterator);
	while(RelationIteratorNext(&iterator)) {
		TypedRelation relation = RelationIteratorGet(&iterator);
		ResizingArrayAppend(&relations, &relation);
	}
	RelationIteratorEnd(&iterator);

	for(index32 i = 0; i < relations.nElements; i++) {
		TypedRelation const * relation = ResizingArrayGetElement(&relations, i);
		TupleStore * store = RelationGetTupleStore(*relation);
		if(store)
			IFactReleaseCached(store, idColumn);
	}
	FreeResizingArray(&relations);
}


void DictionaryRemoveIFactRule(FormulaView * ifactRule)
{
	// We release the cache while the IFACT services still exist, since releasing an
	// ifact reads its TupleStore by the ID column; see IFactSetupStoreOperator()
	releaseIFactRuleCache(ifactRule);
	// Invalidate before the entry is removed, since the term form is released with it
	InvalidateTermFormServices(ifactRule->form, INVALIDATE_BY_RULE);
	ASSERT(BTreeDelete(dictionary->ifactRules, ifactRule, 0) == BTREE_DELETED)
}


void DictionaryRemoveAll(void)
{
	BTreeClear(dictionary->btree);
	// Release the cached ifacts of each ifact rule; see DictionaryRemoveIFactRule()
	BTreeIterator iterator;
	BTreeIterate(&iterator, dictionary->ifactRules);
	while(BTreeIteratorNext(&iterator))
		releaseIFactRuleCache(BTreeIteratorPeekItem(&iterator));
	BTreeIteratorEnd(&iterator);
	BTreeClear(dictionary->ifactRules);
	RemoveAllCompiledServices();
}


void DictionaryIterateClauses(Atom clauseForm, DictionaryIterator * iterator)
{
	ASSERT(IsClauseForm(clauseForm))
	iterator->key =  (FormulaView) {
		.form = clauseForm,
		.actors = 0
	};
	BTreeIterate(&(iterator->btreeIterator), dictionary->btree);
}


void DictionaryIterateIFactRules(Atom termForm, DictionaryIterator * iterator)
{
	ASSERT(IsTermForm(termForm))
	iterator->key = (FormulaView) {
		.form = termForm,
		.actors = 0
	};
	BTreeIterate(&(iterator->btreeIterator), dictionary->ifactRules);
}


bool DictionaryIteratorNext(DictionaryIterator * iterator)
{
	if(!iterator->btreeIterator.btree)
		return false;
	bool foundItem;
	if(BTreeIteratorBeforeFirst(&(iterator->btreeIterator)))
		foundItem = BTreeIteratorSeek(&(iterator->btreeIterator), &iterator->key);
	else
		foundItem = BTreeIteratorNext(&(iterator->btreeIterator));

	if(foundItem) {
		FormulaView const * btreeEntry = BTreeIteratorPeekItem(&(iterator->btreeIterator));
		if(compareEntries(btreeEntry, &iterator->key) == 0)
			return true;
	}
	return false;
}


TypedTuple const * DictionaryIteratorPeekActors(DictionaryIterator * iterator)
{
	FormulaView * entry = BTreeIteratorPeekItem(&(iterator->btreeIterator));
	return entry->actors;
}


void DictionaryIteratorEnd(DictionaryIterator * iterator)
{
	BTreeIteratorEnd(&(iterator->btreeIterator));
	SetMemory(iterator, sizeof(DictionaryIterator), 0);
}
