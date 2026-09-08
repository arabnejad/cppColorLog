# Contributing to cppColorLogger

This guide is for developers who want to change cppColorLogger. It explains
why the source-context parser exists, how its helpers work, and how to test a
change safely.

If you only want to use the logger, read the
[source-context user guide](docs/source_context_parser.md) instead.

## Project rules

Please keep these rules in mind:

- The library must remain a single header.
- The code must compile as C++11.
- GCC, Clang, and MSVC must remain supported.
- `LOGGER_LOG` must detect normal function and method names automatically.
- `LOGGER_LOG_WITH_CONTEXT` must let the user choose the context.
- Compiler-specific code belongs in `cppcolorlogger_detail`.
- Every new compiler-signature case needs a regression test.

## The problem the parser solves

C++11 provides `__func__`. It usually gives the simple name of the current
function:

```text
start
```

That name is useful, but it does not normally include the class:

```text
Service::start
```

The supported compilers provide more information:

- GCC and Clang provide `__PRETTY_FUNCTION__`.
- MSVC provides `__FUNCSIG__`.

For the same method, they can produce text like this:

```text
GCC:  void Service::start()
MSVC: public: void __cdecl Service::start(void)
```

The logger passes this text to `normalizeFunctionSignature()`. That function
removes the return type, parameters, calling convention, and other details that
would make the log difficult to read.

The logging macros choose the best value available on the current compiler:

```cpp
#if defined(_MSC_VER)
#define CPPCOLORLOG_SIGNATURE __FUNCSIG__
#elif defined(__clang__) || defined(__GNUC__)
#define CPPCOLORLOG_SIGNATURE __PRETTY_FUNCTION__
#else
#define CPPCOLORLOG_SIGNATURE __func__
#endif
```

`LOGGER_LOG` also passes `__func__` as a fallback. If the detailed signature
cannot be understood, the logger can still display a simple function name.

## Where this behavior comes from

These official references explain where the compiler strings come from:

