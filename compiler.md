# The atom compiler

This document describes how atom compiles a query into a service. The implementation is
in `src/kernel/compiler.c`, whose entry point is `CompileQuery()`.

These notes began as a design sketch and now describe what is built. Where something is
not implemented, or is implemented in a way that is known to be incomplete, it says so.

## Services, dispatch and operators

A **relation** is a set of tuples over a fixed list of roles, with a fixed atom type per
column. A **service** is a relation together with a **parameter IO**, which says of each
argument whether the caller supplies it or the service produces it, and an operator that
evaluates it. So one relation may have several services, differing in what the caller has
to know in order to ask.

A relation is identity and nothing else — a term form and a column type per argument
(`src/kernel/Relation.h`). Both the ways of reading it and the storage holding its tuples
are registered *against* it, and neither is reachable from the relation:

- the **service registry** records how a relation can be read (`ServiceRegistry.h`),
- the **relation table registry** records where its tuples are stored (`RelationTableRegistry.h`).

A relation with no table registered is a **computed** relation: it has services but no
tuples of its own, which is what a machine service such as `library/math.c` is, and what
the compiler produces. A relation with a table is a **stored** relation, whose services
its storage provider registered. Which services a provider registers *is* its statement of
what it can do: `RelationBTree` registers one per prefix of its index column order, so a
signature binding a column out of that order simply has no service.

Keeping the three apart is what lets one operator serve two relations, which is the case
the next section ends on. A relation is reference counted and disappears when nothing
names it any longer; a relation table is reference counted too, and its storage outlives
the table's registration for as long as an operator is still reading it.

Parameters are written by number, direction and type: `1<INT` is argument 1, an input of
type `INT`, and `3>INT` is argument 3, an output. A service over the addition relation
that computes a sum has the signature

    + 1<INT + 2<INT = 3>INT

and the service that subtracts, by solving the same equation for the other unknown, is

    + 1<INT + 2>INT = 3<INT

**Dispatch** (`src/kernel/dispatch.c`) matches a query against the registered services: a
query atom that is a constant needs an input parameter of its own type, and a query
variable needs an output. When no service matches, the query has to be compiled from the
rules in the dictionary, which is what this document is about.

A service is evaluated by a tree of **operators** (`src/kernel/operator.h`). The leaves
are machine operators, which provide the stored and computed relations; the internal nodes
are the operators of relational algebra — `PERMUTE`, `CONSTRAIN`, `JOIN`, `PROJECT`,
`UNION` — together with `FIXPOINT` and `RECURSE`, which are what recursion adds. Every
operator yields distinct tuples in a declared order; that contract is documented in
`operator.h` and matters here in two places, noted below.

## Compiling a query

### Parameterizing the query

The query is first generalized: every non-variable actor becomes a typed input parameter
and every variable becomes an output parameter of unknown type, numbered by position.
So the query

    + 7 - 4 = d

becomes

    + 1<INT - 2<INT = d

The output types are not known yet. They are discovered as the terms of a rule compile,
and the finished signature is what the new service is registered under.

### Finding the rules

Rules are stored as clauses in conjunctive normal form, so

    number x plusone y plustwo z <- + x + 1 = y & + y + 1 = z

is stored as

    ! + x + 1 = y | ! + y + 1 = z | number x plusone y plustwo z

A clause form is a multiset of term forms, so the clauses that could answer a query are
those whose form contains the query's term form. The compiler finds them by enumerating
the `(multiset element multiple)` relation and keeping the clause forms that contain it.

For each such clause, the query is unified with the matching term. That term is then
dropped, and the rest of the clause is negated, which turns the disjunction of negated
terms back into a conjunction of positive ones — the body of the rule. Compiling that
conjunction is the work.

A term form carries a sign, so the term the query matches has the query's own sign. That
is what resolution asks for: a clause is a disjunction, and dropping one of its terms
leaves an implication whose conclusion is that same term. A negated query resolves by the
same rule as a positive one. Given the clause

    ! even x | ! odd x

