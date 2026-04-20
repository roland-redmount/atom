/**
 * A VM context for C-level (machine code) services.
 * Stores an argument vector + a state memory block (for BTreeIterator, &c)
 */

 #ifndef CCONTEXT_H
 #define CCONTEXT_H

 #include "kernel/ServiceRegistry.h"
 #include "lang/Atom.h"


Atom CreateCContext(ServiceRecord * service);

TypedAtom * CContextArguments(Atom cContext);

void CContextCall(Atom cContext);


 #endif	// CCONTEXT_H


