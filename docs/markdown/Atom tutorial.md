<!-- title: Atom tutorial --><!-- date: 2026-10-21 -->

This is a brief tutorial of the main concepts of the **atom** language. We'll work through examples with a command line client to **atom** running in the browser, so you can freely experiment with the language. This instance of atom is in-memory only, so the session is not saved anywhere. A local version of the client with a persistent session can be built from [source](https://github.com/roland-redmount/atom).

For this tutorial we need to load an example data set, which is a family tree from Tolkien's *Silmarillion*. Run these commands with the `run` button, or with `ctrl + enter`.
```atom
:load data/finwe-family/parent_child.csv parent child
:load data/finwe-family/spouse.csv spouse spouse
:load data/finwe-family/person.csv person sex
:load data/finwe-family/person.csv person birth-era
:load data/finwe-family/person.csv person birth-year
```
All input fields in this tutorial are editable and connected to a single running **atom** session, which can be reset with the `reset` button. Feel free to experiment!

## Atoms and relations

All data in **atom** is stored as *facts*, which are logical statements known to be true. A set of facts is called a *relation*, which is similar to a database table, or a dataframe in R or pandas.  Let's start with a very simple query:
```atom
parent "Finwe" child "Feanor"
```
Here `parent` and `child` are *roles*, and `"Finwe"` and `"Feanor"` are *atoms*. An atom is essentially just an identifier -- it has no internal structure, hence the name. The entire line is a *predicate*, a simple logical statement that relates the atoms to each other. This predicate states that Finwe is a parent of Feanor, or equivalently, that Feanor is a child of Finwe. Running the query returns a relation consisting of a single fact, namely the predicate itself. This means that this logical statement is indeed true.

On the other hand, the query
```atom
parent "Finwe" child "Galadriel"
```
yields nothing, because this predicate is not known to be true (it is not a fact). That doesn't necessarily mean that Finwe is *not* the parent of Galadriel, only that we *don't know* for sure that he is. More about this later.

### Variables

Variables let us view relations consisting of multiple facts. For example,
```atom
parent "Finwe" child c
```
yields the relation consisting of all known facts with `"Finwe"` in the `parent` role. The variable `c` means that any atom can occur in the `child` role. Variable names in **atom** are just single letters: they are local to the formula and not meant to be descriptive -- that's what the role names are for.

The syntax of **atom** queries is designed to be compact: for comparison, the equivalent SQL query would be
```
SELECT parent, child
FROM parent_child
WHERE parent = "Finwe"
```
In **atom**, the relation itself does not have a dedicated name. When needed, we refer to the relation by a tuple of roles`(parent child)`. Roles can be given in any order: the query
```atom
child c parent "Finwe"
```
is the same as the above. The command line client always prints facts with roles in the order entered by the user, but role order is not significant to the **atom** language itself.

To view all parent-child relationships in this relation, we would use two variables:
```atom
parent p child c
```
We can also use the anonymous variable `_` whenever we don't need variable names. Each occurrence of `_` counts as a distinct variable. Try it in the above query.

### Relations vs. functions

Relations can be viewed as a generalization of functions. While a function is a one-way mapping from inputs to outputs, a relation has no particular input-output direction, but can be queried in multiple ways. For example,
```atom
child "Feanor" parent p 
```
views the parents of a given child, rather than the children of a given parent, using the same relation. This is important, because it allows all information to be kept in one place. A functional language would need two independent functions `parent(x) -> y` and `child(y) -> x` to represent the same information, leaving it up to the programmer to make sure that they are consistent with each other -- the language has no way of knowing that they should be. A relation guarantees that the two directions are always consistent.

### Symmetry

Some relations are fundamentally symmetric: for example, if `x` is a spouse of `y`, then `y` is also a spouse of `x`. To handle such relations, **atom** allows a role to be associated with more than one atom in a predicate:
```atom
spouse _ spouse _
```
These two queries yield the same relation:
```atom
spouse "Feanor" spouse _
spouse _ spouse "Feanor"
```

## Computing with relations

