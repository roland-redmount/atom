/**
 * An Agent is a program that can alter states. (Note the brilliant pun.)
 * An AgentHandler can provide multiple agents, and typically corresponds
 * to one storage method such as a B-tree (analogous to ServiceProvider).
 * The Bureau is a central organization for agent handlers.
 * 
 * What's life without whimsy!
 */

#ifndef BUREAU_H
#define BUREAU_H

 #include "kernel/typedtuple.h"

 /**
  * An AgentHandler is analogous to a ServiceProvider in that
  * it provides agents for specific relations.
  */
typedef struct s_AgentHandler {

	/**
	 * Callback for an agent adding a tuple (assert fact)
	 */
	void (*addTuple)(void * agentData, Tuple const * tuple);

	/**
	 * Remove one or more tuples from the underlying relation,
	 * The given tuple may contain variables to remove multiple tuples.
	 */
	void (*removeTuples)(void * agentData, Tuple const * tuple);

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


typedef struct s_Agent {
	Atom form;
	AgentHandler handler;
	void * agentData;
} Agent;


void BureauInstallAgentHandler(AgentHandler const * handler);

/**
 * Register a new agent for a given form, provided by the given agent handler.
 * This used when new relations are created that should have agents to handle
 * assert/retract.
 */
void BureauCreateAgent(Atom form, AgentHandler const * handler, void * agentData);

/**
 * Find an agent for the relation of the given form,
 */
Agent BureauFindAgent(Form form);

/**
 * Assert a fact for the agent's relation.
 */
void AgentAssertFact(Agent * agent, Tuple * tuple);

void AgentRetractFacts(Agent * agent, Tuple * tuple);


#endif	// BUREAU_H
