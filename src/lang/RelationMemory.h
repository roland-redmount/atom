/**
 * The relation memory, a cache for facts resident in memory
 *
 * NOTE: this can be broken down into
 * (1) storage for forms, which can be used for all formulas;
 * (2) storage for facts, which does not store non-fact formulas (queries etc)
 */

#ifndef RELATIONMEMORY_H
#define RELATIONMEMORY_H

#include "Formula.h"



// function prototypes

// store a fact, return a handle used to 
void* StoreFact(Formula* fact);



#endif	// RELATIONMEMORY_H
