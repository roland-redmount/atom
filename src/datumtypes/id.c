
#include "datumtypes/id.h"


Atom CreateID(Datum id)
{
	return (Atom) {.type = DT_ID, .datum = id};
}
