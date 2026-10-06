/**
 * Library of machine services for basic math functions
 */

#ifndef MATH_H
#define MATH_H


/**
 * Register the math services.
 */
void MathSetup(void);

void MathShutdown(void);


/* CLAUDE: Register the functions this module stores in persistent memory;
   see memory/references.h */
void RegisterMathFunctions(void);


#endif	// MATH_H
