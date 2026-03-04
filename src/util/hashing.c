

#include "util/hashing.h"

// this was taken from the djb2 hash function at http://www.cse.yorku.ca/~oz/hash.html
// that hash function is meant for strings, but hopefully works ok for any data

static data32 djb2Hash(byte const * data, size32 nBytes, data32 initHash)
{
    data32 hash = initHash;
    for(index32 i = 0; i < nBytes; i++)
        hash = ((hash << 5) + hash) + data[i];  // hash * 33 + c
    return hash;
}


// same, but runs through the data in reverse
static data32 djb2HashReverse(byte const * data, size32 nBytes, data32 initHash)
{
    data32 hash = initHash;
    for(index32 i = nBytes; i-- > 0; )
        hash = ((hash << 5) + hash) + data[i];	// hash * 33 + c
    return hash;
}


// For our "double hashing" method we initialize with
// 0x1505 (= 5381 decimal) in both upper and lower 32-bit words
data64 const djb2InitialHash = ((0x1505L << 32) | 0x1505);

/*
data64 DJB2DoubleHash(void const * data, size32 nBytes)
{
    data32 lowerHash = djb2Hash(data, nBytes, INIT_HASH_VALUE);
    data32 upperHash = djb2HashReverse(data, nBytes, INIT_HASH_VALUE);
	return lowerHash + (((data64) upperHash) << 32);
}
*/

data64 DJB2DoubleHashAdd(void const * data, size32 nBytes, data64 currentHash)
{
    data32 lowerHash = djb2Hash(data, nBytes, currentHash & 0xFFFFFFFF);
    data32 upperHash = djb2HashReverse(data, nBytes, currentHash >> 32);
    return lowerHash + (((data64) upperHash) << 32);
}


data64 DJB2DoubleHashAddByte(byte value, data64 currentHash)
{
    return DJB2DoubleHashAdd(&value, 1, currentHash);
}

