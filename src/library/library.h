/**
 * Load primitive service libraries. This is a temporary fix to make sure all
 * services we use for testing purposes are loaded. PrintTypedAtom() in particular
 * assumes these are in place, or it will segfault.
 */

#ifndef LIBRARY_H
#define LIBRARY_H

/* CLAUDE: Register the functions the libraries store in persistent memory;
   see memory/references.h. LoadLibraries() calls RegisterLibraryFunctions(). */
void RegisterLibraryFunctions(void);

void LoadLibraries(void);

void UnloadLibraries(void);


#endif	// LIBRARY_H
