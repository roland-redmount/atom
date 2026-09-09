/**
 * Allocation of pages from a persistent memory area,
 * mirrored to disk by mmap.
 */

#ifndef PAGING_H
#define PAGING_H

#include "platform.h"


#define MEMORY_PAGE_SIZE	0x1000					// 4096 bytes

/* The address to use for the paging area for PERSISTENT_MEMORY.
   CLAUDE: an address space too small to hold the address is left with 0, which
   is the "no address preference" that CreateMappedMemory() takes. Such a build
   has nowhere to keep a paging file anyway, so it never asks for one. */
#define FIXED_PAGING_ADDRESS	((void *) (uintptr_t) (1 * TB))	// 1024^4 = 0x400^4 = (0x10000)^2 = 0x10_000_000_000

extern byte * pageTable;

// TODO: for now we have a fixed memory size, as I'm not sure how to
// maintain a filemapping when expanding the virtual memory area
#define MEMORY_SIZE        (10 * MB)
#define MEMORY_N_PAGES     (MEMORY_SIZE / MEMORY_PAGE_SIZE)

#define PAGING_FILE_NAME   "atom_page_file"

/**
 * Initialize new, blank paging memory. The paging area is mirrored to a file
 * in the data directory for PERSISTENT_MEMORY, and is memory alone for
 * TRANSIENT_MEMORY.
 */
#define TRANSIENT_MEMORY	1
#define PERSISTENT_MEMORY	2

 void InitializePaging(uint32 memoryPersistence);


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
