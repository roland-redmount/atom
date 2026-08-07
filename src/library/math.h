/**
 * Library of machine services for basic math functions
 */

#include "kernel/operator.h"


// TODO: replace this with a service provider registry ...
extern MachineProvider mathProvider;


/**
 * Register math services
 */
void MathSetup(void);

void MathTeardown(void);
