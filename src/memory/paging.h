/**
 * Allocation of pages from a persistent memory area,
 * mirrored to disk by mmap.
 */

#ifndef PAGING_H
#define PAGING_H

#include "platform.h"


#define MEMORY_PAGE_SIZE	0x1000					// 4096 bytes

/**
 * The address to use for the paging area for persistent memory.
 * 1 * TB = 1024^4 = 0x400^4 = (0x10000)^2 = 0x10_000_000_000
 * 
 * NOTE: this is not a valid address in 32-bit builds such as Emscripten.
 * Therefore, 32-bit builds must run with TRANSIENT_MEMORY.
 */
#define FIXED_PAGING_ADDRESS	((void *) (uintptr_t) (1 * TB))

extern byte * pageTable;

// TODO: for now we have a fixed memory size, as I'm not sure how to
// maintain a filemapping when expanding the virtual memory area
#define MEMORY_SIZE        (10 * MB)
#define MEMORY_N_PAGES     (MEMORY_SIZE / MEMORY_PAGE_SIZE)

#define PAGING_FILE_NAME   "atom_page_file"

 // Initialize new paging area; don't use a page file
#define TRANSIENT_MEMORY			1

// Initialize new paging area, and write it to a page file
#define NEW_PERSISTENT_MEMORY		2

// Read the paging area back from an existing a page file
#define RESTART_PERSISTENT_MEMORY	3


/**
 * Initialize paged memory, or restore from the page file.
 * 
 * If memoryPersistence != TRANSIENT_MEMORY, this returns false
 * if the page file does not exists.
 */
bool InitializePaging(uint32 memoryPersistence);

/**
 * Release the paging area set up by InitializePaging(). Every pointer
 * into the paging area is invalid afterwards.
 */
void ShutdownPaging(void);

// Persistent data version number. Must be increased whenever the layout of
// a persistent structure, a PersistentStateKey or a registered function name changes,
// since a paging file written by an earlier version can then not be restored.
#define PERSISTENCE_VERSION	1

/**
 * Keys for the array of persistent state slots stored in the paging area.
 * Each slot stores a pointer to an allocated data structure.
 */
typedef enum e_PersistentStateKey {
	STATE_KEY_ALLOCATOR = 0,
	STATE_KEY_KERNEL,
	STATE_KEY_REFERENCES,
	STATE_KEY_NAMES,
	STATE_KEY_FORMULAS,
	STATE_KEY_IFACTS,
	STATE_KEY_LOOKUP,
	STATE_KEY_DICTIONARY,
	STATE_KEY_RELATIONS,
	STATE_KEY_SERVICES,
	STATE_KEY_TUPLE_STORES,
	STATE_KEY_STORAGE_PROVIDERS,
	STATE_KEY_MACHINE_SERVICES,
	STATE_KEY_LIST,
	STATE_KEY_STRING,
	STATE_KEY_MATH,
	STATE_KEY_REFLECT,
	STATE_KEY_SESSION,
	N_STATE_KEYS
} PersistentStateKey;

void * GetPersistentState(PersistentStateKey key);
void SetPersistentState(PersistentStateKey key, void * state);


/**
 * Allocate single pages
 */
void * AllocatePage(void);
void FreePage(void const * page);


/**
 * Allocate a number of consecutive pages.
 */
void * AllocatePages(size32 nPages);
void FreePages(void const * firstPage, size32 nPages);


/**
 * Return true if the address lies in the paging area
 */
bool IsPagedMemoryAddress(void const * address);


/**
 * Get an aligned pointer to the page containing the given address, which may be anywhere in the page.
 * The returned pointer is built from the paging memory area, so a caller holding a
 * const pointer into a page can still write to the page it owns; see PoolFreeItem().
 */
void * GetPageOfAddress(void const * address);


/**
 * Number of pages needed fit the required number of bytes
 */
uint32 PagesToFit(size32 nBytes);


#endif  // PAGING_H
