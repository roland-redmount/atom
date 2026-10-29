#include "kernel/float.h"


// CLAUDE: The number of decimals printed by "%f"
#define FLOAT_DECIMALS		6

// CLAUDE: Every double from 2^53 up is an integer
#define FLOAT_INTEGER_LIMIT	9007199254740992.0


/*
 * CLAUDE: Test whether a value is neither infinite nor NaN.
 */
static bool isFinite(double value)
{
	return value - value == 0.0;
}


/*
 * CLAUDE: Test whether the decimal expansion of a finite value ends within FLOAT_DECIMALS
 * digits after the decimal point. A binary fraction of k bits has exactly k decimal digits,
 * as 1/2 = 0.5, 1/4 = 0.25, and so on. So the expansion ends within FLOAT_DECIMALS digits if
 * the value times 2^FLOAT_DECIMALS is an integer. Multiplying by a power of 2 is exact.
 */
static bool hasShortDecimalExpansion(double value)
{
	ASSERT(isFinite(value))
	if((value >= FLOAT_INTEGER_LIMIT) || (value <= -FLOAT_INTEGER_LIMIT))
		return true;
	double scaled = value * (double) (1 << FLOAT_DECIMALS);
	return scaled == (double) (int64) scaled;
}


size32 FormatFloat(double value, char buffer[], size32 bufferSize)
{
	size32 length = FormatString(buffer, bufferSize, "%.*f", FLOAT_DECIMALS, value);
	if(!isFinite(value))
		return length;
	if(hasShortDecimalExpansion(value)) {
		// The decimals printed are exact, so trailing zeros are dropped, keeping one decimal
		while((buffer[length - 1] == '0') && (buffer[length - 2] != '.'))
			buffer[--length] = 0;
	}
	else {
		// The decimals printed are rounded, which is marked by ".."
		ASSERT(length + 3 <= bufferSize)
		buffer[length++] = '.';
		buffer[length++] = '.';
		buffer[length] = 0;
	}
	return length;
}


void PrintFloat(Atom _float)
{
	char buffer[FLOAT_FORMAT_BUFFER_SIZE];
	FormatFloat(_float._float, buffer, FLOAT_FORMAT_BUFFER_SIZE);
	PrintCString(buffer);
}
