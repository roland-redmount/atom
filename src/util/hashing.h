
#ifndef HASHING_H
#define HASHING_H

#include "platform.h"

/**
 * Use this value for initializing hashing
 */
extern data64 const djb2InitialHash;

/**
 * Compute hash of a sequence of bytes
 */
// data64 DJB2DoubleHash(void const * data, size32 nBytes);

/**
 * Compute hash of a sequence of bytes, initializing
 * with the given hash value. This allows continuing hash computations
 * from a previous call, in effect "adding" more data to the hash.
 * To being hashing from scratch, set currentHash = djb2InitialHash
 */
data64 DJB2DoubleHashAdd(void const * data, size32 nBytes, data64 currentHash);

/**
 * Continue hash computation from a given hash value,
 * "adding" a single byte
 */
data64 DJB2DoubleHashAddByte(byte value, data64 currentHash);


#endif  // HASHING_H
