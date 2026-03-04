# atom

This is the most recent codebase for the atom project, an experimental language and computing system. The goal of atom is to explore declarative and logic programming for data science, and for computing in general.

One challenge is that atom makes extensive use of declarative logic which does not fit well with traditional, imperative programming on von Neumann architectures: logic execution is not strictly sequential, and there is no notion of linear memory. A major task for the implementation is therefore to bridge between atom and the underlying hardware. We use C as the implementation language to stay close to the machine and avoid unnecessary abstractions (read: OOP) that don't align well with atom concepts.

Atom is written as far as possible from the ground up, with minimal reliance on C standard libraries or third-party packages. Instead, we aim to implement features as far as possible within the language itself, to explore its usage for a variety of tasks. We do rely on some external function libraries while bootstrapping, and of course we depend on the host OS, but we aim to isolate all such dependencies into a platform layer. This also helps attain platform independence. Atom is currently being developed on Linux, but the goal is to make the system available on Mac and Windows platforms as well.

## IMPORTANT

This repo should not be made public, as `docs` contain all sorts of internal business documents. Copy the code into a new public repo to remove the commit history.

## Documentation

See under `docs`.

## Notes on implementation

This version focuses on relations as the main mechanism for implementing the system, and does away with "large datums" (is the plan). Aside from a few "primitive" atom types like integers, all data is stored in relations. Most atoms will be defined by "identifying facts": for example, the atom ID for a string `cat` is a hash of the fact(s)
```
list cat length 3 &
list cat position 1 element @c
list cat position 2 element @a
list cat position 3 element @4
```
This will be super inefficient in a first implementation, but the philosophy is to postpone all optimization until we have a working system. Eventually we can make special-purpose optimized tables that reduce the above to essentially the same as the C string. The advantage is that the system is conceptually as simple as possible.

## Implementation plan

* DONE Relation tables.
* DONE Registry for relation tables.
* DONE IFact table.
* DONE Convert large datumtypes `Array`, `Quote`, `String`, `Tuple`, `Variable` to IFacts and remove the `Block` ADT. Reference counting now handled by IFact.
* Lookup. This will require a table listing for each atom the relations it is part of, and the role (marginal fact), e.g. `list cat position _ element _`.
* Virtual machine and bytecode.
* Dispatcher
* Logic rules
* Compiler
* Graphics

## Code conventions

See [this document](code-conventions.md).
