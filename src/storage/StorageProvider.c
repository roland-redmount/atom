
#include "storage/StorageProvider.h"


/**
 * The default storage provider
 */
static void * defaultCreateImpl(size8 nColumns, size32 * nReaders)
{
	return 0;
}

static void defaultFree(void * storage)
{
}

StorageProvider defaultProvider = {
	.setupStorage = defaultCreateImpl,
	.free = defaultFree,
};
