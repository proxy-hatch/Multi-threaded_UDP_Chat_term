//
// Created by shawn on 23/05/17.
//
#pragma once
#include <stdlib.h>    // NULL macro

//#define DEBUG

#ifdef DEBUG
#define MAXLISTCOUNT 4
#define MAXNODECOUNT 4
#include <stdio.h>  // for printf();
#endif

#ifndef DEBUG
#define MAXLISTCOUNT 10
#define MAXNODECOUNT 100
#endif


//-------------------------------------------------------------------------------------------------
//---------------------------------- Data Structure Declarations ----------------------------------
//-------------------------------------------------------------------------------------------------

// common way to define a struct in C:
// https://stackoverflow.com/questions/1675351/typedef-struct-vs-struct-definitions
typedef struct node {
    void *data;
    struct node *next;
    struct node *prev;
    int boolActive;
    struct list *belong;    // necessary for efficient ListRemove() and other searching functionalities
} node;

typedef struct list {
    node *head;
    node *tail;
    node *curr;
    int nodeCount;
    int boolActive;
} list;

//-------------------------------------------------------------------------------------------------
//---------------------------------- Implementation Declarations ----------------------------------
//-------------------------------------------------------------------------------------------------

// makes a new, empty list, and returns its reference on success. Returns a NULL pointer on failure.
list *ListCreate();

// returns the number of items in list.
int ListCount(const list *aList);

// returns a pointer to the first item in list and makes the first item the current item.
void *ListFirst(list *aList);

// returns a pointer to the last item in list and makes the last item the current one.
void *ListLast(list *aList);

// advances list's current item by one, and returns a pointer to the new current item.
// If this operation advances the current item beyond the end of the list, a NULL pointer is returned.
void *ListNext(list *aList);

// backs up list's current item by one, and returns a pointer to the new current item.
// If this operation backs up the current item beyond the start of the list, a NULL pointer is returned.
void *ListPrev(list *aList);

// returns a pointer to the current item in list.
void *ListCurr(list *aList);

// adds the new item to list directly after the current item, and makes item the current item.
// If the current pointer is before the start of the list, the item is added at the start.
// If the current pointer is beyond the end of the list, the item is added at the end. Returns 0 on success, -1 on failure.
int ListAdd(list *aList, void *anItem);

// adds item to list directly before the current item, and makes the new item the current one.
// If the current pointer is before the start of the list, the item is added at the start.
// If the current pointer is beyond the end of the list, the item is added at the end. Returns 0 on success, -1 on failure.
int ListInsert(list *aList, void *anItem);

// adds item to the end of list, and makes the new item the current one. Returns 0 on success, -1 on failure.
int ListAppend(list *aList, void *anItem);

// adds item to the front of list, and makes the new item the current one. Returns 0 on success, -1 on failure.
int ListPrepend(list *aList, void *anItem);

// Return current item and take it out of list. Make the next item the current one.
void *ListRemove(list *aList);

// adds list2 to the end of list1. The current pointer is set to the current pointer of list1.
// List2 no longer exists after the operation.
void ListConcat(list *list1, list *list2);

// delete list. itemFree is a pointer to a routine that frees an item. It should be invoked (within ListFree) as:
// (*itemFree)(itemToBeFreed);
// Example: https://stackoverflow.com/questions/1789807/function-pointer-as-an-argument
void ListFree(list *aList, void (*itemFree)());

// Return last item and take it out of list. Make the new last item the current one.
void *ListTrim(list *aList);

// searches list starting at the current item until the end is reached or a match is found.
// In this context, a match is determined by the comparator parameter.
// This parameter is a pointer to a routine that takes as its first argument an item pointer, and as its second argument comparisonArg.
// Comparator returns 0 if the item and comparisonArg don't match, or 1 if they do.
// Exactly what constitutes a match is up to the implementor of comparator.
// If a match is found, the current pointer is left at the matched item and the pointer to that item is returned.
// If no match is found, the current pointer is left beyond the end of the list and a NULL pointer is returned.
// Shawn's note: comparator is a pointer to a routine in testbench with parameter data1, data2; comparisonArg is data2
void *ListSearch(list *aList, int (*comparator)(), void *comparisonArg);

