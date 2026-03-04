
/**
 * This is a variable-sized pool of items of equal size.
 * Allocated item addresses are stable, allocation is fast,
 * there is no fragmentation except for headers.
 * An alternative to Allocate() for equal-size items.
 */

#ifndef POOL_H
#define POOL_H

#include "platform.h"

/**
 * Create an allocation pool. The item size must be smaller
 * than a memory page.
 */
void * CreatePool(size16 itemSize);

/**
 * Free an allocation pool and all its allocated items.
 */
void FreePool(void * pool);

/**
 * Allocate one item
 */
void * PoolAllocate(void * pool);

/**
 * Free one allocated item
 */
void PoolFreeItem(void * pool, void * item);

/**
 * The number of items currently in the pool.
 */
size32 PoolNItems(void * pool);


#endif	// POOL_H