the query `(! even x)` matches the term `! even x` and compiles to the body `odd x`, so
`(! even 3)` is answered by the fact `(odd 3)`; see `testCompileNegatedTerm`. Note that
this is classical negation, not negation as failure: `(! even 3)` follows from a rule or a
fact establishing it, never from the absence of `(even 3)`.

### One term: PERMUTE

Take the rule

    ! + x + y = z | + z - x = y

and the query `+ 1<INT - 2<INT = d`. Unifying the query with the matching term
`+ z - x = y` gives `{ z -> 1<INT, x -> 2<INT, y -> d }`, and negating the remainder of
the clause leaves the single term

    + 2<INT + d = 1<INT

Dispatch matches this against the subtraction service above. Parameters behave as any atom
of their type when dispatching, so the match also tells us that `d` is an `INT` output.
Substituting back into the query gives the signature of the new service:

    + 1<INT - 2<INT = 3>INT

The term's arguments are in a different order from the query's, so a `PERMUTE` operator
reorders them. `PERMUTE` also binds constants: a term such as `+ x + 1 = y` has the
constant `1` in an argument the service takes as an input, and the permute operator
supplies it. A term with neither reordering nor constants compiles to the matched service
directly.

This example is `testCompilePermute1` in `src/tests/test_compiler.c`.

### Repeated variables: CONSTRAIN

A variable occurring twice in one term constrains the two arguments providing it to be
equal. The rule

    self x <- edge e from x to x

asks for the nodes with an edge to themselves. The term compiles to the `(edge from to)`
service, and a `CONSTRAIN` operator above it keeps only the tuples whose `from` and `to`
arguments agree. See `testCompileConstrain`.

A variable repeated in the *query* is another matter, and not the compiler's. Every query
actor becomes a parameter of its own when the query is parameterized, so the compiled
service is the relation with the two positions left free, and dispatch matches on
parameter types rather than on equality. The constraint is applied to the tuples as they
are read, by the mixed type relation answering the query; see `MixedTypeRelation.h` and
`testConcatRepeatedVariable`.

### A conjunction: JOIN

With the rule and query above,

    number x plusone y plustwo z <- + x + 1 = y & + y + 1 = z

and the query `number 1<INT plusone a plustwo b`, unification leaves the conjunction

    + 1<INT + 1 = a & + a + 1 = b

A conjunction compiles to a `JOIN`, which evaluates its children left to right: for each
tuple of the left child it enumerates the right child, whose shared arguments the left
tuple has bound. The two terms are dispatched separately.

Order matters, because a term can only be dispatched once the arguments it takes as inputs
are available. Starting with `+ 1<INT + 1 = a`, dispatch matches the summing service and
resolves `a` to an `INT` output. That output then becomes an **input** of every term not
yet compiled, so the second term is now `+ 2<INT + 1 = b`, which dispatch matches in turn:

    JOIN(+ 1<INT + 1 = 2>INT, + 2<INT + 1 = 3>INT)

Had the compiler started with the right term, dispatch would have found nothing, as `a` is
not yet available as an input. It therefore tries the terms in turn and postpones the ones
that do not compile yet.

Taking the first term that compiles is sound only because a term dispatches exactly when
its own inputs are available. A recursive term is the exception: `registerTemporaryServices()`
cannot tell in advance which IO pattern the clause will need, so it registers a service for
every one of them, and the recursive term then dispatches whatever is bound at the time.
Taken first, it would win over a term whose outputs it should be consuming, and leave that
term with an input the relation it reads has no service for. `compileConjunctionRecursive()`
therefore passes over the terms twice, taking the recursive term only once no other term
compiles.

Back-substituting into the query gives the signature

    number 1<INT plusone 2>INT plustwo 3>INT

A conjunction of more than two terms compiles to a series of joins. See
`testCompileJoin1`.

### Local variables: PROJECT

In the rule above, every variable of the rule occurs in the query. With

    number x plustwo z <- + x + 1 = y & + y + 1 = z

and the query `number 1<INT plustwo a`, the variable `y` does not, and has to be dropped
from the result. Dropping an argument may leave duplicate tuples, so this needs a
`PROJECT`:

    PROJECT(JOIN(+ x + 1 = y, + y + 1 = z), {x z})

