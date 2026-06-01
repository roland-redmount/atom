/**
 * Library of machine services for basic math functions
 */

#include "kernel/machineservice.h"

// TODO: replace this with a service provider registry ...
extern MachineServiceProvider mathServiceProvider;


/**
 * Register math services
 */
void MathSetup(void);

void MathTeardown(void);
