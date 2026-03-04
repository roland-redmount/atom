#include "util/utilities.h"


uint32 DivCeiling(uint32 numerator, uint32 divisor)
{
    return (numerator / divisor) + (numerator % divisor == 0 ? 0 : 1);
}


void SwapBytes(byte* a, byte* b)
{
    byte tmp = *a;
    *a = *b;
    *b = tmp;
}


void PrintIndexArray(const index32* array, size32 n)
{
    for(index32 i = 0; i < n-1; i++) {
        PrintF("%u ", array[i]);
    } 
    PrintF("%u", array[n-1]);
}

void PrintIndex8Array(const index8* array, size8 n)
{
    for(index8 i = 0; i < n-1; i++) {
        PrintF("%u ", array[i]);
    } 
    PrintF("%u", array[n-1]);
}



/**
 * Sleep for a given number of milliseconds, based on clock()
 *
 * TODO: this is a busy-wait loop, should be improved!
 *   We should use OS-provided functions, Sleep() on Windows
 *   and sleep() on posix systems,
 *   https://pubs.opengroup.org/onlinepubs/007908799/xsh/sleep.html
 *   This is an OS functionality wrapper, should be defined in a 
 *   wrapper module os.c or similar
 */
/*
void Sleep(uint32 milliSeconds)
{
    clock_t endTime = clock() + milliSeconds * (CLOCKS_PER_SEC / 1000);
    while(clock() < endTime)
        ;
}
*/


/**
 * Generic ordering function for a sequence of n consecutive, variable-length "words",
 * whose locations are defined by the position and order arrays, so that
 * word i ranges from data[position[order[i]] to data[position[order[i]+1]
 * and position[n] = length of data. For example, with
 * data "OneTwoThree", position = {0,3,6,11}, order = {1,2,0}, the implicit
 * ordered sequence is "TwoThreeOne"
 * The compare function is used to determine ordering between any two words
 */ 

/*
static index8* findWordOrdering(
    byte const * data, index8 const * position, size_t nWords,
    int (*compare)(void const *, void const *))
{
    // initalize ordering to 1 ... n
    index8* order = malloc(nWords); 
    for(index8 i = 0; i < nWords; i++) 
        order[i] = i;
    //printf("findWordOrdering() nWords = %llu\n", nWords);
    // do a simple bubble sort for now ...  :-/
    bool sorted = false;
    while(!sorted) {
        sorted = true;
        for(index8 i = 1; i < nWords; i++) {
            // compare two words now adjacent in the *reordered* string
            //printf("pos1 = %u pos2 "= %u\n, )
            const byte* word1 = data + position[order[i-1]];
            const byte* word2 = data + position[order[i]];
            int sign = compare(word1, word2);
            if(sign > 0) {
                // word2 should be before word1
                //printf("swap %u and %u\n", order[i-1], order[i]);
                // swap orders
                index_t tmp = order[i-1];
                order[i-1] = order[i];
                order[i] = tmp;
                // set flag
                sorted = false;
            }
        }
    }
    return order;
}
*/

