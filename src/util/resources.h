#ifndef RESOURCES_H
#define RESOURCES_H

#include "platform.h"


/**
 * Locating resource files.
 *
 * We distinguish three directories, as they have different contents and
 * access rights:
 *
 *   resources  read-only application assets, such as shader sources
 *   fixtures   read-only files used by the test suite, kept in the source tree
 *   data       writable, persistent application data, such as the paging file
 *
 * Each is resolved once, upon first use, in the following order:
 *
 *   1. the corresponding environment variable, if set and not empty:
 *      ATOM_RESOURCE_DIR, ATOM_FIXTURE_DIR or ATOM_DATA_DIR
 *   2. the corresponding key in the configuration file (see below):
 *      resource_directory or data_directory
 *   3. a default determined when building atom
 *
 * The configuration file is atom.conf in the atom folder of the user
 * configuration directory, so on Linux ~/.config/atom/atom.conf. It holds
 * lines of the form
 *
 *   # a comment
 *   resource_directory = /usr/local/share/atom/resources
 *
 * Having no configuration file is the normal case, not an error.
 *
 * NOTE: resolved directories are cached, which assumes a single thread.
 */

/**
 * Get a resolved directory, without trailing separator.
 */
void GetResourceDirectory(char * buffer, size32 bufferSize);
void GetFixtureDirectory(char * buffer, size32 bufferSize);
void GetDataDirectory(char * buffer, size32 bufferSize);

/**
 * Compose the path of a named file in the resource or fixture directory.
 * The buffer always receives the composed path, but if no such file exists
 * these report where they looked and return false.
 */
bool GetResourceFilePath(char const * resourceFileName, char * buffer, size32 bufferSize);
bool GetFixtureFilePath(char const * fixtureFileName, char * buffer, size32 bufferSize);

/**
 * Compose the path of a named file in the data directory, creating that
 * directory if it does not exist. The file itself need not exist: creating
 * it is the caller's business. Returns false if the directory is missing
 * and cannot be created.
 */
bool GetDataFilePath(char const * dataFileName, char * buffer, size32 bufferSize);


#endif	// RESOURCES_H
