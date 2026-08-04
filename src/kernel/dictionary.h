/**
 * The dictionary stores logic rules (clauses).
 */

#ifndef DICTIONARY_H
#define DICTIONARY_H

#include "kernel/typedtuple.h"
#include "lang/Formula.h"
#include "btree/btree.h"

// This structure uniquely defines a dictionary entry
typedef struct  s_DictionaryEntry {
	Atom clauseForm;
	TypedTuple * tuple;
} DictionaryEntry;


/**
 * Setup an empty dictionary.
 */
void SetupDictionary(void);

void TeardownDictionary(void);

/**
 * Add a clause (formula) to the dictionary.
 */
DictionaryEntry DictionaryAddClause(Formula const * clause);

/**
 * Parse a string into a clause (formula) and add it to the dictionary.
 */
DictionaryEntry DictionaryAddClauseFromCString(const char * clauseString);

/**
 * Remove a single clause from the dictionary
 */
void DictionaryRemoveClause(DictionaryEntry * entry);

/**
 * Remove all clauses from the dictionary. Used for testing.
 */
void DictionaryRemoveAll(void);


typedef struct {
	DictionaryEntry key;
	BTreeIterator btreeIterator;
} DictionaryIterator;

/**
 * Iterate over clauses of a given form
 */
void DictionaryIterate(Atom clauseForm, DictionaryIterator * iterator);

bool DictionaryIteratorNext(DictionaryIterator * iterator);

TypedTuple const * DictionaryIteratorPeekActors(DictionaryIterator * iterator);

void DictionaryIteratorEnd(DictionaryIterator * iterator);


#endif	// DICTIONARY_H
