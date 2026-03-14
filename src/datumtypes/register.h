/**
 * An atom representing a bytecode register.
 * This just encapsulates an integer index identifying the register
 * within a given bytecode program.
 * 
 * NOTE: "register" is a reserved word in C99, so we use "_register"
 */


#ifndef	REGISTER_H
#define	REGISTER_H 

#include "lang/Atom.h"


Atom CreateRegister(index8 index);

bool IsRegister(Atom a);

index8 RegisterGetIndex(Atom _register);

void PrintRegister(Atom _register);


#endif	// REGISTER_H
