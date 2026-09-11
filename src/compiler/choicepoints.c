#include "compiler/choicepoints.h"


void ChoiceTreeReset(ChoiceTree * choiceTree)
{
	SetMemory(choiceTree, sizeof(ChoiceTree), 0);
}


bool ChoiceTreeNextBranch(ChoiceTree * choiceTree)
{
	for(index8 i = choiceTree->depth; i > 0; i--) {
		index8 d = i - 1;
		if(choiceTree->choicePoints[d].hasNextMatch) {
			// The choices made at this choice point are kept, so that the next run takes a
			// match outside them; the choice points below it start afresh
			for(index8 j = i; j < MAX_CHOICE_POINTS; j++) {
				choiceTree->choicePoints[j].nChoices = 0;
				choiceTree->choicePoints[j].hasNextMatch = false;
			}
			choiceTree->depth = 0;
			return true;
		}
	}
	return false;
}
