# atom

This is the most recent codebase for the atom project, an experimental language and computing system. The goal of atom is to explore declarative and logic programming for data science, and for computing in general.

One challenge is that atom makes extensive use of declarative logic which does not fit well with traditional, imperative programming on von Neumann architectures: logic execution is not strictly sequential, and there is no notion of linear memory. A major task for the implementation is therefore to bridge between atom and the underlying hardware. We use C as the implementation language to stay close to the machine and avoid unnecessary abstractions (read: OOP) that don't align well with atom concepts.

Atom is written as far as possible from the ground up, with minimal reliance on C standard libraries or third-party packages. Instead, we aim to implement features as far as possible within the language itself, to explore its usage for a variety of tasks. We do rely on some external function libraries while bootstrapping, and of course we depend on the host OS, but we aim to isolate all such dependencies into a platform layer. This also helps attain platform independence. Atom is currently being developed on Linux, but the goal is to make the system available on Mac and Windows platforms as well.

## Documentation

Currently maintained separately.

## Code conventions

See [this document](code-conventions.md).
