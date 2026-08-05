/**
 * Path handling
 *
 * TODO: we should encapsulate the path strings to ensure
 *   they have been allocated correctly
 */


#include "platform.h"
#include "resources.h"


/**
 * Directory defaults determined when building atom. These are normally
 * supplied by the build system; the fallbacks here only serve to keep this
 * file compilable on its own.
 *
 * NOTE: these are absolute paths into the source tree, so a binary built
 * this way cannot be relocated. That is what we want for fixtures, which
 * exist nowhere but the source tree. Installed resources should instead be
 * given an installation path.
 */
#ifndef ATOM_FIXTURE_DIR
#define ATOM_FIXTURE_DIR ""
#endif

#ifndef ATOM_RESOURCE_DIR_DEFAULT
#define ATOM_RESOURCE_DIR_DEFAULT ""
#endif

/**
 * An empty data directory default means we determine it at run time,
 * from the user data directory.
 */
#ifndef ATOM_DATA_DIR_DEFAULT
#define ATOM_DATA_DIR_DEFAULT ""
#endif


#define CONFIG_DIR_NAME "atom"
#define CONFIG_FILE_NAME "atom.conf"
#define CONFIG_MAX_FILE_SIZE 4096


static struct {
	bool initialized;
	char resourceDirectory[MAX_PATH_LENGTH + 1];
	char fixtureDirectory[MAX_PATH_LENGTH + 1];
	char dataDirectory[MAX_PATH_LENGTH + 1];
} resources;


static void removeTrailingSlash(char * path)
{
	size32 length = CStringLength(path);
	if(length > 1 && path[length - 1] == '/')
		path[length - 1] = 0;
}


/**
 * Read the configuration file into the given buffer, if it exists.
 */
static bool readConfigFile(char * buffer, size32 bufferSize)
{
	char configFilePath[maxPathLength + 1];
	if(!GetUserConfigDirectory(configFilePath, maxPathLength + 1))
		return false;
	AppendPathComponent(CONFIG_DIR_NAME, configFilePath, maxPathLength + 1);
	AppendPathComponent(CONFIG_FILE_NAME, configFilePath, maxPathLength + 1);

	// having no configuration file is the normal case
	if(!FileExists(configFilePath))
		return false;

	FileHandle configFile = OpenFile(configFilePath);
	size64 configFileSize = GetFileSize(configFile);
	if(configFileSize == 0 || configFileSize >= bufferSize) {
		CloseFile(configFile);
		return false;
	}
	ReadFromFile(configFile, buffer, configFileSize);
	CloseFile(configFile);
	buffer[configFileSize] = 0;
	return true;
}


/**
 * Copy the text between start and end, excluding surrounding white space.
 */
static void copyTrimmed(char const * start, char const * end, char * buffer, size32 bufferSize)
{
	while(start < end && IsSpaceChar(*start))
		start++;
	while(end > start && IsSpaceChar(*(end - 1)))
		end--;

	size32 length = (size32) (end - start);
	if(length >= bufferSize)
		length = bufferSize - 1;
	CopyMemory(start, buffer, length);
	buffer[length] = 0;
}


/**
 * Find the value of the given key in the configuration text. Lines that are
 * empty, commented or malformed are simply skipped: the configuration file
 * is written by hand, so we do not treat it as program input.
 */
static bool findConfigValue(char const * configText, char const * key, char * buffer, size32 bufferSize)
{
	char keyBuffer[MAX_PATH_LENGTH + 1];
	char const * line = configText;
	while(*line != 0) {
		char const * lineEnd = CStringFindChar(line, '\n');
		if(lineEnd == 0)
			lineEnd = line + CStringLength(line);

		char const * separator = CStringFindChar(line, '=');
		if(separator != 0 && separator < lineEnd && *line != '#') {
			copyTrimmed(line, separator, keyBuffer, MAX_PATH_LENGTH + 1);
			if(CStringCompare(keyBuffer, key) == 0) {
				copyTrimmed(separator + 1, lineEnd, buffer, bufferSize);
				return (buffer[0] != 0);
			}
		}

		line = (*lineEnd == 0) ? lineEnd : lineEnd + 1;
	}
	return false;
}


/**
 * Resolve one directory: environment variable, then configuration file,
 * then the default determined when building. A null configKey means the
 * directory cannot be set from the configuration file.
 */
static void resolveDirectory(
	char const * variableName, char const * configKey, char const * configText,
	char const * defaultPath, char * buffer, size32 bufferSize)
{
	char const * variableValue = GetEnvironmentVariable(variableName);
	if(variableValue != 0 && variableValue[0] != 0) {
		CStringCopyLimited(variableValue, buffer, bufferSize);
		removeTrailingSlash(buffer);
		return;
	}

	if(configKey != 0 && configText != 0
			&& findConfigValue(configText, configKey, buffer, bufferSize)) {
		removeTrailingSlash(buffer);
		return;
	}

	CStringCopyLimited(defaultPath, buffer, bufferSize);
	removeTrailingSlash(buffer);
}


