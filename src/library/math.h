/**
 * Library of machine services for basic math functions
 */

#ifndef MATH_H
#define MATH_H


/**
 * Register the math services.
 */
void MathSetup(void);

/**
 * Restore the math services from the paging area upon restart.
 */
void MathRestore(void);

void MathShutdown(void);

/**
 * Register functions referred to from persistent memory.
 */
void RegisterMathFunctions(void);


#endif	// MATH_H
