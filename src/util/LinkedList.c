
#include <stdlib.h>

#include "util/LinkedList.h"


// define this structure here to hide implementation
struct s_LinkedList {
	LinkedList * next;
	void const * item;      // pointer to stored data
};


/**
 * Create a linked list with a single item (possibly NULL)
 */
LinkedList * CreateLinkedList(void const * item)
{
	LinkedList *list = malloc(sizeof(LinkedList));
	list->item = item;
	list->next = NULL;
	return list;
}

void const * GetLinkedListItem(LinkedList * list)
{
	return list->item;
}


LinkedList * GetNextLinkedList(LinkedList * list)
{
	return list->next;
}


bool LinkedListHasNext(LinkedList * list)
{
	return list->next != NULL;
}


/**
 * Deallocate a linked list, calling the supplied free() function for each item
 * If freeItem() is NULL, it is ignored
 */
void FreeLinkedList(const LinkedList * list, ItemFunction * freeItem)
{
	// traverse list
	const LinkedList * p = list;
	while(p != NULL) {
		// call supplied deallocation function
		if(freeItem != NULL)
			freeItem(p->item);
		// advance and free structure
		const LinkedList * tmp = p;
		p = p->next;
		free((void*) tmp);
	}
}

/**
 * Find an item in a linked list using a given comparator
 * If compare == NULL, item pointers are compared directly
 * NOTE: here we would really like to pass a closure test(*, item),
 * but there is no way to do that cleanly in C afaik
 * Returns address to pointer to list containing the item,
 * or address to last NULL pointer if not found
 */
LinkedList ** FindLinkedListItem(LinkedList ** list, void const * item, ItemEqualityTest * compare)
{
	// work with address of list pointer
	LinkedList ** p = list;
	// traverse list
	while(*p != NULL) {
		// compare items
		bool equal = (compare != NULL) ?
			compare((*p)->item, item) :
			(*p)->item == item;
		if(equal) {
			// found item
			return p;
		}
		p = &((*p)->next);
	}
	// item not found, return address to NULL pointer
	return p;
}


/**
 * Append an item to the end of a linked list
 * Takes address of list pointer, *list may be NULL
 * Returns a pointer to new added LinkedList at end of list
 */
LinkedList * AppendToLinkedList(LinkedList ** list, void const * item)
{
	// find last list pointer
	LinkedList ** p = list;
	while(*p != NULL)
		p = &((*p)->next);
	// add new list entry
	*p = CreateLinkedList(item);
	return *p;
}
