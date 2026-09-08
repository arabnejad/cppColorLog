# Using automatic source context

Source context is the function or method name shown beside a log message:

```text
[INFO] [Service::start] Service started
        ^^^^^^^^^^^^^^
        source context
```

cppColorLogger can detect this name for you. In normal application code, you
do not need to understand or call the internal parser.

## The three rules to remember

1. Use `LOGGER_LOG(level, message)` for normal functions, methods,
   constructors, destructors, operators, and templates.
2. Use `LOGGER_LOG_WITH_CONTEXT(level, context, message)` when you want to
   choose the context yourself.
3. Do not call anything in `cppcolorlogger_detail`. Those functions are
   implementation details used by the logging macros.

## Quick start

```cpp
#include "cppColorLogger/logger.h"

void refreshCache() {
  LOGGER_LOG(LogLevel::Info, "Cache refreshed");
}

class Service {
public:
  void start() {
    LOGGER_LOG(LogLevel::Info, "Service started");
  }
};

int main() {
  LOGGER.setLogLevel(LogLevel::Info);

  refreshCache();

  Service service;
  service.start();
}
```

Output:

```text
[2026-09-05 00:59:52] [INFO] [refreshCache] Cache refreshed
[2026-09-05 00:59:52] [INFO] [Service::start] Service started
```

The timestamp will be different when you run the program.

## Runnable example

The project contains a complete example:

[`examples/04_source_context/source_context.cpp`](../examples/04_source_context/source_context.cpp)

Build and run it from the project root:

```sh
make examples
./build/source_context
```

This output was produced with GCC 13.3. A terminal may show INFO messages in
green; ANSI color codes are not displayed below.

```text
Source context: automatic and explicit names.
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

The example and automated tests verify these contexts:

| Where the log is written | Macro | Context in the output |
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
| Lambda, automatic name | `LOGGER_LOG` | `<lambda>` |
| Lambda, chosen name | `LOGGER_LOG_WITH_CONTEXT` | `refreshCache` |
| Any chosen name | `LOGGER_LOG_WITH_CONTEXT` | For example, `UserRepository::save` |

## Templates

You do not need to configure anything before logging from a template. Use
`LOGGER_LOG` in the same way as you would in an ordinary function.

### Function template

```cpp
#include "cppColorLogger/logger.h"

template <typename T>
void process(const T &) {
  LOGGER_LOG(LogLevel::Info, "Function template");
}

int main() {
  LOGGER.setLogLevel(LogLevel::Info);
  process(42);
}
```

The call uses `int`, so the context contains `process<int>`:

```text
[2026-09-05 00:59:52] [INFO] [process<int>] Function template
```

### Class template

```cpp
#include "cppColorLogger/logger.h"

template <typename T>
class Repository {
public:
  void save(const T &) {
    LOGGER_LOG(LogLevel::Info, "Class template");
  }
};

struct User {};

int main() {
  LOGGER.setLogLevel(LogLevel::Info);
  Repository<User> users;
  users.save(User());
}
```

Output:

```text
[2026-09-05 00:59:52] [INFO] [Repository<User>::save] Class template
```

### Class and member templates together

```cpp
#include <utility>

#include "cppColorLogger/logger.h"

template <typename T>
class Repository {
public:
  template <typename U>
  void convert(const U &) {
    LOGGER_LOG(LogLevel::Info, "Class and member templates");
  }
};

struct User {};

int main() {
  LOGGER.setLogLevel(LogLevel::Info);
  Repository<User> repository;
  repository.convert(std::make_pair(1, 2.0));
}
```

Output:

```text
[2026-09-05 00:59:52] [INFO] [Repository<User>::convert<std::pair<int, double>>] Class and member templates
```

Template names can become long, and their exact spelling can differ between
compilers. If that is a problem, choose a shorter context with
`LOGGER_LOG_WITH_CONTEXT`.

## Lambdas

A lambda does not have a user-written function name. The name of the variable
holding the lambda is not included in the compiler's function signature.

For example:

```cpp
const auto refreshCache = [] {
  LOGGER_LOG(LogLevel::Info, "Cache refreshed");
};
```

The logger can identify this only as a lambda:

```text
[2026-09-05 00:59:52] [INFO] [<lambda>] Cache refreshed
```

If `refreshCache` is important, pass it yourself:

```cpp
const auto refreshCache = [] {
  LOGGER_LOG_WITH_CONTEXT(
      LogLevel::Info,
      "refreshCache",
      "Cache refreshed");
};
```

Output:

```text
[2026-09-05 00:59:52] [INFO] [refreshCache] Cache refreshed
```

## Choosing a stable or private context

You may not want a generated template type or internal class name to appear in
a log. You may also need exactly the same context on every compiler. In both
cases, use `LOGGER_LOG_WITH_CONTEXT`:

```cpp
void saveUser() {
  LOGGER_LOG_WITH_CONTEXT(
      LogLevel::Info,
      "UserRepository::save",
      "Saving user");
}
```

The logger uses your text exactly as written:

```text
[2026-09-05 00:59:52] [INFO] [UserRepository::save] Saving user
```

## Other compilers

Automatic class-qualified names are supported for GCC, Clang, and MSVC. These
compilers describe functions differently, so the logger normalizes their
signatures to a common readable form.

On another compiler, logging still works. The logger uses the standard
`__func__` value as a fallback, which may contain only the function name:

```text
[2026-09-05 00:59:52] [INFO] [start] Service started
```

If the class-qualified name matters, provide it yourself:

```cpp
LOGGER_LOG_WITH_CONTEXT(
    LogLevel::Info,
    "Service::start",
    "Service started");
```

Output:

```text
[2026-09-05 00:59:52] [INFO] [Service::start] Service started
```

## Understanding the implementation

Application code should use the public macros and should not call the parser
helpers directly.

If you want to understand or change the implementation, read
[`contribution.md`](../contribution.md). It explains:

- Why the parser is needed.
- What every helper does and why it exists.
- How templates, operators, lambdas, and fallback names are handled.
- Which behavior comes from compiler documentation.
- How to add and test a new compiler signature safely.
