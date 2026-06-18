/**
 * An Agent is a program that can alter states.
 * An AgentHandler can provide multiple agents, and typically corresponds
 * to one storage method such as a B-tree (analogous to ServiceProvider).
 * The Bureau is a central organization for agent handlers.
 * 
 * What's life without whimsy!
 */

#ifndef BUREAU_H
#define BUREAU_H

 #include "kernel/tuple.h"

 /**
  * An AgentHandler is analogous to a ServiceProvider in that
  * it provides agents for specific relations.
  */
typedef struct s_AgentHandler {

	/**
	 * Callback for an agent adding a tuple (assert fact)
	 * The tuple size and atom types are fixed for each agent,
	 * so providing an Atom array is sufficient.
	 */
	void (*addTuple)(void * agentData, Atom const * tuple, uint8 idPosition);

	/**
	 * Remove one or more tuples from the underlying relation,
	 * The given tuple may contain variables to remove multiple tuples.
	 */
	void (*removeTuples)(void * agentData, Atom const * tuple);

	/**
	 * Check if the underlying relation is empty
	 */
	bool (*isEmpty)(void * agentData);

	/**
	 * Remove an agent. Typically deallocates the underlying data structure,
	 * such as a B-tree
	 */
	void (*removeAgent)(void * agentData);

} AgentHandler;


/**
 * An agent is identified by a (form, atom types) pair and is therefore 1:1
 * with a relation table implementation, such as RelationBTree.
 */
typedef struct s_Agent {
	Atom form;
	AgentHandler handler;
	void * agentData;
} Agent;


void BureauInstallAgentHandler(AgentHandler const * handler);

/**
 * Register a new agent for a given form, provided by the given agent handler.
 * This used when new relations are created that should have agents to handle
 * assert/retract. The agentData pointer should contain the information needed
 * to locate the underlying relation table.
 */
void BureauCreateAgent(Atom form, AgentHandler const * handler, void * agentData);

/**
 * Find an agent for the relation of the given (form, types)
 */
Agent const * BureauFindAgent(Atom form, byte const * atomTypes);

/**
 * Assert a fact for the agent's relation.
 */
void AgentAssertFact(Agent const * agent, Atom const * actors, uint8 idPosition);

void AgentRetractFacts(Agent const * agent, Atom const * tuple);


#endif	// BUREAU_H
