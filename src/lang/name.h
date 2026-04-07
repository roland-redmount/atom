/**
 * The atom type DT_NAME
 * 
 * This should be a list (name n -> list n) but for bootstrapping purposes,
 * we implement this with a btree of C strings. We should implement services
 * that provide (list position element) and other key list relations for names.
 */

#ifndef NAME_H
#define NAME_H

#include "lang/TypedAtom.h"

void InitializeNameStorage(void);
void FreeNameStorage(void);

size32 NumberOfNames(void);

/**
 * Create a name from an existing char string and length.
 */
Atom CreateName(char const * string, size32 length);

/**
 * Create from a C string. Copies the string.
 */
Atom CreateNameFromCString(char const * cString);

void NameAcquire(Atom name);
void NameRelease(Atom name);
uint32 NameTotalReferenceCount(void);

bool IsName(TypedAtom atom);

void PrintName(Atom name);

data64 NameHashFromCString(char const * cString, data64 initialHash);


/**
 * For debugging
 */
void NameDump(void);


#endif	// NAME_H
