# Understanding automatic source context

This guide is for developers who want to use cppColorLogger and then understand
how its automatic function names work internally.

## If you only want to use the logger

You do not need to call any function in `cppcolorlog::detail`. For normal code,
use `LOGGER_LOG`:

```cpp
#include "cppColorLogger/logger.h"

void refreshCache() {
  LOGGER_LOG(LogLevel::INFO, "Cache refreshed");
}

class Service {
public:
  void start() {
    LOGGER_LOG(LogLevel::INFO, "Service started");
  }
};

int main() {
  LOGGER.setLogLevel(LogLevel::INFO);
  refreshCache();

  Service service;
  service.start();
}
```

The logger automatically adds a **source context** to each message:

```text
[2026-09-05 00:59:52] [INFO] [refreshCache] Cache refreshed
[2026-09-05 00:59:52] [INFO] [Service::start] Service started
```

This exact program is also available as the separately compiled
[`examples/16_source_context_quick_start/16_source_context_quick_start.cpp`](../examples/16_source_context_quick_start/16_source_context_quick_start.cpp).

Follow these three rules:

1. Use `LOGGER_LOG(level, message)` in normal functions, class methods,
   constructors, destructors, operators, and templates.
2. Use `LOGGER_LOG_WITH_CONTEXT(level, context, message)` when a lambda needs a
   useful name or when the context must be exactly the same on every compiler.
3. Do not use the functions in `cppcolorlog::detail` from application code.
   They are private implementation helpers used by the logging macros.

If that is all you need, you can stop reading here. The remaining sections
explain the implementation in `logger.h`.

## Runnable examples for all supported cases

The project includes
[`examples/15_source_context/15_source_context.cpp`](../examples/15_source_context/15_source_context.cpp).
It uses the real public macros rather than calling the parser directly. Build
and run it from the project root:

```sh
make examples
./build/15_source_context
```

The following output was produced by GCC 13.3. The timestamp changes on every
run. A terminal may display these INFO lines in green; invisible ANSI color
codes are not shown below.

```text
Example 15: Automatic and explicit source contexts.
[2026-09-05 01:10:14] [INFO] [refreshCache] Free function
[2026-09-05 01:10:14] [INFO] [Service::Service] Constructor
[2026-09-05 01:10:14] [INFO] [Service::start] Member function
[2026-09-05 01:10:14] [INFO] [Service::reportStatus] Static member function
[2026-09-05 01:10:14] [INFO] [Service::operator()] Function-call operator
[2026-09-05 01:10:14] [INFO] [process<int>] Function template
[2026-09-05 01:10:14] [INFO] [Repository<User>::save] Class template
[2026-09-05 01:10:14] [INFO] [Repository<User>::convert<std::pair<int, double>>] Class and member templates
[2026-09-05 01:10:14] [INFO] [<lambda>] Cache refreshed
[2026-09-05 01:10:14] [INFO] [refreshCache] Cache refreshed
[2026-09-05 01:10:14] [INFO] [UserRepository::save] Saving user
[2026-09-05 01:10:14] [INFO] [Service::~Service] Destructor
```

This table connects each C++ situation to the source context inside the third
pair of square brackets:

| C++ situation | Macro to use | Verified context |
| --- | --- | --- |
| Free function | `LOGGER_LOG` | `refreshCache` |
| Constructor | `LOGGER_LOG` | `Service::Service` |
| Destructor | `LOGGER_LOG` | `Service::~Service` |
| Member function | `LOGGER_LOG` | `Service::start` |
| Static member function | `LOGGER_LOG` | `Service::reportStatus` |
| Operator | `LOGGER_LOG` | `Service::operator()` |
| Function template | `LOGGER_LOG` | `process<int>` |
| Class template | `LOGGER_LOG` | `Repository<User>::save` |
| Class and member templates | `LOGGER_LOG` | `Repository<User>::convert<std::pair<int, double>>` |
| Lambda with automatic context | `LOGGER_LOG` | `<lambda>` |
| Lambda with a chosen name | `LOGGER_LOG_WITH_CONTEXT` | `refreshCache` |
| Stable/private explicit name | `LOGGER_LOG_WITH_CONTEXT` | `UserRepository::save` |

## Words used in the parser

The parser comments use a few C++ terms:

