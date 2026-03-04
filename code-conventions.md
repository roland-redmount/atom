# Code convention for the atom C implementation

This document outlines code conventions for the C implementation of the **atom** system. Please read this before writing code to ensure a consistent code style throughout the implenentation.


## Language

The implementation is written in C99. Hence, we use in-place variable declarations.

Macros are avoided as far as possible. Some simple numeroc constants are #defined.


## Build system

The build system is a plain makefile, intended for Linux GCC or Mingw/GCC on Windows. Both platforms use a single makefile.


## Naming

Loop counters should be named `i`, `j`, `k`, `l` in order of nesting, unless there is a better name. If you need more than four levels of nested loops you should probably refactor the function.


### Capitalization

Names should be carefully chosen to be descriptive and avoid abbreviations as far as possible (without becoming exceedingly long).

Global functions are written in CamelCase: `QueryMatch()`, `GetRoleNameLength()`. Local functions (those declared static within a source file) use a lowercase initial: `reorderArray()`, `findWordOrdering()`.

Types (defined by `typedef)` are written in CamelCase: `FormHeader`, `Atom`. Structure members, local variables and function parameters use a lowercase initial: `size`, `nRolesTotal`.


### Specific name conventions for functions

C functions should be named by verbs: `CreateThing`, `CopyStuff`, `RemoveSomething`. The exception is functions that test a property (predicates), such as `SameAtoms()`

Some verbs have a  "standardized" interpretation, as follows:

`GetSomething()`
Simple, efficient functions that simply retrieve a value, with no side-effects. It is assumed that these functions can be called frequently with little overhead. If the value must be calculated by some expensive routine, use `FindSomething()` instead. Use `GetSomething()` instead of just the noun `Something()`, e.g. `GetAtomSize()` rather than just `AtomSize()`. `GetSomething()` may return a pointer, but should not return an allocated object that the caller must assume responsibility for; see `CopySomething()`

`FindSomething()`
A procedure that computes a value, with no side-effects. Same assumptions as with `GetSomething()`, except that the computation may be expensive, and should be saved rather than re-computed when possible for efficiency.

`CopySomething()`
Create a deep-copy of some structure, and return an allocated object. The caller is responsible for deallocation (possibly with a `FreeSomething()` method). The copied structure may have a somewhat different representation than the source; see for example `CopyTRelAtom()`

`CreateSomething()`
Allocate and initialize a structure. The caller is responsible for deallocation of the resulting structure.

`FreeSomething()`
A deallocation procedure for complex malloc'ed data structures. Generally calls `free()` on all components of the structure.


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


## Use of const pointers

Generally, all functions and structures that do not modify the contents of a pointer parameter should declare it `const`, as in `void WontTouchIt(Thing const * thing)` or `struct container {Thing const * thing}`.

The `const` keyword should always go to the right side of the constant thing: use `char const *` not `const char *`.  C allows either left or right side only for the first const in a declaration. We don't follow this rule yet but we should.

In particular, deallocation functions should take `const` pointers, as in `FreeThing(const Thing* thing)`, because deallocation should not count as modification. The C stdlib `free()` function is unfortunately takes a `void*`, and may need an cast to `(const void*)` for deallocation. This is ugly, but the problem lies with the C library. See https://stackoverflow.com/questions/2819535/unable-to-free-const-pointers-in-c
We should write a wrapper around the standard library for portability.


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

We use our own allocator `Allocate` for heap allocation instead of `malloc`. It should be used sparingly; most data is stored in relations, which use special-purpose storage like `RelationBTree`, which in turn use page allocation only. 


### `puts`, `fputs`, `putc`, `fputc`

Use `fputs()` and `fputc()` for any string/character I/O that does not require formatting. Use `printf()` only when formatting is required. Do not use `puts()` and `putc()`, since (i) `fputs()` cannot be redirected to other streams that `stdout`, and (ii) `putc()` may be macro-implemented. See https://stackoverflow.com/questions/20106401/why-fputc-when-putc


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




