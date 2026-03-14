
#include "datumtypes/register.h"


Atom CreateRegister(index8 index)
{
	return (Atom) {.type = DT_REGISTER, .datum = (data64) index};
}


bool IsRegister(Atom a)
{
	return a.type == DT_REGISTER;
}


index8 RegisterGetIndex(Atom _register)
{
	return (index8) _register.datum;
}


void PrintRegister(Atom _register)
{
	PrintChar('#');
	PrintF("%u", RegisterGetIndex(_register));
}
