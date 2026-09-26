#include "compiler/choicepoints.h"


void ChoiceTreeReset(ChoiceTree * choiceTree)
{
	SetMemory(choiceTree, sizeof(ChoiceTree), 0);
	choiceTree->branchIndex = NO_BRANCH;
}


bool ChoiceTreeNextBranch(ChoiceTree * choiceTree)
{
	for(index8 i = choiceTree->nChoicePoints; i > 0; i--) {
		index8 d = i - 1;
		if(choiceTree->choicePoints[d].hasNextMatch) {
			// The choices made at this choice point are kept, so that the next run takes a
			// match outside them; the choice points below it start afresh
			choiceTree->branchIndex = d;
			return true;
		}
	}
	return false;
}
