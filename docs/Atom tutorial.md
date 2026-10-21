<!-- title: Atom tutorial -->
<!-- date: 2026-10-21 -->

This is a brief tutorial of the main concepts of the atom language. We'll work through examples with a command-line interface to **atom** running in the browser, so you can freely experiment with the language. This instance of atom has some toy data sets pre-loaded; it is in-memory only, so the session is not saved anywhere. A local version of the command-line client with persistent session can be built from [source](https://github.com/roland-redmount/atom).
## Atoms and relations

All data in **atom** is stored as *facts*, which are logical statements known to be true. A set of facts is called a *relation*, which is similar to a database table, or a dataframe in R / python. As an example, we'll look at some facts in small dataset on elven characters in Tolkien's *Silmarillion* (we are nerds after all). Here's a simple query that yields a relation:
```atom
parent "Finwe" child "Feanor"
```
Here `parent` and `child` are *roles*, and `"Finwe"` and `"Feanor"` are *atoms*. An atom is essentially just an identifier: it has no internal structure (hence the name). The entire line is a *predicate*, a simple logical statement that relate the atoms to each other; in this case stating that Finwe is a parent of Feanor, or equivalently, that Feanor is the child of Finwe. Running the query returns a relation consisting of a single fact, the predicate itself. This means that this logical statement is indeed true in the current *world* -- the set of all facts known to running **atom** process. On the other hand,
```atom
parent "Finwe" child "Galadriel"
```
yields nothing, because this predicate is not known to be true the current world; it is not a fact. To be precise, that doesn't necessarily mean that Finwe is *not* the parent of Galadriel, only that we *don't know* for sure that he is. More about this later.

### Variables

A somewhat more interesting view is obtained by introducing a variable:
```atom
parent "Finwe" child c
```
This yields the relation consisting of all known facts where `"Finwe"` is the parent. The variable `c` means that any atom can occur in the role `child` in the relation. Variable names in **atom** are just single letters: they are local to the formula and not meant to be descriptive -- that's what the role name (`child`) is for.

An equivalent SQL query would be
```
SELECT parent, child
FROM parent_child
WHERE parent = "Finwe"
```
One difference is that in **atom**, the relation itself does not have a dedicated name. When needed, we refer to the relation by a tuple of roles`(parent child)`.

*Drop or move this:*
In contrast, in prolog or datalog, the parent-child relation would be named something like `parent_child(x, y)` and the user must figure our from documentation which of `x` and `y` is the parent and which is the child . (A functional language would have the one-way `parent(x) -> y`, and an object-oriented language would have somethings like `x.is_parent_of(y)`, which is clear for functions with a single argument, but not for multiple arguments, and we must go consult the docs again.)

Because roles are named, they can be given in any order: the query
```atom
child c parent "Finwe"
```
is the same as the above. The command line interface always returns facts with roles in the order entered by the user, but role order is not significant to the **atom** language itself.

### Relations vs. functions

While a function is a one-way mapping from inputs to outputs, a relation has no particular input-output direction, but can be queried in multiple ways. For example,
```atom
child "Feanor" parent p 
```
views the parents of a given child, rather than the children of a given parent. This is important, because it allows all information to be kept in one place. A functional language would require two independent functions `parent(x) -> y` and `child(y) -> x` to represent the same information, and it is then up to the programmer to make sure that they are consistent with each other; the language has no way of knowing that they should be. With relations, consistency is guaranteed by definition.

To view all parent-child relationships om this relation, we would use two variables:
```atom
parent p child c
```
We can also use the anonyuous variable `_` whenever we don't variable names. Each occurence of `_` counts as a distinct variable, so
```atom
parent _ child _
```
is the same query as above.

Some relations are fundamentally symmetric: for example, if `x` is a spouse of `y`, then `y` is also a spouse of `x`. To handle such relations, **atom** allows a role to associated with more than one atom in a predicate:
```atom
spouse _ spouse _
```
As before, role order is  These two queries yield the same relation:
```atom
spouse "Feanor" spouse _
spouse _ spouse "Feanor"
```

