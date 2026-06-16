cmake_minimum_required(VERSION 3.20)
project(atom C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------

add_compile_options(
    -pedantic-errors
    -Wall
    -Wstrict-prototypes
    -Werror
    -fstrict-aliasing
    -Wstrict-aliasing=2
    -Wno-error=unused-variable
    -Wno-error=unused-function
    -Wno-error=unused-but-set-variable
    -Wpointer-arith
    -m64
)

if(APPLE)
    add_compile_options(-ferror-limit=5)
    add_compile_definitions(_POSIX_C_SOURCE=200809L _DARWIN_C_SOURCE)
    set(PLATFORM_SOURCE src/platform_posix.c)
elseif(WIN32)
    add_compile_definitions(__USE_MINGW_ANSI_STDIO)
    set(PLATFORM_SOURCE src/platform_win.c)
else()
    add_compile_options(-fmax-errors=5)
    add_compile_definitions(_POSIX_C_SOURCE=200809L)
    set(PLATFORM_SOURCE src/platform_posix.c)
endif()

option(DEBUG_ALLOCATE "Enable allocator debugging" OFF)
if(DEBUG_ALLOCATE)
    add_compile_definitions(DEBUG_ALLOCATE)
endif()

option(COVERAGE "Enable gcov coverage instrumentation" OFF)
if(COVERAGE)
    add_compile_options(-g -DDEBUG -fprofile-arcs -ftest-coverage)
    add_link_options(-fprofile-arcs -ftest-coverage)
endif()

# ---------------------------------------------------------------------------
# Shared library: atom
# ---------------------------------------------------------------------------

add_library(atom SHARED
    src/btree/btree.c

    src/util/LinkedList.c
    src/util/ResizingArray.c
    src/util/ResizingBuffer.c
    src/util/hashing.c
    src/util/sort.c
    src/util/resources.c
    src/util/utilities.c

    src/lang/Atom.c
    src/lang/AtomType.c
    src/lang/ClauseForm.c
    src/lang/ConjunctionForm.c
    src/lang/Form.c
    src/lang/Formula.c
    src/lang/FormPermutation.c
    src/lang/name.c
    src/lang/PredicateForm.c
    src/lang/Quote.c
    src/lang/SubstitutionList.c
    src/lang/TermForm.c
    src/lang/TypedAtom.c
    src/lang/unification.c
    src/lang/Variable.c

    src/kernel/compiler.c
    src/kernel/dictionary.c
    src/kernel/dispatch.c
    src/kernel/ifact.c
    src/kernel/float.c
    src/kernel/Int.c
    src/kernel/kernel.c
    src/kernel/letter.c
    src/kernel/list.c
    src/kernel/lookup.c
    src/kernel/multiset.c
    src/kernel/pair.c
    src/kernel/Parameter.c
    src/kernel/RelationBTree.c
    src/kernel/service.c
    src/kernel/ServiceRegistry.c
    src/kernel/string.c
    src/kernel/tuple.c
    src/kernel/UInt.c

    src/library/math.c

    src/memory/allocator.c
    src/memory/paging.c
    src/memory/pool.c

    src/network/Connection.c
    src/network/Network.c

    src/parser/Characters.c
    src/parser/ClauseBuilder.c
    src/parser/ConjunctionBuilder.c
    src/parser/PartBuilder.c
    src/parser/PredicateBuilder.c
    src/parser/StringBuffer.c
    src/parser/TermBuilder.c
    src/parser/Token.c
    src/parser/Tokenizer.c

    src/testing/testing.c

    ${PLATFORM_SOURCE}
)

target_include_directories(atom PUBLIC src)
target_compile_definitions(atom PUBLIC $<$<CONFIG:Debug>:DEBUG>)

if(WIN32)
    target_link_libraries(atom PRIVATE mingw32 ws2_32)
endif()

# ---------------------------------------------------------------------------
# Test executables
# ---------------------------------------------------------------------------

enable_testing()

set(ATOM_TESTS
    test_atomtypes
    test_btree
    test_compiler
    test_dictionary
    test_dispatch
    test_kernel
    test_language
    test_list
    test_lookup
    test_math
    test_memory
    test_multiset
    test_pair
    test_parsing
    test_persistence
    test_relation_btree
    test_service
    test_service_registry
    test_string
    test_tokenizer
    test_tuple
    test_unification
    test_utilities
)

foreach(T ${ATOM_TESTS})
    add_executable(${T} src/tests/${T}.c)
    target_link_libraries(${T} PRIVATE atom)
    add_test(NAME ${T} COMMAND ${T})
endforeach()

# ---------------------------------------------------------------------------
# Optional graphics target
# ---------------------------------------------------------------------------

option(BUILD_GRAPHICS "Build the OpenGL graphics target" OFF)
if(BUILD_GRAPHICS)
    add_executable(opengltest
        src/graphics/opengltest.c
        src/graphics/Graphics.c
        src/graphics/Mesh.c
        src/graphics/Point.c
        src/graphics/Polygon.c
        src/graphics/TextBlock.c
        src/graphics/Triangle.c
    )
    target_link_libraries(opengltest PRIVATE atom)
endif()
