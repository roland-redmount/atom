
# The Atom computing system. 

This is the source repository for the Atom computing system. For a brief description of the project, see README.md. Atom is still under construction, so beware that certain components may be missing, parts of the source may be outdated and concepts may still be changing. There is also some dead code.

## Project structure

The Atom system is written in C99. All sources are under src/, divided into subdirectories roughly by subject area. This is not a strict subdivision, but helps keeping the source folder managable.

## Code conventions

We have a few code conventions, some of which are a bit non-standard: see code-conventions.md. 

## Writing comments

Be restrictive with writing comments: too many or too long comments rarely get read, and so serve no purpose.

DO NOT EVER over-write existing comments in the codebase -- they may have been carefully and painstakingly crafted by other users to get a very specific message across. If you need to add something, add it in a separate comment, either a new `/* */` block, or a block of `//` line comments separated from existing comments with a blank line. ALWAYS prefix your comment text with `"CLAUDE:"` so that we know who wrote the comment, as in `/* CLAUDE: comment about something */`. You may however edit you own comments that already are prefixed with `"CLAUDE:"` at any time.

When you must write a comment, follow these guidelines:

* Keep comments BRIEF. Include ONLY the necessary points. Do NOT reiterate documentation that is already present elsewhere; refer to it instead ("see other_function", etc). Long, meandering documentation strings are more confusing than clarifying, and so is counter-productive.
* Use plain language as far as possible, and avoid technical jargon. When technical terms are used, make sure they are introduced somewhere in the codebase. Don't invent terminology on the fly; the reader will likely not understand it.
* Be specific when referring to code elements: it is best to name a structure or function explicitly. Don't refer to code elements by location ("the function above" or "the struct below"), since text blocks might be moved in the future.
* Don't refer to old behavior that no longer occurs: for example when fixing bugs, don't write "this step used to failed in the old version ... ". Just describe what the code does in the present version. There is no way to confirm old behavior once the code has changed. Such comments belong in commit messages, not in source code comments.
* Use nouns to name functions, parameters, object, concepts explicitly. Avoid noun references ("it", "those", "them", "that", "which") as far as possible, as they introduce ambiguity and makes text hard to read. It is fine to have somewhat repetitive text using the same noun many times -- this is technical documentation, not literature. If noun references must be used, make sure that they are not ambiguous. Consider the sentence _"A projection keeping every argument drops nothing and materializes its child, which is what sorts it."_ (This sentence was previously found in a doc string in `compiler.c`.) Here the first noun reference _"its"_ is okay, as it is clear that it refers to the noun _"projection"_, but the last noun reference _"it"_ is ambiguous: it could refer to either _"projection"_ or to "_its child_". Also, its unclear what _"which"_ refers to: the fact that a projection _"drops nothing"_, or that it _"materializes its child"_, or perhaps both. The noun _"projection"_ is also a vague reference to the `PROJECT` operator in `operator.c`. This makes it very hard to understand what is meant. A better sentence for this doc string is _"The PROJECT operator with no arguments removed will sort the tuples of its child relation."_ 
* Avoid long sentences with many phrases. Break long sentences with period. It is easier to comprehend several clear, short sentences than one long meandering sentence with complex punctuation. Long sentences risk ending up with multiple noun references which again is problematic.
* Prefer describing objects in singular rather than plural, since reasoning about multiple objects is more difficult and can lead to confusion. For example, in the sentence _"Recursive clauses are only compiled in the second pass, once there are services for their recursive terms to dispatch to"_, it is not clear whether each clause has exactly one service and one term, or whether clauses, services and terms are related in more complex ways. Using the singular form makes the 1:1:1 correspondence clear : _"A recursive clause is only compiled in the second pass, once there is a service for its recursive term to dispatch to"_.



## Git usage

ALWAYS ask before executing `git commit`. Do not run `git push`, `git pull` or other commands affecting the upstream; the user will handle pushing manually.

