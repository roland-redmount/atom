/**
 * Allocation of pages from a persistent memory area,
 * mirrored to disk by mmap.
 */
#ifndef PAGING_H
#define PAGING_H

#include "platform.h"


#define MEMORY_PAGE_SIZE	0x1000					// 4096 bytes
#define PAGE_ADDRESS_MASK	0xFFFFFFFFFFFFF000L		// mask to convert any address to the page address

// location of paging area
#define BASE_ADDRESS		(1 * TB)                // 1024^4 = 0x400^4 = (0x10000)^2 = 0x10_000_000_000
extern byte * const pageTable;

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
void FreePage(void * page);


/**
 * Allocate a number of consecutive pages.
 */
void * AllocatePages(size32 nPages);
void FreePages(void * firstPage, size32 nPages);


/**
 * Number of pages needed fit the required number of bytes
 */
uint32 PagesToFit(size32 nBytes);


#endif  // PAGING_H
