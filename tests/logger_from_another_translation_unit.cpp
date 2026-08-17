// This file is intentionally compiled separately from test_logger.cpp.
//
// A normal C++ application usually contains many source files, called
// translation units. Because cppColorLogger is header-only, definitions and
// thread-local state from logger.h must behave as one shared implementation
// after those source files are linked together. The functions below are called
// by test_logger.cpp to verify that behavior.

#include "cppColorLogger/logger.h"

#include <string>

bool colorsAreAvailableFromAnotherTranslationUnit() {
  return !std::string(Color::Red).empty() && !std::string(Color::Green).empty();
}

bool infoIsEnabledFromAnotherTranslationUnit() {
  return LOGGER.isEnabled(LogLevel::Info);
}
