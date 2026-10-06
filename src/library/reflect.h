/**
 * Library of machine services for reflection
 */

#ifndef REFLECT_H
#define REFLECT_H

/**
 * Register the services.
 */
void ReflectionSetup(void);

/**
 * Restore the reflection services from the paging area upon restart.
 */
void ReflectionRestore(void);

void ReflectionShutdown(void);


/* CLAUDE: Register the functions this module stores in persistent memory;
   see memory/references.h */
void RegisterReflectFunctions(void);


#endif	// REFLECT_H
