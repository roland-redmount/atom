
#include "kernel/Parameter.h"
#include "parser/Characters.h"


void PrintParameter(Atom atom)
{
	PrintF("@%u", atom.parameter.number);
	switch(atom.parameter.io) {
	case PARAMETER_IN:
		PrintChar('<');
		break;
	case PARAMETER_OUT:
		PrintChar('>');
		break;
	case PARAMETER_IN_OUT:
		PrintChar('~');
		break;
	}
	PrintCString(GetAtomTypeName(atom.parameter.atomType));
}
