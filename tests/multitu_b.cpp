#include "cppColorLogger/logger.h"

#include <string>

int multiTranslationUnitB() {
  return std::string(Color::GREEN).empty() ? 0 : 1;
}
