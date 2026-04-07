
#ifndef VARIABLETYPE_H
#define VARIABLETYPE_H

#include "lang/AtomType.h"

/**
 * This structure associates a variable with a atom type
 */
struct s_VariableType {
	Atom variable;		// possibly quoted
	byte typeId;
};
typedef struct s_VariableType VariableType;


#endif	// VARIABLETYPE_H
