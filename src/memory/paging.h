/**
 * Allocation of pages from a persistent memory area,
 * mirrored to disk by mmap.
 */
#ifndef PAGING_H
#define PAGING_H

#include "platform.h"


#define MEMORY_PAGE_SIZE	0x1000					// 4096 bytes

/* CLAUDE: where the paging area is asked for. A 64-bit build asks for a fixed
   address so that the mapping lands in the same place every run; a 32-bit
   build, such as WebAssembly, has no room for one and takes what it is given.
   Either way the arena actually starts at pageTable, which InitializePaging()
   sets from the mapping that was made. */
#if defined(__wasm__) || (UINTPTR_MAX <= 0xFFFFFFFFu)
#define PAGING_ADDRESS_HINT	0
#else
#define BASE_ADDRESS		(1 * TB)                // 1024^4 = 0x400^4 = (0x10000)^2 = 0x10_000_000_000
#define PAGING_ADDRESS_HINT	((void *) BASE_ADDRESS)
#endif

extern byte * pageTable;

// TODO: for now we have a fixed memory size, as I'm not sure how to
// maintain a filemapping when expanding the virtual memory area
#define MEMORY_SIZE        (10 * MB)
#define MEMORY_N_PAGES     (MEMORY_SIZE / MEMORY_PAGE_SIZE)

#define PAGING_FILE_NAME   "atom_page_file"

/**
 * Initialize new, blank paging memory
 */
void InitializePaging(void);


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