`y` cannot simply be left out of the operators below. It is shared between the two terms,
and the join needs it as an argument in order to constrain one term against the other.
Every such clause-local variable is therefore given an argument of its own, numbered after
the query arguments, and the conjunction is compiled with that extended argument tuple.
The project operator then drops them again.

`PROJECT` materializes its child into a B-tree, which both removes the duplicates and
sorts the result. This is not merely how it happens to be implemented: dropping an
argument reorders the arguments the child ordered below it, so a projection cannot in
general be streamed. It could be streamed when every dropped argument is functionally
determined by the kept arguments that outrank it — the compiler does not know that today,
and `PROJECT` always materializes. See `testCompileJoin2` and `testCompileProject`.

### Several rules: UNION

When more than one rule answers the query, each compiles to its own branch and the
branches are combined with `UNION`, which merges them and drops duplicates.

A union merges two ordered relations, so its children have to agree on the order they
yield tuples in. Each clause compiles on its own and inherits its order from the relations
its own terms read, so two branches of one rule have no reason to agree. Branches that
disagree are therefore sorted alike first, by a projection that keeps every argument: it
drops nothing and materializes its child, which is what sorts it. See `testCompileUnion`.

### Choice points

A query term that leaves an output parameter untyped may match several services, one per
relation matching its form — the element type of a list, say, is not determined by the
query. Each such term is a choice point, and each combination of choices yields a
separately typed service, so one query can compile to several. See the notes on
`ChoicePoints` in `compiler.c`, and `testCompileProject`, which compiles one service per
list element type.

## Recursive rules

A rule is recursive when the query resolves against a clause that contains the query term
again. The classic example over a stored relation is the transitive closure

    before x after y <- prec x succ y
    before x after y <- prec x succ z & before z after y

The second clause needs `(before after)` — the very relation being compiled — so at the
point where the compiler wants to dispatch that term, the service it needs does not exist.

Note that what marks the recursion is the **opposite sign**, not a negative one. In atom a
fact may be a negated term, and so may a query, so a rule `(odd x | even x)` can be read as
the implication `(odd x -> ! even x)` just as well as `(even x -> ! odd x)`, and the query
`(! even x)` recurses through the positive `(even x)` term of that rule.

### Why not top-down

The obvious approach is the one a Prolog engine takes: let the recursive leaf refer back to
the root of the operator tree, so that evaluating it re-enters the whole plan with new
argument values. The operator tree becomes a graph with a cycle in it, and the recursion
terminates on the values rather than on the plan.

This was designed and rejected. It does not terminate on cyclic data: evaluating
`before a after y` on a graph with a cycle descends forever, and a graph generally has
cycles. It also requires every context to be created lazily, since building a context tree
eagerly over a cyclic graph never bottoms out, and it requires the back edge to be an
uncounted pointer that is patched in once compilation completes, or a recursive service
could never be released.

### FIXPOINT and RECURSE

Recursion is evaluated bottom-up instead. A `FIXPOINT` operator applies its child — the
rule bodies — to the tuples derived so far, over and over, until a round derives nothing
new, accumulating them in a B-tree. A `RECURSE` operator, at a leaf of that child,
enumerates the tuples derived by the rounds so far. The transitive closure compiles to a
tree of the shape

    FIXPOINT(UNION(prec-succ relation, PROJECT(JOIN(prec-succ relation, RECURSE))))

with one branch per rule, and in practice a sort over the first branch as well, for the
reason given under UNION above.

The recursion is a loop inside one context rather than a tower of contexts, and the
operator tree stays a tree: `RECURSE` holds no reference to the fixpoint, and finds it
through the context chain when the recursion is evaluated. So there is no back edge to
patch, no uncounted pointer, and no traversal that has to know to stop. Evaluation
terminates whenever the derived relation is finite, cyclic data included.

A round collects into a second B-tree rather than inserting into the derived relation
directly, because the `RECURSE` iterators reading it hold it locked against modification.
The two are merged when the round completes, and a round that merges nothing new is the
fixpoint.

### Driving the derivation from the call bindings

