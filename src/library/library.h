/**
 * Load primitive service libraries. This is a temporary fix to make sure all
 * services we use for testing purposes are loaded. PrintTypedAtom() in particular
 * assumes these are in place, or it will segfault.
 */

#ifndef LIBRARY_H
#define LIBRARY_H

void LoadLibraries(void);

void UnloadLibraries(void);


#endif	// LIBRARY_H