| Term | Plain-English meaning | Example |
| --- | --- | --- |
| Source context | The function or method name printed with a log message. | `Service::start` |
| Function signature | A compiler-generated description containing the return type, function name, and arguments. | `void Service::start(int)` |
| Qualified name | A name that includes its class or namespace. | `Service::start` |
| Identifier | One simple C++ name made from letters, digits, and `_`. | `T`, `User`, `value_type` |
| Template placeholder | A name waiting to be replaced by a real type. | `T` in `Repository<T>` |
| Template binding | A placeholder together with its resolved type. | `T = User` |
| Parser | Code that reads a string and extracts the useful pieces. | `normalizeFunctionSignature` |

## What happens when `LOGGER_LOG` is called

Consider this method:

```cpp
void Service::start() {
  LOGGER_LOG(LogLevel::INFO, "Service started");
}
```

The call travels through the header in this order:

```text
LOGGER_LOG
    |
    | passes the detailed compiler signature and __func__ fallback
    v
detail::normalizeFunctionSignature
    |
    | removes information that is not useful in the log
    v
"Service::start"
    |
    v
Logger::log -> final log entry
```

On GCC, the macro passes these two values to the parser:

```text
detailed signature = "void Service::start()"
fallback function  = "start"
```

The detailed signature contains the class name, so the parser uses it when it
can. The fallback comes from standard `__func__` and supplies the simpler name
`start` if the detailed signature cannot be understood. No temporary source
object is needed; the parser's returned string is passed directly to
`Logger::log()`.

The final logger output for this call is:

```text
[2026-09-05 00:59:52] [INFO] [Service::start] Service started
```

## The parser, one step at a time

`normalizeFunctionSignature` is the main parser function. The other functions
in `detail` help it complete four steps.

### Step 1: clean the input

`trim` removes whitespace only from the beginning and end:

```text
Input to trim:    "  void Service::start()  "
Returned string:  "void Service::start()"
```

It does not change spaces in the middle of the signature.

`trim` is only an internal preparation step. It does not write a log by itself.
After the complete parser runs, the corresponding logger output is still:

```text
[2026-09-05 00:59:52] [INFO] [Service::start] Member function
```

### Step 2: separate template information

For an ordinary function, there may be no extra work:

```text
void Service::start()
```

For a template, GCC or Clang may add resolved types at the end:

```text
void process(const T&) [with T = int]
                       ^^^^^^^^^^^^^^
                       template suffix
```

`removeTemplateSuffix` changes this into two separate pieces:

```text
signature after removal = "void process(const T&)"
bindings                = [{ name: "T", value: "int" }]
```

The function changes its `signature` argument directly. This is why that
argument is passed as `std::string &` instead of `const std::string &`.

When several bindings exist, `splitTemplateBindings` separates them. Splitting
on every comma would be incorrect because a type can contain its own comma:

```text
Input:  "U = std::pair<int, int>, T = User"

Correct output:
  1. "U = std::pair<int, int>"
  2. "T = User"

Incorrect output:
  1. "U = std::pair<int"
  2. "int>"
  3. "T = User"
```

The function avoids the incorrect result by counting open `< >`, `( )`,
`[ ]`, and `{ }` pairs. A comma separates bindings only when all counts are
zero, meaning the comma is not nested inside a type or expression.

`isIdentifier` checks that the left side of a binding is a safe, simple name.
For example, `T` is accepted but `std::T` is rejected.

These helpers do not log separately. For the `process<int>` example, their work
contributes to this final logger line:

```text
[2026-09-05 00:59:52] [INFO] [process<int>] Function template
```

### Step 3: extract the callable name

`extractFunctionName` removes the return type and argument types:

```text
Input to parser: "void Service::start(int)"
Parser result:   "Service::start"
```

It first finds the final `)`, then searches backward for the matching `(`.
Everything after that opening parenthesis is the argument list and can be
removed. From the remaining text, the last top-level name is the callable.

Operators need special handling because their name can contain punctuation or
spaces:

```text
"bool Predicate::operator()(int)"       -> "Predicate::operator()"
"public: bool Value::operator bool()"   -> "Value::operator bool"
```

`findNameStart` supports this case. It searches backward to find where the
class-and-operator name begins without stopping at a space inside an operator
or a nested template.

