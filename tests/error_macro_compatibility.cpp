// Some platform headers and applications define a macro named ERROR. The
// logger must not remove that macro from the including source file.
#if defined(_WIN32)
#include <windows.h>
#else
#define ERROR 123
#endif

#include "cppColorLogger/logger.h"

bool errorMacroRemainsDefinedAfterIncludingLogger() {
#if defined(ERROR)
  return true;
#else
  return false;
#endif
}
