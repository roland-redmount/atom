/**
 * Relocation of function references (pointers) in persistent memory.
 *
 * A function address changes from one run of the program to the next, so a function
 * pointer stored in the paging area becomes invalid once a paging file is restored.
 * The reference table records each such pointer: the address of the slot holding the
 * pointer, and the name of the function. On restart, ResolveReferences() writes the
 * current address of each named function back into its slot.
 *
 * A function is known by name once its module has passed it to RegisterFunctions().
 * The name is chosen by the module, and must stay the same between program versions
 * for a paging file to be restored.
 *
 * Two rules keep the reference table correct:
 * 1. A function pointer is written to the paging area only by SetReference().
 * 2. A slot must not move. A function pointer is never stored inside a B-tree item
 *    or a ResizingArray, since these move their contents.
 * A structure holding a slot calls ClearReferences() before the structure is freed.
 */

#ifndef REFERENCES_H
#define REFERENCES_H

#include "platform.h"


// A generic function pointer type. Any function pointer is cast to AnyFunction.
typedef void (*AnyFunction)(void);

typedef struct s_NamedFunction {
	char const * name;
	AnyFunction function;
} NamedFunction;


/**
 * Make the given functions known by name. Registering a function again under the
 * same name does nothing. The registry is kept in process memory, not in the paging area.
 */
void RegisterFunctions(NamedFunction const functions[], size32 nFunctions);


/**
 * Create and free the reference table. The reference table is the module state
 * STATE_KEY_REFERENCES. FreeReferences() requires every reference to be cleared first.
 */
void InitializeReferences(void);
void FreeReferences(void);

size32 NumberOfReferences(void);

/**
 * Write the current address of each registered function into its slot in the  paging area.
 * Every function must have been registered first.
 * Returns false if some recorded function is not registered, after printing a message.
 */
bool ResolveReferences(void);


/**
 * Write the function pointer to the given slot in page-allocated memory,
 * and record the slot in the reference table. The function must be registered,
 * and the slot address must be in paging memory. InitializeReferences() must
 * have been called before using this function.
 * Setting function = 0 zeroes the pointer slot and removes its record.
 */
void SetReference(void * slot, AnyFunction function);

/**
 * Remove the function records for every known address within the given memory block.
 */
void ClearReferences(void const * start, size32 nBytes);

/**
 * Return true if any recorded slot lies within the given memory block.
 */
bool HasReferences(void const * start, size32 nBytes);


#endif  // REFERENCES_H
