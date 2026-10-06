#include "btree/btree.h"
#include "kernel/dictionary.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/ifact.h"
#include "kernel/operator.h"
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/TupleStore.h"
#include "kernel/typedtuple.h"
#include "lang/formula.h"
#include "lang/ClauseForm.h"
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
static void invalidateClauseServices(Atom clauseForm)
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


bool ClauseFormExistsForTermForm(Atom termForm)
{
	// CLAUDE: Scan every (multiset element multiple) tuple for a clause form holding the
	// given term form. This is the existence-only counterpart of findMatchingClauses()
	// in compiler.c, and is likewise a full scan for want of an element index; see the
	// TODO there.

	Operator const * multisetOperator = GetCoreOperator(SERVICE_MULTISET_ID_ALL);
	if(!multisetOperator) {
		// The service does not exist during kernel bootstrapping
		return false;
	}
	Atom multisetQueryTuple[3];
	OperatorContext * multisetContext = OperatorCreateContext(multisetOperator, multisetQueryTuple);
	bool found = false;
	while(OperatorCall(multisetContext)) {
		Atom element = multisetQueryTuple[
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_ELEMENT)];
		if(!SameAtoms(element, termForm))
			continue;
		Atom clauseForm = multisetQueryTuple[
			CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET)];
		if(!IsClauseForm(clauseForm))
			continue;
		found = true;
		break;
	}
	OperatorFreeContext(multisetContext);
	return found;
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
	invalidateClauseServices(entry.form);
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
	invalidateClauseServices(clause->form);
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
	CreateResizingArray(&relations, sizeof(Relation), 4);
	RelationIterator iterator;
	RelationRegistryIterate(ifactRule->form, &iterator);
	while(RelationIteratorNext(&iterator)) {
		Relation relation = RelationIteratorGet(&iterator);
		ResizingArrayAppend(&relations, &relation);
	}
	RelationIteratorEnd(&iterator);

	for(index32 i = 0; i < relations.nElements; i++) {
		Relation const * relation = ResizingArrayGetElement(&relations, i);
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
