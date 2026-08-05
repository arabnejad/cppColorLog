#include <cstdio>
#include <utility>

#include "cppColorLogger/logger.h"

void refreshCache() {
  LOGGER_LOG(LogLevel::INFO, "Free function");
}

template <typename T> void process(const T &) {
  LOGGER_LOG(LogLevel::INFO, "Function template");
}

class Service {
public:
  Service() {
    LOGGER_LOG(LogLevel::INFO, "Constructor");
  }

  ~Service() {
    LOGGER_LOG(LogLevel::INFO, "Destructor");
  }

  void start() {
    LOGGER_LOG(LogLevel::INFO, "Member function");
  }

  static void reportStatus() {
    LOGGER_LOG(LogLevel::INFO, "Static member function");
  }

  bool operator()(int) const {
    LOGGER_LOG(LogLevel::INFO, "Function-call operator");
    return true;
  }
};

struct User {};

template <typename T> class Repository {
public:
  void save(const T &) {
    LOGGER_LOG(LogLevel::INFO, "Class template");
  }

  template <typename U> void convert(const U &) {
    LOGGER_LOG(LogLevel::INFO, "Class and member templates");
  }
};

int main() {
  std::printf("Example 15: Automatic and explicit source contexts.\n");
  LOGGER.setLogLevel(LogLevel::INFO);

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

  const auto automaticLambda = []() { LOGGER_LOG(LogLevel::INFO, "Cache refreshed"); };
  automaticLambda();

  const auto namedLambda = []() { LOGGER_LOG_WITH_CONTEXT(LogLevel::INFO, "refreshCache", "Cache refreshed"); };
  namedLambda();

  LOGGER_LOG_WITH_CONTEXT(LogLevel::INFO, "UserRepository::save", "Saving user");

  return 0;
}
