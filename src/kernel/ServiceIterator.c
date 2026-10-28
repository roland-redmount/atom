
#include "kernel/ServiceIterator.h"


void ServiceIterate(ServiceRecord const * serviceRecord, Atom const values[], ServiceIterator * iterator)
{
	iterator->serviceRecord = serviceRecord;
	Service service = serviceRecord->service;
	size8 arity = FormArity(service.relation.form);

	// The service's operator takes one argument per distinct parameter; a repeated query
	// variable is copied once per column, to the same argument
	EqualitySignatureGetArgumentMap(service.equalitySignature, arity, iterator->argumentMap);
	for(index8 i = 0; i < arity; i++)
		iterator->arguments[iterator->argumentMap[i]] = values[i];

	iterator->nGroups = ServiceGetExchangeableGroups(
		service, iterator->groupArguments, iterator->groupSizes);
	iterator->context = OperatorCreateContext(serviceRecord->op, iterator->arguments);
}


/**
 * CLAUDE: Test whether the values of each exchangeable group ascend in the current
 * arguments of the iterator.
 */
static bool isCanonicalTuple(ServiceIterator const * iterator)
{
	index8 const * groupArguments = iterator->groupArguments;
	for(index8 g = 0; g < iterator->nGroups; g++) {
		size8 groupSize = iterator->groupSizes[g];
		for(index8 i = 1; i < groupSize; i++) {
			if(CompareAtoms(
				iterator->arguments[groupArguments[i - 1]],
				iterator->arguments[groupArguments[i]]) > 0)
				return false;
		}
		groupArguments += groupSize;
	}
	return true;
}


bool ServiceIteratorNext(ServiceIterator * iterator)
{
	while(OperatorCall(iterator->context)) {
		if(isCanonicalTuple(iterator))
			return true;
	}
	return false;
}


TypedAtom ServiceIteratorGetElement(ServiceIterator const * iterator, index8 column)
{
	TypeSignature typeSignature = iterator->serviceRecord->service.relation.typeSignature;
	return CreateTypedAtom(
		typeSignature.atomTypes[column],
		iterator->arguments[iterator->argumentMap[column]]);
}


void ServiceIteratorEnd(ServiceIterator * iterator)
{
	OperatorFreeContext(iterator->context);
	iterator->context = 0;
}
