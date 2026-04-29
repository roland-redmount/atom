/**
 * The dictionary stores logic rules (clauses).
 */

#ifndef DICTIONARY_H
#define DICTIONARY_H

#include "kernel/tuple.h"
#include "lang/Atom.h"
#include "btree/btree.h"


/**
 * Setup an empty dictionary.
 */
void SetupDictionary(void);

/**
 * Add a clause (formula) to the dictionary
 */
void DictionaryAddClause(Atom clause);

/**
 * Remove a single clause from the dictionary
 */
void DictionaryRemoveClause(Atom clause);


typedef struct {
	byte * keyRecord;
	BTreeIterator btreeIterator;
} DictionaryIterator;

/**
 * Iterate over clauses of a given form
 */
void DictionaryIterate(Atom clauseForm, DictionaryIterator * iterator);

bool DictionaryIteratorHasRecord(DictionaryIterator * iterator);

Tuple const * DictionaryIteratorPeekActors(DictionaryIterator * iterator);

void DictionaryIteratorNext(DictionaryIterator * iterator);

void DictionaryIteratorEnd(DictionaryIterator * iterator);


#endif	// DICTIONARY_H