/**
 * Determine the data directory to use when nothing else specifies one.
 * Leaves an empty path when the environment does not say where the user's
 * data belongs. We deliberately do not fall back on the working directory:
 * writing the paging file into whatever directory we happen to be run from
 * is how a test run ends up dirtying the source tree.
 */
static void getDefaultDataDirectory(char * buffer, size32 bufferSize)
{
	if(CStringLength(ATOM_DATA_DIR_DEFAULT) > 0) {
		CStringCopyLimited(ATOM_DATA_DIR_DEFAULT, buffer, bufferSize);
		return;
	}

	if(!GetUserDataDirectory(buffer, bufferSize)) {
		buffer[0] = 0;
		return;
	}
	AppendPathComponent(CONFIG_DIR_NAME, buffer, bufferSize);
}


static void initializeResources(void)
{
	char configText[CONFIG_MAX_FILE_SIZE];
	char const * configTextOrNull =
		readConfigFile(configText, CONFIG_MAX_FILE_SIZE) ? configText : 0;

	resolveDirectory("ATOM_RESOURCE_DIR", "resource_directory", configTextOrNull,
		ATOM_RESOURCE_DIR_DEFAULT, resources.resourceDirectory, MAX_PATH_LENGTH + 1);

	// fixtures live in the source tree, so they have no place in a user configuration
	resolveDirectory("ATOM_FIXTURE_DIR", 0, configTextOrNull,
		ATOM_FIXTURE_DIR, resources.fixtureDirectory, MAX_PATH_LENGTH + 1);

	char defaultDataDirectory[maxPathLength + 1];
	getDefaultDataDirectory(defaultDataDirectory, maxPathLength + 1);
	resolveDirectory("ATOM_DATA_DIR", "data_directory", configTextOrNull,
		defaultDataDirectory, resources.dataDirectory, MAX_PATH_LENGTH + 1);

	resources.initialized = true;
}


static void ensureInitialized(void)
{
	if(!resources.initialized)
		initializeResources();
}


void GetResourceDirectory(char * buffer, size32 bufferSize)
{
	ensureInitialized();
	CStringCopyLimited(resources.resourceDirectory, buffer, bufferSize);
}


void GetFixtureDirectory(char * buffer, size32 bufferSize)
{
	ensureInitialized();
	CStringCopyLimited(resources.fixtureDirectory, buffer, bufferSize);
}


void GetDataDirectory(char * buffer, size32 bufferSize)
{
	ensureInitialized();
	CStringCopyLimited(resources.dataDirectory, buffer, bufferSize);
}


/**
 * Report a resource file we could not find, naming the ways of pointing us
 * somewhere else. A missing file is not a program error, so we do not abort.
 */
static void reportMissingFile(
	char const * fileName, char const * directory, char const * variableName, char const * configKey)
{
	PrintF("Cannot find resource file '%s' in '%s'.\n", fileName, directory);
	if(!DirectoryExists(directory))
		PrintF("That directory does not exist.\n");
	if(configKey != 0)
		PrintF("Set %s, or %s in the configuration file, to look elsewhere.\n",
			variableName, configKey);
	else
		PrintF("Set %s to look elsewhere.\n", variableName);
}


static bool getFilePath(
	char const * fileName, char const * directory, char const * variableName,
	char const * configKey, char * buffer, size32 bufferSize)
{
	FormatString(buffer, bufferSize, "%s/%s", directory, fileName);
	if(FileExists(buffer))
		return true;
	reportMissingFile(fileName, directory, variableName, configKey);
	return false;
}


bool GetResourceFilePath(char const * resourceFileName, char * buffer, size32 bufferSize)
{
	ensureInitialized();
	return getFilePath(resourceFileName, resources.resourceDirectory,
		"ATOM_RESOURCE_DIR", "resource_directory", buffer, bufferSize);
}


bool GetFixtureFilePath(char const * fixtureFileName, char * buffer, size32 bufferSize)
{
	ensureInitialized();
	return getFilePath(fixtureFileName, resources.fixtureDirectory,
		"ATOM_FIXTURE_DIR", 0, buffer, bufferSize);
}


bool GetDataFilePath(char const * dataFileName, char * buffer, size32 bufferSize)
{
	ensureInitialized();
	buffer[0] = 0;
	if(CStringLength(resources.dataDirectory) == 0) {
		PrintF("There is no data directory to hold '%s'.\n", dataFileName);
		PrintF("Set ATOM_DATA_DIR, or data_directory in the configuration file, to name one.\n");
		return false;
	}

	FormatString(buffer, bufferSize, "%s/%s", resources.dataDirectory, dataFileName);
	if(EnsureDirectory(resources.dataDirectory))
		return true;

	PrintF("Cannot create data directory '%s'.\n", resources.dataDirectory);
	PrintF("Set ATOM_DATA_DIR, or data_directory in the configuration file, to use another.\n");
	return false;
}