Lambdas also need special handling. Their compiler-generated names differ, so
the parser converts them all to the stable text `<lambda>`.

The runnable example verifies ordinary methods and operators with these logger
lines:

```text
[2026-09-05 00:59:52] [INFO] [Service::start] Member function
[2026-09-05 00:59:52] [INFO] [Service::operator()] Function-call operator
```

### Step 4: put resolved template types into the name

For a free function template:

```cpp
#include "cppColorLogger/logger.h"

template <typename T>
void process(const T &) {
  LOGGER_LOG(LogLevel::INFO, "Function template");
}

int main() {
  LOGGER.setLogLevel(LogLevel::INFO);
  process(42); // T is int
}
```

The compiler signature and parser result can look like this:

```text
Compiler: void process(const T&) [with T = int]
Log name: process<int>
```

The callable name does not contain `T`, so the parser adds the resolved type as
a function-template argument: `<int>`.

The complete logger output is:

```text
[2026-09-05 00:59:52] [INFO] [process<int>] Function template
```

A class template is slightly different:

```cpp
#include "cppColorLogger/logger.h"

template <typename T>
class Repository {
public:
  void save(const T &) {
    LOGGER_LOG(LogLevel::INFO, "Class template");
  }
};

struct User {};

int main() {
  LOGGER.setLogLevel(LogLevel::INFO);
  Repository<User> users;
  users.save(User());
}
```

GCC may provide:

```text
void Repository<T>::save(const T&) [with T = User]
```

`replaceIdentifier` replaces the complete `T` token in `Repository<T>`:

```text
Repository<T>::save  ->  Repository<User>::save
```

It replaces complete names only. Replacing `T` must not accidentally change a
longer name such as `Type`.

The verified logger output for `Repository<User>::save` is:

```text
[2026-09-05 00:59:52] [INFO] [Repository<User>::save] Class template
```

Clang sometimes already places the resolved type in the class name while also
reporting the binding:

```text
Repository<User>::save() [T = User]
```

The displayed class name already contains `User`.
`consumeClassTemplateArgument` marks that binding as used so the parser does
not create the incorrect result `Repository<User>::save<User>`.

### Complete template example

This example has both a class template and a method template:

```cpp
#include <utility>

#include "cppColorLogger/logger.h"

template <typename T>
class Repository {
public:
  template <typename U>
  void convert(const U &) {
    LOGGER_LOG(LogLevel::INFO, "Class and member templates");
  }
};

struct User {};

int main() {
  LOGGER.setLogLevel(LogLevel::INFO);
  Repository<User> repository;
  repository.convert(std::make_pair(1, 2.0));
}
```

For that call, GCC may provide:

```text
void Repository<T>::convert(const U&)
    [with U = std::pair<int, double>; T = User]
```

The parser transforms it like this:

```text
1. Remove suffix:       void Repository<T>::convert(const U&)
2. Extract name:        Repository<T>::convert
3. Replace class T:     Repository<User>::convert
4. Append method U:     Repository<User>::convert<std::pair<int, double>>
```

The final name clearly shows both the class type and the method type.
It also verifies that the comma inside `std::pair<int, double>` does not split
the compiler's binding list incorrectly. The valid logger output is:

```text
[2026-09-05 00:59:52] [INFO] [Repository<User>::convert<std::pair<int, double>>] Class and member templates
```

## What each helper does

Read this table from top to bottom when following the code. These are internal
results; only `normalizeFunctionSignature` supplies a context to the logger.
Every helper example in the table is now called directly by a unit test in
`tests/test_logger.cpp`.

| Helper | Example input | Result |
| --- | --- | --- |
| `trim` | `"  void run()  "` | `"void run()"` |
| `isIdentifierCharacter` | `'A'`, `'7'`, `'-'` | `true`, `true`, `false` |
| `isIdentifier` | `"value_1"`, `"std::string"` | `true`, `false` |
| `splitTemplateBindings` | `"U = std::pair<int, int>, T = User"` | Two items: `"U = std::pair<int, int>"` and `"T = User"` |
| `TemplateBinding` | `{ "T", "User" }` | Stores `name = "T"` and `value = "User"` |
| `removeTemplateSuffix` | `"void process(T) [with T = int]"` | Changes the string to `"void process(T)"`; returns `T = int` |
| `replaceIdentifier` | Text `"Repository<T>::save"`, replace `T` with `User` | Changes text to `"Repository<User>::save"` |
| `findNameStart` | `"public: bool Value::operator bool"` | Returns the position of `Value::operator bool` |
| `consumeClassTemplateArgument` | Class `"Repository<User>"`, argument `"User"` | Returns `true`; marks the match only in its working copy |
| `extractFunctionName` | `"void Service::start(int)"` | `"Service::start"` |
| `normalizeFunctionSignature` | `"void process(T) [with T = int]"` | `"process<int>"` |

