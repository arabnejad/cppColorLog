#include <cstdio>
#include <utility>

#include "cppColorLogger/logger.h"

void refreshCache() {
  LOGGER_LOG(LOGLEVEL::INFO, "Free function");
}

template <typename T> void process(const T &) {
  LOGGER_LOG(LOGLEVEL::INFO, "Function template");
}

class Service {
public:
  Service() {
    LOGGER_LOG(LOGLEVEL::INFO, "Constructor");
  }

  ~Service() {
    LOGGER_LOG(LOGLEVEL::INFO, "Destructor");
  }

  void start() {
    LOGGER_LOG(LOGLEVEL::INFO, "Member function");
  }

  static void reportStatus() {
    LOGGER_LOG(LOGLEVEL::INFO, "Static member function");
  }

  bool operator()(int) const {
    LOGGER_LOG(LOGLEVEL::INFO, "Function-call operator");
    return true;
  }
};

struct User {};

template <typename T> class Repository {
public:
  void save(const T &) {
    LOGGER_LOG(LOGLEVEL::INFO, "Class template");
  }

  template <typename U> void convert(const U &) {
    LOGGER_LOG(LOGLEVEL::INFO, "Class and member templates");
  }
};

int main() {
  std::printf("Example 15: Automatic and explicit source contexts.\n");
  LOGGER.setLogLevel(LOGLEVEL::INFO);

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

  const auto automaticLambda = []() { LOGGER_LOG(LOGLEVEL::INFO, "Cache refreshed"); };
  automaticLambda();

  const auto namedLambda = []() { LOGGER_LOG_WITH_CONTEXT(LOGLEVEL::INFO, "refreshCache", "Cache refreshed"); };
  namedLambda();

  LOGGER_LOG_WITH_CONTEXT(LOGLEVEL::INFO, "UserRepository::save", "Saving user");

  return 0;
}
