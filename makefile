#
# Makefile for the atom project
#
# This makefile should support building on both Windows and Linux platforms.
# Some targets are only available on specific platforms
#
# TODO: we are currently linking all objects needed (and then some) into each
# test executable. It would be better to compile all object code into a shared
# library (.so on Linux, .dll on Windows) and then load that from test executables.
# See:  https://www.cprogramming.com/tutorial/shared-libraries-linux-gcc.html
#

# Find what system we're on:
# the OS environment variable is only defined on Windows
ifdef OS
PLATFORM := WINDOWS
else
	PLATFORM := $(shell uname)
	ifeq ($(PLATFORM), Darwin)
		PLATFORM := MACOS
	else
		PLATFORM := LINUX
	endif
endif

$(info PLATFORM = $(PLATFORM))

# source directories
SRCDIRS := $(patsubst %, src/%,\
 btree graphics kernel lang memory network parser testing tests util vm)

# source directories for FreeType
# FREETYPE_BASEDIR := third-party/$(FREETYPE)/src
# FREETYPE_SRCDIRS := $(addprefix $(FREETYPE_BASEDIR)/, base gzip psnames raster smooth sfnt truetype)

# main build directory
BUILDDIR := build

# object file directories
OBJDIR := $(BUILDDIR)/obj
OBJDIRS := $(patsubst src/%, $(OBJDIR)/%, $(SRCDIRS))

# dependency file directories
DEPDIR := $(BUILDDIR)/depend
DEPDIRS := $(patsubst src/%, $(DEPDIR)/%, $(SRCDIRS))

# binary (executable) directory
BINDIR := $(BUILDDIR)/bin

# commands and flags
# TODO: can we export the common flags to environment variables to avoid crazy long command lines ?
CFLAGS_COMMON := -std=c99 -pedantic-errors\
 -Wall -Wstrict-prototypes -Werror\
 -Wno-error=unused-variable -Wno-error=unused-function -Wno-error=unused-but-set-variable -Wpointer-arith\
 -m64 -D$(PLATFORM)

ifdef DEBUG
CFLAGS_COMMON := $(CFLAGS_COMMON) -g -DDEBUG -fprofile-arcs -ftest-coverage
endif

ifdef DEBUG_ALLOCATE
CFLAGS_COMMON := $(CFLAGS_COMMON) -DDEBUG_ALLOCATE
endif

ifeq ($(PLATFORM), MACOS)
CFLAGS := $(CFLAGS_COMMON) -ferror-limit=5 -D_POSIX_C_SOURCE=200809L
endif

ifeq ($(PLATFORM), LINUX)
CFLAGS := $(CFLAGS_COMMON) -fmax-errors=5 -D_POSIX_C_SOURCE=200809L
endif

ifeq ($(PLATFORM), WINDOWS)
CFLAGS := $(CFLAGS_COMMON) -D__USE_MINGW_ANSI_STDIO
endif


CPPFLAGS := -Wall -g -m64 -D$(PLATFORM)

# flags controlling dependency file handling by GCC
DEPFLAGS = -MMD -MP -MF $(DEPDIR)/$*.d

# include file directories
# TODO: fix this on mac os
# FREETYPE_INCLUDE_DIRS := $(shell pkg-config --cflags freetype2)
INCDIR := -I src $(FREETYPE_INCLUDE_DIRS)

# library file directories
# LIBDIR := -L third-party -L third-party/$(SDL)/x86_64-w64-mingw32/lib \
#  -L third-party/$(GLEW)/lib/Release/x64

# commonly used libraries
ifeq ($(PLATFORM), WINDOWS)
LIBS := -lmingw32 -lws2_32
else
LIBS := 
endif

# the C and C++ compiler command lines
# NOTE: on MacOS, gcc is an alias for clang. We should probably use clang explicitly.
CC := gcc $(INCDIR) $(LIBDIR) $(CFLAGS)

# main target
.PHONY: all
all : all_core tests


