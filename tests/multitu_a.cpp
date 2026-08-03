#include "cppColorLogger/logger.h"

#include <string>

int multiTranslationUnitA() {
  return std::string(Color::RED).empty() ? 0 : 1;
}
