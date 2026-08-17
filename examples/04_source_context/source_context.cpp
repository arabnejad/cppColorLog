#include <cstdio>
#include <utility>

#include "cppColorLogger/logger.h"

void refreshCache() {
  LOGGER_LOG(LogLevel::Info, "Free function");
}

template <typename T> void process(const T &) {
  LOGGER_LOG(LogLevel::Info, "Function template");
}

class Service {
public:
  Service() {
    LOGGER_LOG(LogLevel::Info, "Constructor");
  }

  ~Service() {
    LOGGER_LOG(LogLevel::Info, "Destructor");
  }

  void start() {
    LOGGER_LOG(LogLevel::Info, "Member function");
  }

  static void reportStatus() {
    LOGGER_LOG(LogLevel::Info, "Static member function");
  }

  bool operator()(int) const {
    LOGGER_LOG(LogLevel::Info, "Function-call operator");
    return true;
  }
};

struct User {};

template <typename T> class Repository {
public:
  void save(const T &) {
    LOGGER_LOG(LogLevel::Info, "Class template");
  }

  template <typename U> void convert(const U &) {
    LOGGER_LOG(LogLevel::Info, "Class and member templates");
  }
};

int main() {
  std::printf("Source context: automatic and explicit names.\n");
  LOGGER.setLogLevel(LogLevel::Info);

  refreshCache();

  Service service;
  service.start();
  Service::reportStatus();
  service(1);

  process(42);

  Repository<User> repository;
  const User       user = {};
  repository.save(user);
  repository.convert(std::make_pair(1, 2.0));

  const auto automaticLambda = []() { LOGGER_LOG(LogLevel::Info, "Cache refreshed"); };
  automaticLambda();

  const auto namedLambda = []() { LOGGER_LOG_WITH_CONTEXT(LogLevel::Info, "refreshCache", "Cache refreshed"); };
  namedLambda();

  LOGGER_LOG_WITH_CONTEXT(LogLevel::Info, "UserRepository::save", "Saving user");
  return 0;
}