#
# use make make_dirs to create build directories 
# we don't include this in main target since it generates
# a lot of warning messages on windows, couldn't figure out how
# to do this quietly with CMD ...
# on windows 2> nul directs stderr to null stream, but then
# CMD does not exit ...

.PHONY: make_dirs
make_dirs :
ifeq ($(PLATFORM), WINDOWS)
# make directories, changing unix- to windows-style paths
# the leading - in -cmd causes make to ignore errors
	-cmd /C mkdir $(subst /,\,$(OBJDIRS))
	-cmd /C mkdir $(subst /,\,$(DEPDIRS))
	-cmd /C mkdir $(subst /,\,$(BINDIR))
else
	mkdir -p $(OBJDIRS) $(DEPDIRS) $(BINDIR)
endif

#
# source file listings per subdirectory
#
# TODO: this could be done better. We should be able to compile all source files
# to object files using pattern rules. When linking, we should not have to specify
# all object files explicitly, but only the direct dependencies: if for example
# foo.exe require Array.o, which reqiures Block.o, we should just add an object list for Array
# that already includes Block.o. These linking requirements usually correspond to dependencies
# but perhaps not always.
#

ifeq ($(PLATFORM), LINUX)
PLATFORM_FILE := platform_linux
else ifeq ($(PLATFORM), MACOS)
PLATFORM_FILE := platform_mac
else
PLATFORM_FILE := platform_win
endif


BTREE_FILES := btree/btree

UTIL_FILES := $(addprefix util/, \
 LinkedList ResizingArray ResizingBuffer hashing sort resources utilities)

LANG_FILES := $(addprefix lang/, \
 Atom AtomType ClauseForm Form Formula FormPermutation FullForm name PredicateForm \
 Quote SubstitutionList TermForm TypedAtom unification Variable)

KERNEL_FILES := $(addprefix kernel/, \
 dispatch ifact FloatIEEE754 Int kernel letter list lookup multiset pair \
 Parameter RelationBTree string ServiceRegistry tuples UInt)

MEMORY_FILES := $(addprefix memory/, allocator paging pool)

NETWORK_FILES := $(addprefix network/, Connection Network)

PARSER_FILES := $(addprefix parser/, Characters ClauseBuilder PartBuilder PredicateBuilder \
 StringBuffer TermBuilder Token Tokenizer)

VM_FILES := $(addprefix vm/, bytecode bytecodecontext ccontext instruction vm)

GRAPHICS_FILES := $(addprefix graphics/, \
 Graphics Mesh Point Polygon TextBlock Triangle)

TESTING_FILES := $(addprefix testing/, testing)

ALL_CORE_FILES := $(LANG_FILES) $(DATUMTYPES_FILES) $(KERNEL_FILES) $(MEMORY_FILES) $(NETWORK_FILES) \
 $(UNITY_FILES) $(PARSER_FILES) $(UTIL_FILES) $(BTREE_FILES) $(PLATFORM_FILE) $(VM_FILES) $(TESTING_FILES)

#
# core language
#
.PHONY: all_core
all_core : $(patsubst %, $(OBJDIR)/%.o, $(ALL_CORE_FILES))


#
# Graphics
#

.PHONY: graphics
graphics : $(BINDIR)/opengltest

# TODO
# SLD2_LINK_FLAGS := $(shell pkg-config --libs sdl2)
# GLEW_LINK_FLAGS := $(shell pkg-config --libs glew)
# FREETYPE_LINK_FLAGS := $(shell pkg-config --libs freetype2)

$(BINDIR)/opengltest : $(patsubst %, $(OBJDIR)/%.o, \
 $(GRAPHICS_FILES) $(MEMORY_FILES) $(PLATFORM_FILE) $(UTIL_FILES) \
 graphics/opengltest ) | $(BINDIR)
	$(CC) $^ -o $@ $(LIBS) $(SLD2_LINK_FLAGS) $(GLEW_LINK_FLAGS) $(FREETYPE_LINK_FLAGS)


#
# unit testing executables
# These cannot be included in ALL_CORE_FILES since we can only
# link one main file at a time.
#

