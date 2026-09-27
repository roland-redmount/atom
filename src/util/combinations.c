
#include "util/combinations.h"


void FirstCombination(size8 k, index8 indices[])
{
	for(index8 i = 0; i < k; i++)
		indices[i] = i;
}


bool NextCombination(size8 n, size8 k, index8 indices[])
{
	ASSERT(k <= n)
	// Find the last index that can be increased: index i can take at most the value n - k + i
	for(index8 i = k; i > 0; i--) {
		index8 j = i - 1;
		if(indices[j] < n - k + j) {
			indices[j]++;
			// The indices after it follow on consecutively
			for(index8 l = j + 1; l < k; l++)
				indices[l] = indices[l - 1] + 1;
			return true;
		}
	}
	return false;
}
