

#ifndef LINKEDLIST_H
#define LINKEDLIST_H

#include "platform.h"


/**
 * A "container" structure for singly linked lists
 * This avoids (1) code duplication for common operations such as
 * find last element, insert, etc, and allows for the "item"
 * of each node to be kept const while the links may be altered
 */
typedef struct s_LinkedList LinkedList;

LinkedList * CreateLinkedList(const void * item);

const void * GetLinkedListItem(LinkedList * list);
LinkedList * GetNextLinkedList(LinkedList * list);
bool LinkedListHasNext(LinkedList * list);

LinkedList* AppendToLinkedList(LinkedList ** list, const void * item);

// function type used by FreeLinkedList()
typedef void (ItemFunction)(const void *);

// deallocate a linked list, calling the supplied freeItem() function for each item
void FreeLinkedList(const LinkedList * list, ItemFunction * freeItem);

// function type used by FindLinkedListItem()
typedef bool (ItemEqualityTest)(const void *, const void *);

// find an item in linked list
// this returns a pointer-to-pointer to allow modifying the list
LinkedList ** FindLinkedListItem(LinkedList ** list, const void * item, ItemEqualityTest * compare);


#endif	// LINKEDLIST_H
