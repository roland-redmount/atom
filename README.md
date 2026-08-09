# atom

This is the most recent codebase for the **atom** project, an experimental language and computing system.

## What is it

**atom** is a declarative logic language for data science. It is based on a relational data model where all information is stored in relations between atoms, which have no internal structure -- sort of the opposite of object-oriented languages. You might think of it as a [relational algebra](https://en.wikipedia.org/wiki/Relational_algebra) where tables can be computed on-the-fly using logic reasoning methods. The closest living relative is probably [datalog](https://en.wikipedia.org/wiki/Datalog).

Features:
* Simple declarative language with minimal syntax
* Open-world model using three-state logic to represent unknowns
* Type safety and correctness via logic rules -- no dedicated type system
* Automatic persistence to disk -- no load/save operations needed

A primer on the atom language concepts is here [TODO].

## Implementation

_The implementation is incomplete and many features described here are not yet in place._

One challenge with implementing atom is that declarative logic does not map naturally onto traditional, imperative programming: logic execution is not strictly sequential, and there is no notion of linear memory. The current implementation compiles atom's logic rules into an intermediate representation which is executed by an interpreter.

We use C99 as the implementation language to stay close to the machine and avoid unnecessary abstractions (read: OOP) that don't align well with atom concepts. We try to minimize dependencies on C standard libraries or third-party packages to stay self-contained. All dependencies should be wrapped in a platform layer to promote platform independence. Atom is currently being developed on Linux.

## Documentation

Currently maintained separately. The compiler, which turns a query into an executable operator tree, is documented in [this document](compiler.md).

## Code conventions

See [this document](code-conventions.md).