Deriving the whole relation and filtering afterwards would answer `before <node> after y`
by exploring the entire graph, when the rule should explore only the part reachable from
`<node>`. Binding the query's arguments on the fixpoint's child instead gives wrong
answers: the head's bound argument is `x` while the recursive call's is `z`, so the
derivation has to cover bindings the query never asked about — just not all of them.

The fixpoint therefore keeps a second table, of the argument bindings it has been *called*
with. It is seeded from what the caller bound, and a `RECURSE` operator adds the binding it
is asked for. Each round applies the rule bodies to every binding known so far, and the
rounds stop when neither the derived tuples nor the call bindings grow. This is tabling by
call pattern, which magic sets achieve by rewriting the rules; done in the operator, the
compiler needs no rewriting.

Because the child now runs with arguments bound, the rule bodies are compiled with input
parameters, so their machine services are lookups rather than scans. With no bound
arguments the call table holds one empty binding and the whole relation is derived, which
is the right answer to a query that asks for all of it.

The two forms yield the same tuples, so the difference is visible only in how much was
derived; `FixpointNDerivedTuples()` reports that, and `testFixpointCallBinding` in
`test_operator.c` asserts it.

### Compiling a recursive rule

The clauses are taken in two passes.

The non-recursive clauses compile first, and fix the parameter types of each variant.
Those types have to be settled before a recursive clause can compile, because the service
its recursive term dispatches to is registered with them. A recursive clause therefore has
to occur together with a non-recursive clause of the same signature; on its own it simply
fails to compile.

Temporary services are then registered over the relation being compiled, each evaluated by
a `RECURSE` operator, and the recursive clauses compile against them. Dispatch resolves the
recursive term through its normal path, so permutation matching and the choice points apply
to it unchanged. The temporary services are removed afterwards, and a variant that a
recursive clause compiled into is wrapped in the `FIXPOINT` operator that derives it. Only
the services are temporary: the relation they are registered against is the one the
finished service provides.

A recursive term does not have the binding pattern of the query. A join binds what its
other terms provide, so the recursive term of the transitive closure takes `z` as an input
however the query is asked. One temporary service is registered per pattern that binds the
arguments of the query together with any of the ones it leaves free, as those are the
patterns a recursive term can ask for; which of them is used is not known until the clauses
compile. A pattern binding *fewer* arguments than the query is not among them, because the
derivation is keyed on what the query binds, so a term asking for less has no call binding
to name it.

### Termination

Nothing guarantees that a recursive service terminates. A relation over a finite stored
relation has a finite fixpoint and terminates without a guard, which is the transitive
closure case. A relation over an infinite domain does not. For

    integer n factorial f <-
      + m + 1 = n & integer m factorial e & * n * e = f

with the fact `(integer 0 factorial 1)`, the call bindings run `n = 4, 3, 2, 1, 0, -1, -2,
…` and the derivation never converges. Answering it needs the recursive clause guarded by a
precondition `? < n > 0:`, which the language does not have yet. `testCompileRecursiveJoin1`
is disabled for this reason.

## Answering a user query

A user asks a question, not for a service to be compiled: `(before x after y)` should
give every fact the knowledge base entails, whether stored or derived, and the user has
no way of knowing whether anything answers it yet. `UserQuery()` in `src/ui/query.c` is
that entry point, one layer above the compiler and dispatch.

It compiles a query the first time that query is asked, and is answered by the compiled
services from then on. Whether a query has been asked before is decided by dispatching it,
and both dispatch and the compiler work by the query **type**: the query generalized to
parameters by `GetQueryParameters()`, which is the term form together with the direction
and input type of each parameter. Two queries of one type compile to the same services, so
a match means the compilation has happened, whether by an earlier query, by the kernel or
by a stored relation registering its own services.

Generalizing is what makes the two agree. The compiler numbers every actor of a query
separately, so a variable occurring twice loses its equality constraint; were dispatch to
match that constraint, a query repeating a variable would look uncompiled while its type
was compiled, and compiling it again would register a service that exists. Take a rule
deriving `(item index)` over a LETTER and an INT column: `(item z index z)` can be
satisfied by no tuple, but its type is exactly the service compiled for
`(item e index p)`. So `DispatchQuery()` generalizes the actors it is given, and the
entry points the compiler uses take a query already generalized.

