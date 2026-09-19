
#include "storage/StorageProvider.h"


/**
 * The default storage provider. This allocates no storage
 * and generates no readers.
 */
static void * defaultCreateImpl(size8 nColumns, size32 * nReaders)
{
	*nReaders = 0;
	return 0;
}

static void defaultFree(void * storage)
{
}


StorageProvider defaultProvider = {
	.setupStorage = defaultCreateImpl,
	.free = defaultFree,
};
