# atom

This is the most recent codebase for the **atom** project, an experimental language and computing system.

## What is it

**atom** is a declarative logic language for data science. It is based on a relational data model where all information is stored in relations between atoms, which have no internal structure -- sort of the opposite of object-oriented languages. You might think of it as a [relational algebra](https://en.wikipedia.org/wiki/Relational_algebra) where tables can be computed on-the-fly using logic reasoning methods. The closest living relative is probably [datalog](https://en.wikipedia.org/wiki/Datalog).

Features:
* Simple declarative language with minimal syntax
* Open-world model using three-state logic to represent unknowns
* Type safety and correctness via logic rules -- no dedicated type system
* Automatic persistence to disk -- no load/save operations needed

A brief, interactive tutorial on the **atom** language concepts is here [TODO].


## Platforms and dependencies

**atom** is currently being developed on Linux, and should work on MacOS as well. There are currently no dependencies except the C standard library. Windows builds are currently not supported, but should be pretty straightforward to implement.


## Building

Clone the repo and build with CMake:
```
cmake -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
```

This builds a shared library in `build/debug/lib`  and the REPL (command line client) and test executables in `build/debug/bin`. For a release build, use `-DCMAKE_BUILD_TYPE=Release` instead. VSCode task definitions are also provided for the different build types.



### Web application

**atom** can be compiled to WASM for web applications; see `src/ui/web.c`. This requires the [Emscripten SDK](https://github.com/emscripten-core/emsdk). Assuming `emcmake` on the path, do:
```
emcmake cmake -B build/wasm -DCMAKE_BUILD_TYPE=Debug
cmake --build build/wasm
```
This build optimizes for size. The `build/wasm/web` folder contains the resulting WASM executable and a test page. To add the web app to a site, see `web/publish.sh`.


## Implementation

The **atom** implementation consists of:
* Storage providers that provide persistent tuple storage
* Primitive services, which provide fundamental operations such as arithmetic.
* A parser for the atom syntax, which generates "code" (formulas) represented as atoms (reflection)
* A compiler, which evalates queries against known logic rules and generates programs in an intermediate 
representation, which is executed by an interpreter. The intermediate representation could be further compiled to bytecode / machine code.

The implementation language in C99. We try to minimize dependencies on third-party packages. All dependencies on the C standard library or OS services should be wrapped in a platform layer (`src/platform.h`) to promote platform independence.

### Code conventions

See [this document](code-conventions.md).
