
#include "Datum.h"


int8 CompareDatums(Datum datum1, Datum datum2)
{
	if(datum1 < datum2)
		return -1;
	if(datum1 > datum2)
		return 1;
	return 0;
}


static void shiftDatumArrayLeft(Datum * array, uint8 nDatums, uint8 steps)
{
	for(index8 i = 0; i < nDatums - steps; i++)
		array[i] = array[i + steps];
}


/**
 * Reduce a list of datums in-place so that each datum occurs only once,
 * assuming that any duplicated datums are adjacent in the array.
 * Writes the multiplicities of each datum to the
 * provided multiplicities array and returns the number of unique datums.
 */
uint8 ReduceDatumArray(Datum * datums, uint32 * multiplicities, size8 nDatums)
{
	for(index8 k = 0; k < nDatums; k++) {
		index8 i = k + 1;
		while((i < nDatums) && (datums[k] == datums[i]))
			i++;
		multiplicities[k] = i - k;
		if(multiplicities[k] > 1) {
			shiftDatumArrayLeft(datums + k, nDatums - k, multiplicities[k] - 1);
			nDatums -= (multiplicities[k] - 1);
		}
	}
	return nDatums;
}