TESTS_EXE_FILES := $(addprefix $(BINDIR)/,\
 test_btree test_datumtypes test_dispatch test_kernel test_language test_list test_lookup\
 test_memory test_multiset test_pair test_parsing test_persistence\
 test_relation_btree test_string test_table_registry test_tokenizer test_utilities test_vm)

.PHONY: tests
tests : $(TESTS_EXE_FILES)

# pattern rule to create each test_* executable file
$(BINDIR)/test_% : $(patsubst %, $(OBJDIR)/%.o, $(ALL_CORE_FILES)) \
 $(OBJDIR)/tests/test_%.o | $(BINDIR)
	$(CC) $^ -o $@
ifeq ($(PLATFORM), MACOS)
	dsymutil $@
endif

#
# pattern rules
#

$(OBJDIR)/%.o : src/%.c $(DEPDIR)/%.d
	$(CC) -c $< -o $@ $(DEPFLAGS)

# run tests, roughly in order of increasing complexity

.PHONY: test
test:
	find $(OBJDIR) -name '*.gcda' -delete
	$(BINDIR)/test_memory
	$(BINDIR)/test_utilities
	$(BINDIR)/test_datumtypes
	$(BINDIR)/test_btree
	$(BINDIR)/test_relation_btree
	$(BINDIR)/test_kernel
	$(BINDIR)/test_pair
	$(BINDIR)/test_list
	$(BINDIR)/test_multiset
	$(BINDIR)/test_string
	$(BINDIR)/test_lookup
	$(BINDIR)/test_tokenizer
	$(BINDIR)/test_language
	$(BINDIR)/test_parsing
	$(BINDIR)/test_table_registry
	$(BINDIR)/test_persistence
	$(BINDIR)/test_vm
	$(BINDIR)/test_dispatch

#
# code coverage
#
# NOTE: lcov produces warnings of the form
#  'Subroutine r... redefined at /usr/bin/geninfo line ...'
#  This appears to be a known bug, see https://bugs.launchpad.net/ubuntu/+source/lcov/+bug/2002238
#  It supposedly does not interfere with lcov function
#

LCOV_EXCLUDE_PATTERN := '*/src/btree/*' '*/src/tests/*'

.PHONY: coverage
coverage:
	gcov src/tests/*.c --object-directory $(OBJDIR)/tests -t > coverage/run_tests.gcov
	lcov --capture --directory . --output-file coverage/coverage.info > /dev/null
	lcov --remove coverage/coverage.info $(LCOV_EXCLUDE_PATTERN) --output-file coverage/coverage_filtered.info
	genhtml coverage/coverage_filtered.info --output-directory  coverage/lcov-report > /dev/null


#
# cleaning
#

.PHONY: clean
clean:
ifeq ($(PLATFORM), WINDOWS)
	-cmd /C del /Q $(subst /,\,$(BINDIR)//*.exe) >nul 2>&1
	-cmd /C del /Q /S $(subst /,\,$(DEPDIR)//*.d) >nul 2>&1
	-cmd /C del /Q /S $(subst /,\,$(OBJDIR)//*.o) >nul 2>&1
else
	rm -rf $(BINDIR)/*
	find $(OBJDIR) -name '*.o' -delete
	find $(OBJDIR) -name '*.gcda' -delete
	find $(OBJDIR) -name '*.gcno' -delete
	find $(DEPDIR) -name '*.d' -delete
endif


#
# dependencies
#

# empty rules for dependency files (generated automatically by gcc)
# this is needed so make doesn't get stuck if the .d file is absent
$(DEPDIR)/%.d : ;

# ensure make does not delete .d files
.PRECIOUS: $(DEPDIR)/%.d

# include *existing* dependency files in this make file
# this creates make rules to track dependency on headers for each object file
DEP := $(wildcard $(DEPDIR)/*/*.d)
-include $(DEP)


#
# miscellaneous
#

# for debugging, use make print-VAR to print variable VAR
print-% : ; @echo $* = $($*)
