
#include "memory/pool.h"
#include "memory/paging.h"


typedef struct s_FreeItem FreeItem;

struct s_FreeItem {
	FreeItem * previous;
	FreeItem * next;
};


typedef struct s_PoolPage PoolPage;
struct s_PoolPage {
	size16 itemSize;
	size16 pageNItems;		// no. items used on this page, including free list entries
	FreeItem * firstFreeItem;
	PoolPage * previousPage;
	PoolPage * nextPage;
	PoolPage * lastPage;
};


// The maximum item size that can be stored.
#define MAX_ITEM_SIZE (MEMORY_PAGE_SIZE - sizeof(PoolPage))

static PoolPage * createPage(size16 itemSize)
{
	PoolPage * page = AllocatePage();

	// item size must be at least 16 bytes to fit two pointers for free items
	page->itemSize = itemSize >= 16 ? itemSize : 16;
	page->pageNItems = 0;
	page->firstFreeItem = 0;
	return page;
}


void * CreatePool(size16 itemSize)
{
	ASSERT(itemSize < MAX_ITEM_SIZE)
	PoolPage * page = createPage(itemSize);
	page->previousPage = 0;
	page->nextPage = 0;
	page->lastPage = page;
	return page;
}


void FreePool(void * pool)
{
	PoolPage * firstPage = pool;
	PoolPage * page = firstPage->lastPage;
	while(page) {
		PoolPage * tmp = page;
		page = page->previousPage;
		FreePage(tmp);
	}
}


static size16 pageMaxNItems(PoolPage * page)
{
	return (MEMORY_PAGE_SIZE - sizeof(PoolPage)) / page->itemSize;
}


static bool pageIsFull(PoolPage * page)
{
	return page->pageNItems == pageMaxNItems(page);
}

static void * indexToAddress(PoolPage * page, index16 index)
{
	return (void *) (((addr64) page) + sizeof(PoolPage) + index * page->itemSize);
}


void * PoolAllocate(void * pool)
{
	PoolPage * firstPage = pool;
	if(firstPage->firstFreeItem) {
		// remove item from free list
		FreeItem * item = firstPage->firstFreeItem;
		firstPage->firstFreeItem = item->next;
		if(item->next)
			item->next->previous = 0;
		return item;
	}
	else {
		// Nothing on the free list, so append to last page
		PoolPage * page = firstPage->lastPage;
		if(pageIsFull(page)) {
			// allocate new page
			PoolPage * newPage = createPage(page->itemSize);
			newPage->previousPage = page;
			newPage->nextPage = 0;
			page->nextPage = newPage;
			firstPage->lastPage = newPage;
			page = newPage;
		}
		// page now has at least one free item
		void * item = indexToAddress(page, page->pageNItems);
		page->pageNItems++;
		return item;
	}
}


void PoolFreeItem(void * pool, void * item)
{
	PoolPage * firstPage = pool;

	PoolPage * itemPage = (PoolPage *) (((addr64) item) & PAGE_ADDRESS_MASK); 	
	if(itemPage->pageNItems == 1) {
		// item is the only item on its page
		if(itemPage == firstPage) {
			firstPage->pageNItems = 0;
		}
		else {
			// deallocate the page and update page links
			itemPage->previousPage->nextPage = itemPage->nextPage;
			if(itemPage->nextPage)
				itemPage->nextPage->previousPage = itemPage->previousPage;
			if(firstPage->lastPage == itemPage)
				firstPage->lastPage = itemPage->previousPage;
			FreePage(itemPage);
		}
	}
	else {
		// add the item to the top of the free list
		FreeItem * newFreeItem = item;
		newFreeItem->next = firstPage->firstFreeItem;
		newFreeItem->previous = 0;
		firstPage->firstFreeItem = newFreeItem;
	}
}


static size32 nFreeItems(PoolPage * firstPage)
{
	FreeItem * item = firstPage->firstFreeItem;
	size32 n = 0;
	while(item) {
		n++;
		item = item->next;
	}
	return n;
}


size32 PoolNItems(void * pool)
{
	size32 nItems = 0;
	PoolPage * page = pool;
	while(page) {
		nItems += page->pageNItems;
		page = page->nextPage;
	}
	return nItems - nFreeItems((PoolPage *) pool);
}
