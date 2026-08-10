
# The Atom computing system. 

This is the source repository for the Atom computing system. For a brief description of the project, see README.md. Atom is still under construction, so beware that certain components may be missing, parts of the source may be outdated and concepts may still be changing. There is also some dead code.

## Project structure

The Atom system is written in C99. All sources are under src/, divided into subdirectories roughly by subject area. This is not a strict subdivision, but helps keeping the source folder managable.

## Code conventions

We have a few code conventions, some of which are a bit non-standard: see code-conventions.md. 

## Guidelines for writing documentation

Documentation of code in comments is important, particularly for highly technical parts of the codebase, such as the compiler. The more conceptually difficult code is, the more thorough and explicit the documentation has to be. Some guidelines for writing readable comments:
* Be specific when referring to code elements: it is best to name a structure or function explicitly. Don't refer to code elements by location ("the function above" or "the struct below"), since text blocks might be moved in the future.
* Prefer nouns to noun references ("it", "those", "them", "that", "which"). When noun references are used, make sure that they are not ambiguous. Consider the sentence _"A projection keeping every argument drops nothing and materializes its child, which is what sorts it."_ (This sentence was previously found in a doc string in `compiler.c`.) Here the first noun reference _"its"_ is fine, as it is clear that it refers to the noun _"projection"_, but the last noun reference _"it"_ is ambiguous: it could refer to either _"projection"_ or to "_its child_". Also, its unclear what _"which"_ refers to: the fact that a projection _"drops nothing"_, or that it _"materializes its child"_, or perhaps both. The noun _"projection"_ is also a vague reference to the `PROJECT` operator in `operator.c`. A better sentence for this doc string is _"The PROJECT operator with no arguments removed will sort the tuples of its child relation."_ 
* Break long sentences with period. It is easier to comprehend several clear, short sentences than one long meandering sentence with complex punctuation. Long sentences risk ending up with multiple noun references which can be confusing.
* Prefer describing objects in singular rather than plural, since reasoning about multiple objects is more difficult and can lead to confusion. For example, in the sentence _"Recursive clauses are only compiled in the second pass, once there are services for their recursive terms to dispatch to"_, it is not clear whether each clause has exactly one service and one term, or whether clauses, services and terms are related in more complex ways. Using the singular form makes the 1:1:1 correspondence clear : _"A recursive clause is only compiled in the second pass, once there is a service for its recursive term to dispatch to"_.

## Git usage

Always ask before executing `git commit`. Do not run `git push`, `git pull` or other commands affecting the upstream; the user will handle pushing manually.

