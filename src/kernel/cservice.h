/**
 * A service implemented by C functions.
 * Consists of a function that creates a context.
 */

#ifndef CSERVICE_H
#define CSERVICE_H

#include "lang/Atom.h"

/**
 * A C service context consists of function pointers
 * for CCTX, CALL and END, same for all contexts using the
 * same implementation), and a memory block for storing
 * state.
 * 
 * To call a C service from bytecode (from VMExecute) we must
 * set arguments and do a C function call. 
 */

typedef struct CServiceContext_s CServiceContext;

typedef struct CService_s {
	CServiceContext * createContext(CService * service);
	// returns true if the flag should be set
	bool call(CServiceContext * context, Atom const * queryTuple);


	Atom getatom(RelationBTreeIterator const * iterator, index8 i);
	void getTuple(RelationBTreeIterator const * iterator, Atom * tuple);

	void end(RelationBTreeIterator * iterator);
} CService;



struct CServiceContext_s {
	CService * service;
	byte state[];
}


#endif	// CSERVICE_H
