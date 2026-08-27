/**
 * Convenience functions for creating a list (AT_ID) of letters (AT_LETTER), case-insensitive.
 * This is different from AT_NAME which has separate string storage.
 */

#ifndef STRING_H
#define STRING_H

#include "kernel/Relation.h"
#include "kernel/RelationTable.h"
#include "lang/Atom.h"


/**
 * Create the (string) relation and its service. StringShutdown() removes them.
 * The list relations must already exist; see ListSetup()
 */
void StringSetup(void);

void StringShutdown(void);

/**
 * The role name "string", an AT_NAME atom.
 */
Atom GetStringRoleName(void);

Atom GetStringPredicateForm(void);
Atom GetStringTermForm(void);

Relation const * GetStringRelation(void);

RelationTable * GetStringRelationTable(void);


Atom CreateString(char const * chars, size32 length);
Atom CreateStringFromCString(char const * cString);

bool IsString(Atom atom);

void PrintString(Atom string);

Atom ParseString(char const * syntax, size32 length);


#endif //	STRING_H