The constraint the type drops is applied where the answer is read: a `MixedTypeRelation`
over the query actors gathers the tuples of every matching service and keeps those in
which the repeated actors agree, atom type included, since dispatch no longer says
anything about the columns they matched; see `MixedTypeRelation.h`.

A query whose type compiles to nothing is compiled again every time it is asked. That
costs a walk over the rules and registers nothing, and it is what lets a query start
working once a rule answering it is asserted.

### Invalidating a compiled service

A compiled service answers as the facts and the rules stood when it was compiled, so it is
a cache, and a change to either has to remove the services it could affect. The next query
of that type then compiles them again. Removing too much costs a compilation; removing too
little gives a wrong answer, so invalidation is deliberately coarse.

Only structural change matters. A fact asserted into a relation that exists needs nothing:
the compiled service reads that relation live through its MACHINE operator. What does
matter is a relation appearing, a service appearing or disappearing, and a rule being
added or removed.

The service registry records, for each compiled service, the services its operator tree is
built on. The walk recording them stops at every service it meets, so a tree contributes a
handful of entries and a service further down is reached through the one above it. Removing
a service therefore removes the compiled services built on it, and then the ones built on
those; see `ServiceRegistryInvalidateTermForm()`.

A dependency is between two operators, and the services are the ones those operators
evaluate. Services sharing an operator are one for this purpose, and go stale together,
which is what a term compiling to the service it matched gives: the compiled service is
then nothing but that service's operator. A rule that merely renames roles, such as
`(person p dependent d) <- (parent p child d)`, produces exactly that: the compiled
service is registered against the `(person dependent)` relation and evaluated by the
machine operator of `(parent child)`, a relation it holds no pointer to.

This is a rule about the knowledge base, not about memory. Dropping the `(parent child)`
table removes the compiled service because the relation it read has left the knowledge
base, and the next query compiles it again — correctly finding nothing. It is not what
keeps the storage alive: the machine operator holds a counted reference to the table it
reads, so the storage is deallocated when the last operator reading it is gone, whatever
order things are dropped in. See `testDropTableWithSharedOperator` in
`test_service_registry.c`, which drops the table while the shared operator is still held.

None of this applies to the temporary services a recursive compilation registers for its
own recursive terms. They are scaffolding, registered and removed within one compilation,
so nothing outlives it to depend on them and nothing invalidates them; they are marked
`SERVICE_TEMPORARY` and take no part in the bookkeeping.

Three events drive it:

- **A primitive service is registered.** A query of its term form now has one more
  relation to match, so the compiled services of that form are incomplete. Hooking service
  registration rather than relation registration is what keeps the compiler out of it: the
  compiler creates relations of its own while compiling, and invalidating there could
  remove a service the compilation in flight is building on.
- **A service is removed**, which is what retracting the last fact of a relation and
  dropping its table comes to. Everything built on it goes.
- **A rule is added or removed.** A clause form is the multiset of the term forms of its
  terms, and the compiler resolves a query against the clauses whose form contains the
  query term form, so those term forms name every service the rule could have reached.
  No per-rule bookkeeping is needed, and a rule added before anything was compiled costs
  nothing.

A compiled service removed by invalidation takes its relation with it when no service and
no table names that relation any longer. Invalidation modifies
the registries, so it cannot run while a query is being read: an open `DispatchIterator`
or `MixedTypeRelation` write-locks against modification.

## Known gaps

- **Preconditions**, needed to guard a recursive clause over an infinite domain, do not
  exist. This is the only thing standing between the compiler and the factorial rule.
- **A relation with both stored facts and rules** is not handled. Compiling a query that a
  stored service already answers registers a second service of the same signature, which
  `ServiceRegistryAdd()` asserts against. The generated service should replace the existing
  one and take it as a branch of its union.
- **Evaluation is naive**, not semi-naive: every round re-expands every call binding,
  rather than only the tuples the previous round derived.
- **Duplicate work across queries.** A fixpoint derives its relation afresh for every
  context, and nothing is memoized between queries.
