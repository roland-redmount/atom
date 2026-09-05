# Code convention for the atom C implementation

This document outlines code conventions for the C implementation of the **atom** system. Please read this before writing code to ensure a consistent code style throughout the implenentation.


## Language

The implementation is written in C99. Hence, we use in-place variable declarations.

Macros are avoided as far as possible. Some simple numeroc constants are #defined.


## Build system

The build system is a plain makefile, intended for Linux GCC or Mingw/GCC on Windows. Both platforms use a single makefile.


## Naming

Naming things is the hardest thing in computer science.

### Loop counters

Loop counters should be named `i`, `j`, `k`, `l` in order of nesting, unless there is a better name. If you need more than four levels of nested loops you should probably refactor the function.


### Capitalization

Names should be carefully chosen to be descriptive and avoid abbreviations as far as possible (without becoming exceedingly long).

Global functions are written in CamelCase: `QueryMatch()`, `GetRoleNameLength()`. Local functions (those declared static within a source file) use a lowercase initial: `reorderArray()`, `findWordOrdering()`.

Types (defined by `typedef)` are written in CamelCase: `FormHeader`, `Atom`. Structure members, local variables and function parameters use a lowercase initial: `size`, `nRolesTotal`.


### Specific name conventions for functions

Function names should have the "module" name as prefix, to compensate for lack of namespaces in C: `RegistryRemoveService`, `TupleNAtoms`, &c.

C functions should be named by verbs: `CreateThing`, `CopyThing`, `RemoveThing`. The exception is functions that test a property (predicates), such as `SameAtoms()`

Some verbs have a  "standardized" interpretation, as follows:

`GetThing()`
Simple, efficient functions that simply retrieve a value, with no side-effects. It is assumed that these functions can be called frequently with little overhead. If the value must be calculated by some expensive routine, use `FindSomething()` instead. Use `GetSomething()` instead of just the noun `Something()`, e.g. `GetAtomSize()` rather than just `AtomSize()`. `GetSomething()` may return a pointer, but should not return an allocated object that the caller must assume responsibility for; see `CopySomething()`

`FindSomething()`
A procedure that computes a value, with no side-effects. Same assumptions as with `GetSomething()`, except that the computation may be expensive, and should be saved rather than re-computed when possible for efficiency.

`CopySomething()`
Create a deep-copy of some structure, and return an allocated object. The caller is responsible for deallocation (possibly with a `FreeSomething()` method). The copied structure may have a somewhat different representation than the source; see for example `CopyTRelAtom()`

`CreateSomething()`
Allocate and initialize a structure. The caller is responsible for deallocation of the resulting structure.

`FreeSomething()`
A deallocation procedure for complex malloc'ed data structures. Generally calls `free()` on all components of the structure.

### Use as general names as possible

Functions should be given a name that describes what it does in full generality, rather than some special case application of it. When factoring out a helper function from a larger function, it may be tempting to name the helper according to its role in the larger function, but this is misleading as it adds connotations that the helper function really does not have. For example, when factoring out a helper function from the `FIXPOINT` operator that handled merging a set of pending operator calls into another list, the helper was named
```
static size32 mergePendingCalls(BTree * pendingCalls, BTree * calls)
```
However, this function knows nothing about operator calls, but simply adds all items from the
`pendingCalls` B-tree to the `calls` B-tree. A better function signature is
```
static size32 mergeBTrees(BTree * source, BTree * destination)
```
which makes clear that this function is purely about B-tree manipulation (and might perhaps belong in `btree.c`).


## Type names

For clarity, we use a set of custom type names defined in `platform.h`. They largely mirror the standard C types, minus the `_t` suffix, so for example `int8_t` becomes just `int8`. For data types that do not necessarily encode integers, we use the aliases `data8`, `data16`, `data32`, `data64` to indicate word size, with `byte` as an additional alias for `data8`.

For indexing we use unsigned integers with aliases `index8`, `index16`, `index32`; these are probably the most commonly used integer types, as indexing is so common. For data sizes we use `size8` through `size64`, also unsigned. There are [potential issues](https://c3-lang.org/blog/unsigned-sizes-a-five-year-mistake/) with unsigned types related to type conversion and overflow that we should watch out for, but I haven't has trouble so far.


## Use of boolean flags

Avoid using `bool` flags in function signatures as behavior switches, such as
```
int add2(int x, bool negate)
{
	return 2 + (negate ? -x : x);
}
```
This makes the function call `add2(42, true)` difficult to understand without looking up the function definition. Instead, use an integer flag with `#define` values instead:
```
#define NEGATED			1
#define NOT_NEGATED		2

int add2(int x, int negate)
{
	return 2 + ((negate == NEGATE) ? -x : x);
}
```
which allow for the more readable `add2(42, NEGATE)`. An `enum` type can also be used.


## Tests for null values

While tests like `if(x == 0)` and `if(!x)` are equivalent, they have different connotations. The form `x == 0` should
be used whenever `x` represents a quantity, as in `if(numberOfApples == 0) {...}`. The form `!x` should be used
when 0 indicates a null / absent value: this applies if $x$ is a pointer, but also if `x` is an integer type where the
0 value indicates absent, such as with a 1-based index where 0 means "no index".


## Error handling

Functions generally assume that they are provided valid arguments -- that data structures are properly filled in, pointers are not null, array indexes are within range, &c. We use the `ASSERT()` macro to verify such assumptions in debug builds; violation of the condition of an `ASSERT()` is considered programmer error, and should never occur. Functions do not have to test for programmer errors, return error codes, and callers don't have to test for error codes. It is assumed that functions work correctly when given correct arguments, and it is the callers responsibility to provide correct arguments. In release builds, a violated `ASSERT()` will lead to undefined behavior.

There are of course situations where a function will not be able to do what the caller requested due to bad input, for example `AssertFact(fact)` will abort and return `false` when `fact` contradicts an existing fact. This is not an error: it is a normal outcome, since the caller cannot know whether the fact is valid before attempting to assert it, and `AssertFact` is the function that decides whether the fact is valid or not. User input may be invalid in various ways, but rejecting such input is normal behavior, not a program error.

The third category of errors are irrecoverable failures, such as the OS failing to provide resources that are absolutely necessary (like memory pages). These are not programmer errors, and should not be handled with the `Panic()` function, not `ASSERT`.


## Memory allocation conventions

As far as possible, we avoid heap allocation in functions that initialize data structures. Instead, the caller passes a pointer to a pre-allocated structure, which is usually stack-allocated as the structure lifetime is typically the same as the caller scope. So instead of
```
Thing * CreateThing(...)

{
	// caller scope
	Thing * myThing = CreateThing(...);

	// do stuff with the thing

	FreeThing(myThing);
}
```
we have
```
void InitializeThing(Thing * thing, ...)

{
	// caller scope

	Thing myThing;
	InitializeThing(myThing, ...);

	// do stuff with the thing

	// deallocation is automatic on scope exit
}
```

Heap allocation with `Allocate()` always returns cleared (zeroed) memory; so do `PoolAllocate()` and `AllocatePage()`. Therefore, zero initialization is redundant and should be avoided. It it recommended to design data structures so that zero values is a valid default, as far as possible; then, simply allocating a structure with `Thing * thing = Allocate(sizeof(Thing))` gives a valid data structure.

## Use of const

Generally, all functions and structures that do not modify the contents of a pointer parameter should declare it `const`, as in `void WontTouchIt(Thing const * thing)` or `struct container {Thing const * thing}`. Functions that deallocate their argument are a special case: they should be const as well, as they do not _modify_ the data. For example, `void Free(void const * memory)`.

The `const` keyword should always go to the right side of the constant thing: use `char const *` not `const char *`.  C allows const on either left or right side, but only for the first const in a declaration, so this style (known as "East const") is more consistent and easier to read.

Do not cast away const, as in `Thing const * thing1; thing 2 = (Thing *) thing1`. This may lead to undefined behavior.


## Passing arrays to function

For functions that take array arguments, prefer the array syntax `void f(data64 array[], size32 length)` syntax over the bare pointer version `void f(data64 * array, size32 length)`. This improves readability.

## 1-based positions vs. 0-based indexing

C code naturally uses 0-based indexing into arrays (offsets). We use the data types `index8` through `index64` to indicate 0-based indices. Some data structure in this codebase uses 1-based indexing to match the atom language conventions. To help distinguish between the two, we refer to a 1-based index as "position" and use the `uint` data types for these values. 1-based positions have the advantage that a 0 value can be used as the "null" value (position not specified).


## Array intervals

For indexes into arrays/strings that denote intervals, we use the convention of half-open intervals [a, b) as in Python, so that the interval ranges from x[a] to x[b-1]. This has several advantages, including that the interval length equals b - a.

## Macros

Avoid using macros as far as humanly possible. Macro calls are written without trailing semicolon, as they are not themselves C statements.
```
MACRO(argument);    // WRONG
MACRO(argument)     // RIGHT
```

## OS and C library dependencies

Standard C library functions like `printf` should not be used directly, but encapsulated with wrappers in the platform layer (`platform.h`), so that these functions can be replaced in the future. This also goes for all OS depenencies. Essentially, the only code that should have to be rewritten to port the software to another OS / platform is the implementation on `platform.h`. For linux this is `platform_linux.c`, for Windows `platform_windows.c`, &c.

### `malloc` and `free`

We use our own allocator functions `Allocate` and `Free` for heap allocation instead of `malloc` and `free`. Heap allocation should be used sparingly; most data is stored in relations, which use special-purpose storage like `RelationBTree`, which in turn use page allocation only. For arrays constant-size structures, `PoolAllocate()` is preferable to `Allocate()`. 


## Documentation

Put function documentation in header files, not in `.c` implementation files. Use C multiline comments.
```
/**
 * This is how my function works.
 */
void MyFunction(...);
```

Do not write superfluous comments:
```
int beanCounter;		// a bean counter
```

I is better to use (longer) descriptive names than to add comments to demystify poor naming: instead of
```
int t;		// the current time
```
just write
```
int currentTime;
```
Cryptic computations can often be pulled out to a function so that they can be given a descriptive name.

See also `CLAUDE.md` for guidance on language in documentation strings.




