VLBDB: The Very Late Bitcode and Data Binder
============================================

The VLBDB aims to be a lightweight library that enables the
runtime-specialisation of functions written in ``normal'' languages
like C, C++ or Fortran.  The latter goal is achieved by exploiting
LLVM's infrastructure; the former is achieved by hiding all of LLVM
behind a minimal interface.

_WARNING_ this is extremely experimental code.  I'm currently focusing
on getting code generation and optimization right.  Work on fast
compilation, robustness, memory management, etc. will come later.
Also, the C++ interface simply doesn't exist yet.

See `pike-regex.c` for an example: it takes Pike's tiny
sub-regular-expression matcher, and compiles it very naively, and then
a bit less naively, to native code.  The naive compiler simply marks
the regex argument as constant; the less naive compiler explicitly
specializes the regex argument, in the spirit of compilation to
closures (except that closures are further specialised and inlined in
VLBDB).

Runtime Specialisation
----------------------

VLBDB only supports a limited form of runtime specialisation: the
_first_ (few) arguments of functions can be specified to be constants.
In other words, it only implements partial application with constants.
However, unlike partial application in most functional languages,
partial application in VLBDB is followed by a constant-folding and
inlining pass.  Thus, the result of partial application is a new
native-code function, optimised to exploit the known arguments.

In addition to constant values, VLBDB also supports constant folding
through pointers to constant data.

What does it do
---------------

In addition to the obvious constant propagation, dead code
elimination, etc. that follows from known constant arguments, VLBDB
automatically specializes and inlines calls.

Inlining is strongly restricted to ensure termination and avoid space
explosions.  Only calls originally in the function can be inlined
(i.e., no recursive inlining).  Moreover, only calls to functions that
were constant-folded in can be inlined (i.e., calls that were known in
the original remain out-of-line).

Automatic specialization is similarly restricted.  With each function,
a maximum number of autospecialised argument is associated.  The
(safe) default is 0, except for block functions, for which it's 1 (the
environment argument).  New specialisations are only recursively
created for up to that many constant arguments.  However, extent
specialisations will always be exploited: the one with the most
(corresponding) constant arguments is used.  In particular, this means
that recursive calls that preserve constant arguments are simplified
into recursive calls to the specialized function.

Specialisations are keyed on *all* the constant arguments, not only
the last set.  This ensures that there is no difference between
specializing a function's first argument, and then specialising the
specialised function again, versus specialising the same function's
arguments all at once.  The additional sharing is doubly beneficial:
it increases the set of compatible extant specialisations, and
improves the effectiveness of the internal memoisation.

Using the library
=================

Using VLBDB is currently a two-step process.  First, bitcode must be
generated; then, a C or C++ program, linked to VLBDB, loads the
bitcode file and calls specialized versions of functions in the
bitcode file.

## Generating bitcode

Generating bitcode instead of object files is an essential element of
LLVM's lifelong program analysis strategy.  If you use `clang`, you
can simply execute

    clang [regular flags] -c -emit-llvm -o foo.bc

LLVM's link-time optimisations work with bitcode files.  Under
`llvm-gcc`, it's instead necessary to

    llvm-gcc [...] -c -flto -o foo.bc

## Using VLBDB

VLBDB revolves around the VLBDB binding unit.  A VLBDB unit is very
much like a compilation unit, but at runtime: functions or data in a
given unit can be inlined, constant-folded, etc., while references
leaving the current VLBDB unit can only be compiled naively.

### Registering metadata

In order to maximise the unit's usefulness, it maps addresses and
values in the program to constants or functions in the unit.
Metadata for specialized functions is automatically generated to
ensure that specialized functions can be specialized further.

#### Registering functions

However, the mapping must still be seeded with functions to
specialize.  `register_function` maps a pointer to the corresponding
function in the unit's bitcode file, along with the number of
arguments to auto-specialize.  The last argument is the function's
name; when `NULL`, VLBDB will use dynamic loading machinery to map the
pointer to a name.

`register_function_name` is a convenience wrapper around
`register_function` to register a function in the unit without
associating a pointer with it.

Finally, `register_all_functions` will attempt to register all the
functions in the bitcode file, using dynamic loading machinery, again,
to map names to addresses.

#### Registering constant data

Constant arguments of primitive types are enough to go very far.
However, particularly when working with circular structure, it's very
useful to constant fold through pointers.

The recommended way to achieve this is with `intern_range`, which,
given the beginning of a range of constant bytes and the number of
bytes, will import it as a constant range of bytes in the unit.  This
isn't directly useful, but is used by `bind_range` to implement
pointers to known constant address ranges.  The advantage of
`intern_range` and `bind_range` is that they will merge different
address ranges that are identical in content, and that the address
range can be modified or released once the function has been
specialized.

In contrast, `register_range` marks a given address range in the
program as constant-foldable.  Loads from the range may be
constant-folded away, but addresses will be preserved.  The
disadvantage of this option is that identical ranges in different
addresses will not be merged, and that the range must not be released
or modified.

### Specializing functions

In order to hide LLVM's heavy machinery, specialized functions are
constructed with an opaque VLBDB binder.  A binder is created from an
unit and a function (or block) pointer.

`bind_uint` marks the next not-yet-bound argument as equal to that
unsigned integer (of any size). `bind_int` does the same for signed
integers (coercing as needed), `bind_fp` for floating point values,
`bind_ptr` for pointers, and `bind_range` for pointers to ranges that
may be interned.

Once all the constant arguments have been bound, `vlbdb_specialize`
returns a new function that's partially applied, and optimised.

Finally, the binder must be destroyed.

## Block support

Apple introduced lexical closures for C, C++, and Objective-C, as
blocks.  They are implemented as pointers to self-describing
structures.  There is special support to parse these structures and
treat blocks as functions; the structure is considered up for
interning, and constant-folded away.
