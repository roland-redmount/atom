/**
 * The dictionary stores logic rules (clauses).
 */

#ifndef DICTIONARY_H
#define DICTIONARY_H

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
	Atom clauseForm;
	BTreeIterator btreeIterator;
} DictionaryIterator;

/**
 * Iterate over clauses of a given form
 */
void DictionaryIterate(Atom clauseForm, DictionaryIterator * iterator);



#endif	// DICTIONARY_H