- [C++ draft: function definitions and `__func__`](https://eel.is/c++draft/dcl.fct.def.general)
- [GCC: Function Names as Strings](https://gcc.gnu.org/onlinedocs/gcc/Function-Names.html)
- [Microsoft: predefined macros, including `__FUNCSIG__`](https://learn.microsoft.com/en-us/cpp/preprocessor/predefined-macros?view=msvc-170)
- [Clang: source-location builtins](https://clang.llvm.org/docs/LanguageExtensions.html#source-location-builtins)

The references guarantee that function-name information is available. They do
not define one shared text format for all compilers. Templates, lambdas,
operators, and anonymous namespaces may be written differently.

For example:

```text
GCC or Clang: Functor::operator()
MSVC:         Functor::operator ()
```

For that reason, this parser is not an implementation of a standard grammar.
Its exact output is defined by the examples and tests in
[`tests/test_logger.cpp`](tests/test_logger.cpp).

## Parser terminology

| Term | Meaning | Example |
| --- | --- | --- |
| Source context | The function or method name stored with a log message. | `Service::start` |
| Function signature | Compiler text describing a function. | `void Service::start(int)` |
| Qualified name | A name that includes its class or namespace. | `Service::start` |
| Identifier | One simple C++ name. | `T`, `User`, `value_type` |
| Template placeholder | A name waiting to be replaced by a real type. | `T` in `Repository<T>` |
| Template binding | A placeholder and the selected type. | `T = User` |
| Parser | Code that reads text and extracts the useful parts. | `normalizeFunctionSignature` |

## The complete flow

Consider this code:

```cpp
void Service::start() {
  LOGGER_LOG(LogLevel::Info, "Starting");
}
```

The context moves through the library like this:

```text
__PRETTY_FUNCTION__ or __FUNCSIG__
                    |
                    v
       normalizeFunctionSignature()
                    |
                    v
            "Service::start"
                    |
                    v
              Logger::log()
                    |
                    v
       [INFO] [Service::start] Starting
```

The parser produces only `Service::start`. It does not create the timestamp,
log level, message, or complete output line.

## Parser helpers

The helpers below are listed in the order in which they are easiest to
understand. Application code must not call them directly.

### `trim(value)`

Removes spaces, tabs, and newlines from the beginning and end of a string. This
keeps the other parser helpers from having to handle extra whitespace around
a signature.

```text
Input:  "  void Service::start()  "
Output: "void Service::start()"
```

### `isIdentifierCharacter(character)`

Returns `true` for a letter, digit, or underscore. The parser uses it to find
where a simple C++ name starts and ends.

```text
'A' -> true
'7' -> true
'_' -> true
'-' -> false
```

Only the small ASCII subset needed for compiler template placeholders is
handled here.

### `isIdentifier(value)`

Checks whether the complete string is one simple name. This prevents the
parser from treating a complex type or qualified name as a template
placeholder.

```text
"T"           -> true
"value_1"     -> true
"std::string" -> false
```

For example, in `T = User`, the parser checks that `T` is an identifier before
replacing it with `User`.

### `splitTemplateBindings(bindings)`

Separates multiple template bindings reported by GCC or Clang:

```text
Input:
U = std::pair<int, double>, T = User

Output:
1. U = std::pair<int, double>
2. T = User
```

It cannot split at every comma because a type can contain commas of its own. A
simple split would break `std::pair<int, double>` into the wrong pieces:

```text
U = std::pair<int
double>
T = User
```

To avoid that, it keeps track of matching `< >`, `( )`, `[ ]`, and
`{ }` characters. It splits at a comma or semicolon only when that separator
is not inside one of those pairs.

### `TemplateBinding`

Stores a template placeholder and the real type selected for it:

```cpp
TemplateBinding{"T", "User"}
```

This means:

```text
T = User
```

Keeping the two values together, with the names `name` and `value`, makes the
code clearer than passing unrelated strings between parser functions.

### `removeTemplateSuffix(signature)`

GCC and Clang can append resolved template types to a function signature. This
helper removes that suffix and returns each resolved type as a
`TemplateBinding`:

```text
Input signature:
void process(T) [with T = int]

Signature after the call:
void process(T)

Returned binding:
T = int
```

The declaration and its resolved types are easier to process separately. The
helper changes `signature` directly, which is why the parameter is
`std::string &` instead of `const std::string &`.

### `replaceIdentifier(text, identifier, replacement)`

Replaces an identifier only when it appears as a complete name:

```text
Text:        Repository<T>::save
Identifier:  T
Replacement: User
Result:      Repository<User>::save
```

A normal text replacement could also replace the `T` in `Type` and produce the
incorrect name `Userype`.

### `findNameStart(prefix, end)`

Searches backward to find where the qualified function name starts:

```text
Input:  public: bool __cdecl Value::operator bool
Result:                       Value::operator bool
```

Taking everything after the final space would return only `bool`. Searching
backward lets the parser discard the return type and calling convention while
keeping the meaningful space in `operator bool`.

### `consumeClassTemplateArgument(classQualifier, argument)`

Checks whether a resolved template type is already present in the class name:

```text
Class:    Repository<User>
Argument: User
Result:   true
```

Clang can include both `Repository<User>` and `T = User` in the same signature.
Without this check, the parser could add `User` a second time and produce:

```text
Repository<User>::save<User>
```

### `extractFunctionName(rawSignature, fallbackFunction)`

Removes return types, parameters, calling conventions, and other declaration
details, leaving the qualified function name:

```text
void Service::start(int)                        -> Service::start
public: void __cdecl Service::start(void)       -> Service::start
bool Predicate::operator()(int)                 -> Predicate::operator()
public: void __cdecl Functor::operator ()(void) -> Functor::operator()
```

It also changes compiler-generated lambda names to `<lambda>`. If it cannot
understand the detailed signature, it returns `fallbackFunction`, which
normally comes from `__func__`. A short, predictable name is easier to read in
a log than the complete compiler declaration.

### `normalizeFunctionSignature(rawSignature, fallbackFunction)`

Runs the complete parser and returns the context used by `LOGGER_LOG`.

It performs these steps:

1. Trim the compiler text.
2. Remove and parse template bindings.
3. Extract the qualified function name.
4. Replace placeholders used in the class name.
5. Add remaining types as function-template arguments.
6. Use the `__func__` fallback when parsing cannot produce a name.

Complete example:

```text
Input:
void Repository<T>::convert(U)
    [with U = std::pair<int, double>; T = User]

After removing bindings:
void Repository<T>::convert(U)

After extracting the name:
Repository<T>::convert

After resolving the class type T:
Repository<User>::convert

After adding the function type U:
Repository<User>::convert<std::pair<int, double>>
```

The compiler references at the end of this guide define the input strings. The
project tests define the exact normalized output.

## Special cases

### Operators

MSVC can put a space before a symbolic operator:

```text
Functor::operator ()
```

GCC and Clang normally produce:

```text
Functor::operator()
```

The parser removes MSVC's extra space so all supported compilers return
`Functor::operator()`. It does not remove the required space from a conversion
operator such as `Value::operator bool`.

### Lambdas

A lambda variable name is not present in the compiler signature, so the parser
cannot discover it. Automatic context is `<lambda>`. The application must use
`LOGGER_LOG_WITH_CONTEXT` when it needs a meaningful lambda name.

### Unsupported compilers

Do not add complicated parsing for an unsupported compiler or an unstable
generated name only to make one context slightly nicer. Use the `__func__`
fallback, or let the application provide a stable context.

## How to fix a parser problem

Start with a test instead of changing the parser immediately:

1. Copy the exact signature produced by the compiler.
2. Add it to `SignatureParserHandlesSupportedCompilerFormats` in
   `tests/test_logger.cpp`.
3. Add the exact short context the logger should return.
4. Make the smallest parser change that passes the new test.
5. Run related cases, such as conversion operators or nested templates.
6. Run the complete test suite.
7. Let CI check GCC, Clang, and MSVC.

For example, the MSVC call-operator regression is:

```cpp
EXPECT_EQ(
    normalizeFunctionSignature(
        "public: void __cdecl Functor::operator ()(void)",
        "operator()"),
    "Functor::operator()");
```

This test tells the next developer three things: which compiler caused the
problem, the exact input, and the expected output.

## Suggested reading order

When reading `logger.h` for the first time:

1. Start with `LOGGER_LOG` and `LOGGER_LOG_WITH_CONTEXT` at the bottom.
2. Read `normalizeFunctionSignature()` to see the main steps.
3. Read `removeTemplateSuffix()` and `extractFunctionName()`.
4. Read a smaller helper only when you need its particular edge case.
5. Read `Logger::log()` to see where the context enters `LogEntry`.

Starting with the public macros makes the smaller helpers easier to understand.

## Checks to run

```sh
make test
make run-examples
make format
```

Before submitting a parser change, check that:

- Free functions and methods keep their current context.
- Constructors and destructors still work.
- Symbolic operators have the same spelling across supported compilers.
- Conversion operators keep their meaningful space.
- Commas inside nested templates do not split bindings.
- Class-template types are not repeated as function-template types.
- Lambdas remain `<lambda>` unless the user supplies a context.
- Invalid or unknown input falls back to `__func__`.
- GCC, Clang, and MSVC CI jobs pass.

Every parser helper is tested directly in `tests/test_logger.cpp`.

## References

- [C++ draft: function definitions and `__func__`](https://eel.is/c++draft/dcl.fct.def.general)
- [C++ draft: identifiers](https://eel.is/c++draft/lex.name)
- [C++ draft: templates](https://eel.is/c++draft/temp)
- [C++ draft: overloaded operators](https://eel.is/c++draft/over.oper)
- [C++ draft: lambda expressions](https://eel.is/c++draft/expr.prim.lambda)
- [GCC: Function Names as Strings](https://gcc.gnu.org/onlinedocs/gcc/Function-Names.html)
- [Microsoft: predefined macros](https://learn.microsoft.com/en-us/cpp/preprocessor/predefined-macros?view=msvc-170)
- [Clang: source-location builtins](https://clang.llvm.org/docs/LanguageExtensions.html#source-location-builtins)