Relations are not merely data tables: they can also represent computation. For example, addition is represented by the relation `( + + = )`,
```atom
+ 1 + 2 = _
```
Here `+` and `=` are role names, with no special significance. Note that this relation has symmetry, with two atoms in the `+` role, so that `+ 2 + 1 = _` is the same query as the above. The addition relation is a primitive built into the **atom** system, but we can also define new computed relations (see [[Atom tutorial#Logic rules|logic rules]]). 

The **atom** language makes no distinction between "computed" and "stored" relations. This unifies code and data, letting us express both computation and data access as queries or views rather than procedures. For example, we can think of the addition query above as viewing one fact from an infinite relation `(+ + =)` over all integers. Variables are analogous to (multiple) return values in functional or imperative languages. 

Since relations have no input-output direction, we can also run the "inverse" query
```atom
+ 1 + x = 3
```
to solve the equation for `x`. Such queries are not *always* possible, since many operations are difficult or impossible to invert. For example, if  `(complex z riemann-zeta r)` represents the Riemann zeta function of a complex number `z`, then computing the query `complex z riemann-zeta 0` would amount to proving the [Riemann hypothesis](https://en.wikipedia.org/wiki/Riemann_hypothesis). But *if* an inverse is available, it can be represented with a relation.

Relations also handle corner cases gracefully, since computations that do not have a well-defined result simply yield no tuples, rather than causing an error. For example
```atom
* 1 / 0 = _
```
yields no tuple since division by zero is not defined. In most languages, implementing division by a function `divide(x, y) -> z` requires error handling, because a function *must* return a value. In **atom**, this kind of error does not occur.

## Combining relations

We often need to combine facts from different relations. In **atom**, as in other logic languages, this is done by logical AND (`&`) between two or more predicates. For example, the following query combines the `(parent child)` relation with the `(person sex)` relation to find the mother of Feanor:
```atom
parent m child "Feanor" & person m sex "female"
```
Here the variable `m` occurs twice, indicating an equality constraint: the same atom must occur in the roles `parent` and `person`. This corresponds to the SQL inner join query
```
SELECT parent, child, person, sex
FROM parent_child
JOIN person_sex ON (parent_child.parent = person_sex.sex)
WHERE person_sex.sex = "female" AND parent_child.child = "Feanor"
```

We can also combine a relation with itself: for example, to view Finwe's children with his other spouse Indis, we can write
```atom
parent "Finwe" child c & parent "Indis" child c
```
As an exercise, try writing a query to view all children and grandchildren of Finwe.

We can combine computed relations with `&` in the same way. For example,
```atom
* y * y = _ & =< y >= 1 & >= y =< 10
```
computes the squares of numbers 1 through 10. Here `=< y >= 1` is a relation over all integers greater than or equal to 1 (where `>=`and `=<` are roles) and `>= y =< 10` is the opposite bound. These relations are infinite on their own, but their join is not. This can be viewed as a mechanism for iteration over a set of values, though I prefer to think about it as just *viewing* the set defined by the query formula.

The output may look odd since the command line client by default prints the full formula for every fact. If you like, you can switch the client to a table view with the command `:view table`. This will show a table for the unique, named variables only (try it in the above). Switch back with `:view formula` .

Computed and stored relations can of course be mixed freely, as the language makes no distinction between them.  For example, 
```atom
person p birth-era "YT" & person p birth-year y & < y > 1200
```
views the individuals born after the year 1200 YT (years of the trees). Here `< a > b` is the strict inequality. 

## Logic rules

To build more complex views of data and computations, we define new relations from existing ones using logic rules. An example of a logic rule is
```atom
:assert mother m child c <-
  parent m child c & person m sex "female"
```
Here `:assert` is a client command to assert (create) a rule, and `<-` means logical implication. It is hopefully clear that the above rule defines a `mother` of a `child` as a `parent` of that child who is female.  Having created this rule, we can now query
```atom
mother _ child "Feanor"
```
Rules can of course depend on other rules to successively build up increasingly complex transformations of data. Here's an example, first defining the predicate `even` testing if an integer is even, and then defining `odd` based on `even`
```atom
:assert even n <- * n / 2 = q rem 0
:assert odd n <- + n - 1 = m & even m
```
The relation `(* / = rem)` is integer division with remainder. In the `odd` rule, we first find the number `m` which is one less than `n`, and test if it is even. Test it out:
```atom
odd 3
```

This is what "programming" looks like in **atom**. Rules are not really programs though, but rather specifications that **atom** uses to *generate* programs -- so writing rules is more of "designing" than "programming." (Under the hood, **atom** performs just-in-time compilation of queries to generate programs that compute relations. We won't delve into this in the tutorial, but for a sneak peek try the `:inspect` command on a query, such as `:inspect odd 3`. This shows how the query is translated into a program consisting of relational operators.)

### Rules in clause form

While it is often convenient to express rules with the  `<-` connective, internally each rule is stored as a *clause*, a set of terms combined with logical OR (`|`). We can view rules in clause form with the command
```atom
:rules mother child
```
which lists rules involving a term of the form `(mother child)`. The clause shown is the same as
```
mother m child c <- parent m child c & person m sex "female"
```
Storing rules in clause form is important because it allows additional logical inferences. For example, if we state that a `person` is a `parent` but not a `mother`, then **atom** can deduce that the person cannot be female, since the above clause is also equivalent to the implication
```
! person p sex "female" <- ! mother p child c & parent p child c
```
So rules are not merely one-way routes from assumptions to conclusion. In general, a clause with *n* terms encodes *n* logical implications.

### Recursive rules

Rules can also be recursive. For example, we can compute all ancestors of a person as
```atom
:assert ancestor a descendant d <- parent a child d
:assert ancestor a descendant d <-
  parent a child x & ancestor x descendant d
```
Here the second rule is recursive, as it queries the `(ancestor descendant)` relation. In this case, this yields the transitive closure of the parent-child graph. So the descendants of Finwe are given by
```atom
ancestor "Finwe" descendant _
```
We can also query the relation in the other direction; for example, the ancestors of Galadriel are
```atom
ancestor _ descendant "Galadriel"
```

Of course, recursion can be used with computed relations in the same way. Here's a classic example, computing the Fibonacci number sequence, which requires a more complex rule:
```atom
:assert fibonacci 0 number 0
:assert fibonacci 1 number 1
:assert fibonacci f number n <-
  =< n >= 2 &
  + n - 1 = p & fibonacci g number p &
  + n - 2 = q & fibonacci h number q &
  + g + h = f
```
Here we assert the first numbers (which terminate the recursion) as plain facts; then we specify the recursion formula *f(n) = f(n-1) + f(n-2)*. Since **atom** does not have functions, rules must include intermediate steps: we must define  `+ n - 1 = p` and `+ n - 2 = q`, and we must add the recursively computed values `g` and `h` separately.  The order of terms in a rule does not matter -- the compiler will determine the order in which terms are evaluated.

Here are the first Fibonacci numbers:
```atom
fibonacci f number n & =< n >= 1 & >= n =< 10
```


## Creating and modifying data

Modifying data in **atom** means adding or removing facts. This is done by *actions*. In the client used here, actions are written with a `:` prefix. The simplest action is to *assert* a single fact, for example
```atom
:assert parent "Celebrian" child "Arwen"
```
asserts that Elrond is a parent of Arwen. Test the new data with a query:
```atom
ancestor _ descendant "Arwen"
```

We can remove a fact with the *retract* action,
```atom
:retract parent "Celebrian" child "Arwen"
```

### Creating atoms

As we create new facts, we naturally also need to introduce new atoms. A small set of literals can be entered directly, including numbers and letters; all other atoms are *defined by facts*.

To define an atom, we use a *generator* rule, a special rule that lets us to query for atoms with a particular definition. For example, a circle is defined entirely by its radius, and if we want an atom representing a circle, we first assert a generator rule
```atom
:assert circle * radius r
```
where the special symbol `*` indicates that the atom in this position is *defined by* the fact. With this rule in place, we can obtain a circle with a given radius using an ordinary query:
```atom
circle _ radius 5
```
The returned atom `ID(...)` represents this circle. This atom is unique: repeating the query will give the same atom.  Note that the query does *not* change the knowledgebase: the change occurs when the generator rule is asserted. We can think of the generator rule as defining an implicit, infinite relation containing all possible circles, of any radius. The query merely asks for one of them.

The **atom** language does not have local variables that we can assign atoms to, but the command line client allows tagging atoms returned by queries using the `@tag` syntax: for example
```atom
circle @myCircle radius 5
```
assigns the returned circle atom to the tag `@myCircle`, which can then be used in queries. For example, this rule gives us the circle area:
```atom
:assert circle c area a <-
  circle c radius r & * r * r = s & * 3.14 * s = a

circle @myCircle area _
```

A fact that defines an atom cannot be retracted:
```atom
:retract circle @myCircle radius 5
```
This is because the fact was not explicitly asserted: it is *entailed* by the generator rule, and so will hold true as long as the generator rule is in place. The same goes for facts entails by other rules:
```atom
:retract circle @myCircle area 78.5
```

## Types

The **atom** language does not have a dedicated type system, since predicates are essentially the same thing as types. For example, `integer x` is true for all atoms that represent integers,
```atom
integer -17
```
and false for anything else,
```atom
! integer 3.14
```
The type "integer" is then just the set of all atoms that satisfy this predicate. In general, a type *T* is just the set of atoms that satisfy a "type predicate" for *T*.  A type predicates can be any logical rule. For example, a "circle" type could be defined as
```atom
:assert circle c <- circle c radius _
```
which lets us test if an atom is a circle,
```atom
circle @myCircle
```
Similarly, a type predicate for the natural numbers (positive integers) can be defined as
```atom
:assert natural-number n <- integer n & < n > 0
```

## Data structures

In an object-oriented language we might represent a car with an data structure like
```
class Car {
   String brand;
   String model;
}

Car myCar = {.brand = "Porsche", model = 911}
```

The **atom** language replaces objects with relations, which can represent the same information. First, instead of instantiating an object, we define an atom `@myCar` by a name:
```atom
:assert named * name n
named @myCar name "myCar"
```
At this point the `@myCar` atom has no data attached to it other than its name. We then build up the data structure by adding facts:
```atom
:assert car @myCar brand "Porsche"
:assert car @myCar model 911
```
This can be viewed as a mutable data structure: both properties of `@myCar` can be changed or removed entirely at any time, since the atom is defined by name only. This makes it easy to represent missing values and heterogeneous data. Unlike the object-oriented view, there is no notion of composition: the `brand` and `model` atoms are not contained in, or "owned" by, the `@myCar` atom in any way.

Similar to the `Car` class, we can define a type predicate for cars:
```atom
:assert car c <- car c brand _ & car c model _
```
This asserts that an atom *is a* car if it has a brand and a model. (That's probably not the best definition of a car, but that's another story.) Let's confirm that `@myCar` indeed is a car in this sense:
```atom
car @myCar
```
Note that "types" in this sense are dynamic: if we retract either of the `model` or `brand` facts, then `@myCar` is no longer a car.

### Immutable data structures

An immutable data structure can be represented by an atom whose defining facts describe that data structure. For example, an immutable 2D point:
```atom
:assert point * x x & point * y y
```
Here the generator rule includes two predicates specifying the `x` and `y` coordinates of the points. This corresponds to an immutable `class Point { float x; float y}`. Here is the point with coordinates (-1, 3):
```atom
point @p x -1 & point @p y 3
```
The point is immutable:
```atom
:retract point @p x -1
```

Let's define vector addition of two points to yields another (immutable) point:
```atom
:assert point p point q sum s <-
  point p x a & point p y b &
  point q x c & point q y d &
  + a + c = e & + b + d = f &
  point s x e & point s y f
```
For example, adding (-1, 3) to (5, -2):
```atom
point @q x 5 & point @q y -2
point @p point @q sum @s
point @s x _ & point @s y_
```

<!--
For example, we can model a physical quantity (like a measurement) to consist of a value (magnitude) and a unit:
```atom
:assert quantity * value v unit u
```
Here's a temperature atom, measured in the Celsius unit:
```atom
quantity @roomTemp value 23.0 unit "Celsius"
```

Conversion between Celsius and Kelvin:
```atom
:assert quantity t value k unit "Kelvin" <-
  + c + 273.15 = k & quantity t value c unit "Celsius"
```

```atom
quantity @roomTemp value _ unit "Kelvin"
```

TODO: this requires ifacts and asserted facts to coexist -- requires multiple TupleStores per relation.

A mutable temperature:
```
named @someTemp name "someTemp"

:assert quantity @someTemp value 15.0 unit "Celsius"

quantity @1 value _ unit "Kelvin"
```

Changing the temperature in Celsius units affects the temperature in Kelvin too:
```atom
:retract quantity @someTemp value 20.0 unit "Celsius" 
:assert quantity @someTemp value 30.0 unit "Celsius"
quantity @1 value _ unit "Kelvin"
```

TODO: make `quantity` a function of `(value, unit)`,  meaning that `(value, unit)` is a unique key for the relation. 

Adding two quantities of different units.
-->

## Logical consistency

As a logic language, **atom** allows expressing data integrity constraints natively, in the language itself. Any modification to the knowledgebase is tested for logical consistency before making changes, so all we need to do is provide the rules that valid data must satisfy. For example, for the *Silmarillion* family tree data set, we might add a rule that a child cannot be a parent of any of its ancestors:
```atom
:assert ! parent d child a <- ancestor a descendant d
```
Any attempt to assert a fact that contradicts this rule will now fail, because it would render the knowledgebase inconsistent:
```atom
:assert parent "Galadriel" child "Finwe"
```
As always, this holds for computed relations as well. For example, 
```atom
:assert + 1 + 1 = 3
```
is rejected because the negated fact `! + 1 + 1 = 3` is inferred from the built-in arithmetic relations. Any data integrity condition that can be expressed in the language will be automatically enforced: for example, we might restrict a user input to be a positive number by a rule
```atom
:assert < n > 0 <- input-number n
```
With this rule in place, **atom** will reject malformed input:
```atom
:assert input-number -3
```

<!--
To navigate an **atom** world, it is helpful to be able to ask "what relations does this atom participate in?". This is handled by
```
// need to add this command to REPL
:lookup "Finwe"
```
-->

## Conclusion

Hopefully this tutorial gave you a feel for the **atom** language and what it can do. If you want to experiment further, a command-line version of the REPL client can be build from [source](https://github.com/roland-redmount/atom).

Please not that the **atom** language is still a work in progress. Not all features are in place, and there are a number of known bugs. If you run into issues, [report them here](https://github.com/roland-redmount/atom/issues).  If you are interested in contributing, please contact [roland@redmount.net](mailto:roland@redmount.net)