For the final row, the returned context appears in a complete log line like
this:

```text
[2026-09-05 00:59:52] [INFO] [process<int>] Function template
```

## When to provide context yourself

### Lambdas

A lambda has no user-written function name. The variable holding a lambda is
not part of its function signature:

```cpp
#include "cppColorLogger/logger.h"

int main() {
  LOGGER.setLogLevel(LogLevel::INFO);
  const auto refreshCache = []() {
    LOGGER_LOG(LogLevel::INFO, "Cache refreshed");
  };
  refreshCache();
}
```

The context will be `<lambda>`, not `refreshCache`. If the name matters, provide
it explicitly:

```cpp
#include "cppColorLogger/logger.h"

int main() {
  LOGGER.setLogLevel(LogLevel::INFO);
  const auto refreshCache = []() {
    LOGGER_LOG_WITH_CONTEXT(
        LogLevel::INFO,
        "refreshCache",
        "Cache refreshed");
  };
  refreshCache();
}
```

Both choices produce valid log lines:

```text
[2026-09-05 00:59:52] [INFO] [<lambda>] Cache refreshed
[2026-09-05 00:59:52] [INFO] [refreshCache] Cache refreshed
```

### Stable or private names

Resolved template types can be long, compiler-dependent, or sensitive. Use an
explicit context when you do not want them in the log:

```cpp
#include "cppColorLogger/logger.h"

void saveUser() {
  LOGGER_LOG_WITH_CONTEXT(
      LogLevel::INFO,
      "UserRepository::save",
      "Saving user");
}

int main() {
  LOGGER.setLogLevel(LogLevel::INFO);
  saveUser();
}
```

The supplied string is used exactly as the context:

```text
[2026-09-05 00:59:52] [INFO] [UserRepository::save] Saving user
```

### Other compilers

Automatic class detection understands GCC, Clang, and MSVC signatures. On
another compiler, logging still works by using `__func__`, but a context may
contain only `start` instead of `Service::start`. Use
`LOGGER_LOG_WITH_CONTEXT` when the class-qualified name is important.

For example, the portable fallback and explicit versions would look like:

```text
[2026-09-05 00:59:52] [INFO] [start] Service started
[2026-09-05 00:59:52] [INFO] [Service::start] Service started
```

The first line is the possible `__func__` fallback. The second line uses
`LOGGER_LOG_WITH_CONTEXT` and is therefore stable on every compiler.

## Suggested order for reading `logger.h`

If this is your first time reading the header, use this order:

1. Read the `LOGGER_LOG` and `LOGGER_LOG_WITH_CONTEXT` macros at the bottom.
2. Read `normalizeFunctionSignature` to see the four high-level steps.
3. Read `removeTemplateSuffix` and `extractFunctionName`.
4. Read the smaller helpers only when you need to understand a specific edge
   case.
5. Read `Logger::log()` to see how the returned context enters the log message.

This order starts with the public API and moves inward. Reading the small parser
helpers from top to bottom without this context can make their purpose harder to
see.

## Changing the parser

Compiler signature formats are extensions rather than a C++ standard format.
When adding support or fixing an edge case:

1. Copy the exact compiler signature into a test in `tests/test_logger.cpp`.
2. Add the expected short context beside it.
3. Keep new parsing code inside `cppcolorlog::detail`.
4. Test ordinary methods, templates, nested template types, constructors,
   destructors, operators, and lambdas when relevant.
5. Run `make format` and `make test`.
6. When possible, compile with both GCC and Clang because their strings differ.

Application code should depend only on the logging macros and public classes,
not on `cppcolorlog::detail`.