## Computed relations

Relations are not merely data tables: they can also represent computation. For example, addition is represented by the relation `( + + = )`,
```atom
+ 1 + 2 = _
```
Here `+` and `=` are role names, with no special significance. Note that this relation has symmetry, with two atoms in the `+` role. This particular relation is a primitive built into the **atom** system, but we can also define new computed relations (see [[Atom tutorial#Logic rules|logic rules]]). 

The **atom** language itself makes no distinction between "computed" and "stored" relations, but treats all queries in the same way. This allows expressing computation as questions or statements rather than procedures. Variables in queries are analogous to return values in functional or imperative languages. For example, we can think of the above query as selecting one fact from an infinite relation `(+ + -)` over all integers. 

Since relations have no input-output direction, we can also do the "inverse" query
```atom
+ 1 + x = 3
```
to solve the equation $1 + x = 3$ for $x$. Such queries are not *always* possible, since not all operations can be easily inverted. For example, if we `(complex riemann-zeta)` represents the [Riemann zeta function](https://en.wikipedia.org/wiki/Riemann_zeta_function) of a complex number, computing the query `complex z riemann-zeta 0` would require proving the [Riemann hypothesis](https://en.wikipedia.org/wiki/Riemann_hypothesis). There are many examples of functions whose inverse is difficult to compute; but *if* an inverse is available, it can be represented with a relation.

Relations handle corner cases gracefully, since computations that do not have a well-defined result simply yield no tuples, rather than causing an error. For example
```atom
* 1 / 0 = _
```
yields no tuple since division by zero is not defined. In contrast, implementing division $z = x / y$ by a function `divide(x, y) -> z` requires error handling, because functions must return a value. In **atom**, this kind of error does not exist.

## Creating facts and atoms

While data analysis in **atom** can be done almost exclusively with queries, we can of course also create new facts, or retract existing facts. This is done by *actions*, operations that modify the world. In the command-line client, actions are written with a `:` prefix. The simplest action is to *assert* a single fact, for example
```atom
:assert parent "Elrond" child "Arwen"
```
asserts that Elrond is a parent of Arwen. Removing facts are done with the *retract* action,
```atom
:retract parent "Elrond" child "Arwen"
```

As we create new facts, we naturally also need to introduce new atoms. An atom is essentially  an identifier that represents some thing or concept; it has no internal structure. There two ways to create atoms:
* a small set of literal atoms can be entered directly, including numbers and letters.
* all other atoms are *defined by facts*.

The latter is the more interesting case. To 


*NOTE: we need a way to list the ifacts of an ID atom.*
In the above we also created a new atom `"Arwen"`. This atom is a string, which is actually just syntactic sugar for a list of characters ; we can view this as
```atom
list "Arwen" element _ position _
```
This is also an example of data structure implemented with a relation. See [[Atom tutorial#Data structures and types|data structures]]. 



## Combining relations

We often need to combine facts from different relations. In **atom**, as in other logic languages, this is done by logical AND (`&`) between two or more predicates. For example, the following query combines the `(parent child)` relation with the `(person sex)` relation to find the mother of Feanor:
```
parent m child "Feanor" & person m sex "female"
```
Here the variable `m` occurs twice, indicating an equality constraint: in each fact, the same atom must occur in the roles `parent` and `person`. This corresponds to an *inner join* in database query languages. 

We can also combine a relation with itself: for example, to view the Finwe's childen with his other spouse Indis, we can write
```atom
parent "Finwe" child c & parent "Indis" child c
```

Similarly, to view two generations of descendants of Finwe
```atom
parent "Finwe" child c & parent c child _
```

*We need a bit more on how joins can be used for computation*
Computed and stored relations can be mixed freely: there language itself makes no distinction between them.  For example, 
```
person p birth-era "YT" & person p birth-year y & < y > 1200
```
views the individuals born after the year 1200 YT (years of the trees). The computed relation `< a > b` (where `<` and `>` are roles) expresses the strict numeric inequality $a > b$. 

## Iteration

Relations can also be implement a simple iteration primitive, since queries *enumerate* tuples. For example,
```atom
=< x >= 1 & >= x =< 10
```
enumerates all integers `x` between 1 and 10 (inclusive). I prefer to think about this as *viewing* the set $\{x : x \ge 1 \land x \le 10 \}$, but it is essentially the same thing. The output may look odd since the command line client prints the full formula for every fact, but this is merely a formatting issue.

## Lookup

To navigate an **atom** world, it is helpful to be able to ask "what relations does this atom participate in". This is handled by
```
// need to add this command to REPL
:lookup "Finwe"
```

## Logic rules

To build more complex views of data, we define new relations from existing ones using logic rules. An example of a logic rule is
```atom
:assert mother m child c <- parent m child c & person m sex "female"
```
Here `:assert` is a command to the interpreter to assert (create) a rule, not a part of the **atom** language proper. It is hopefully clear that the above rule defines a `mother` of a `child` as a `parent` of that child who is female.  Having created this rule, we can now query
```atom
mother _ child "Feanor"
```
Rules can of course depend on other rules to successively build up increasingly complex transformations of data. This what "programming" looks like in **atom**, although rules are not really programs, but rather specifications that **atom** uses to create a program: under the hood, **atom** performs just-in-time compilation of queries to generate programs that compute the result.

If you're familiar with predicate logic, you'll notice that rules have *universally quantified* variables: the above rule is true with *any* atoms in place of the variables `f` and `p`: in predicate logic we would write $\forall f, c:  \text{mother(f, c)} \leftarrow \text{parent(f, c)} \;\land\; \text{sex}(f, \text{female})$.  We can also state that a predicate becomes true if there exists *some* atom(s) for which other predicate(s) are true; for example,
```
:assert parent p <- parent p child c
```
Because the variable `c` is not present in `(parent p)`, this rule is equivalent to $\forall p: \text{parent(p)} \leftarrow \exists c: \text{parent(f, c)}$, which is the same as $\forall p,c: \text{parent(p)} \leftarrow \text{parent(f, c)}$. 

*This comes later*
A unary predicate like `parent p` can also be seen a type definition: the type "parent" is the set of all atoms `p` that satisfy `parent p`. So the logic essentially gives a type system for free. 

Another example is
```
:assert grandparent p grandchild c <- parent p child x & parent x child c
```
where `grandparent p grandchild c` is true if there exists some `x` in the intermediate generation.

While it is often convenient to express rules with the  `<-` connective, internally each rule is stored as a *clause*, a set of terms combined with logical OR (`|`). We can view rules in this form with the command
```
:rules mother child
```
which lists any rule involving a term of the form `(mother child)`. In this case there is only one. The is important ibecuasethat it allows the implication to be reversed. For example, if we state that a `person` is a `parent` but not a `mother`, then **atom** can deduce that the person cannot be female, since the above clause is also equivalent to the implication
```
! person p sex "female" <- ! mother p child c & parent p child c
```
Therefore, rules are not merely one-way routes from assumptions to conclusions. In general, a clause with *n* terms encodes *n* logical implications.

### Recursive rules

Tules can also be recursive. For example, we can compute all ancestors of a person as
```
:assert ancestor a descendant d <- parent a child d
:assert ancestor a descendant d <- parent a child x & ancestor x descendant d
```
where the second rule is recursive. So the descendants of Finwe are
```
ancestor "Finwe" descendant _
```
And, in the other direction, the ancestors of Galadriel are
```
ancestor _ descendant "Galadriel"
```

Of course, recursion can be used with computed relations in the same way. Here's a classic example, computing the Fibonacci number sequence, which requires a more complex rule:
```
:assert fibonacci 0 number 0
:assert fibonacci 1 number 1
:assert fibonacci f number n <- =< n >= 2 & + n - 1 = p & fibonacci g number p & + n - 2 = q & fibonacci h number q & + g + h = f
```
Here we assert the first numbers (which terminate the recursion) as plain facts; then we specifies the recursion formula $f(n) = f(n-1) + f(n-2)$. Since **atom** does not have functions, rules must include intermediate steps: we must define  `+ n - 1 = p` and `+ n - 2 = q`, and we must add the recursively computed values `g` and `h` separately.  The order of terms in a rule does not matter -- the compiler will determine the order in which terms are evaluated.

Here are the first Fibonacci numbers:
```
fibonacci f number n & =< n >= 1 & >= n =< 10
```


## Data structures and types

Data structures in **atom** are represented by relations. For example, in an object-oriented language we might represent a generic car with a structure like
```
class Car {
   String brand;
   String model;
}
```
While an object is created from its class, specifying a value for each field,
```
Car myCar = {.brand = "Porsche", model = 911}
```
In **atom**, a specific car is an atom, and we represent the same data structure with the facts
```
car @myCar brand "Porche"
car @myCar model 911
```
Unlike the `Car` class, this data structure is flexible: we can represent missing values by simply omitting facts. It is therefore more suited for heterogenous data.

Many languages make a distinction between mutable and immutable objects. The atom counterpart of an immutable object is an atom defined by a set of facts.
*Need a better example here.*


; for example, we can define the atom by a name
```
named @myCar name "Sally"
```
and define its "components" by
```
:assert car @myCar brand "Porche"
:assert car @myCar model 911
```
This can be seen as a mutable data structure: from its definition, any properties of the atom `@myCar` can be changed.

*NOTE: this is not yet supported:*
```
:assert car * brand b & car * model m
```

### Temperature example

Physical measurements (quantities) consist of a value (magnitude) and a unit. This is a triple where the quantity is defined by its magnitude and unit.


```
:assert quantity * value v unit u

quantity _ value 20.0 unit "Celsius"
```
Conversion between Celsius and Kelvin:
```
:assert quantity t value k unit "Kelvin" | ! + c + 273.15 = k | ! quantity t value c unit "Celsius"

quantity @1 value _ unit "Kelvin"
```

Adding two quantities of different units.

A mutable temperature:
```

:assert named * name n
named _ name "temp"

:assert quantity @1 value 20.0 unit "Celsius"
:assert quantity t value k unit "Kelvin" | ! + c + 273.15 = k | ! quantity t value c unit "Celsius"

quantity @1 value _ unit "Kelvin"
```

Changing the temperature in Celsius units affects the temperature in Kelvin too:
```
:retract quantity @1 value 20.0 unit "Celsius" 
:assert quantity @1 value 30.0 unit "Celsius"
quantity @1 value _ unit "Kelvin"
```

TODO: make `quantity` a function of `(value, unit)`,  meaning that `(value, unit)` is a unique key for the relation. 




## Logical consistency

Because **atom** is a logic language, we can use the language itself to ensure logical consistency of both data (facts) and code (logic rules).  For example, given the above examples, we might add a rule that a child cannot be a parent of any of its ancestors:
```atom
:assert ! parent d child a <- ancestor a descendant d
```
Then any attempt to assert a fact that contradicts this rule will fail:
```atom
:assert parent "Galadriel" child "Finwe"
```
In this way, describing data structures by logic rules automatically ensures data integrity. This holds for computed relations as well: for example, 
```atom
:assert + 1 + 1 = 3
```
is rejected because `! + 1 + 1 + 3` is inferred from the built-in arithmetic relations. This mechanism allows enforcing any condition that can be expressed in the language: for example, we might restrict a user input to be a positive number by a rule
```atom
:assert < n > 0 <- input-number n
```
With this rule in place, **atom** will reject malformed input:
```atom
:assert input-number -3
```

