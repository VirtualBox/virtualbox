#pragma once

#include <dxgi1_2.h>

#define IMPLEMENT_ME                                                           \
  Logger::err(                                                                 \
      str::format(__FILE__, ":", __FUNCTION__, " is not implemented."));       \
  while (1) {                                                                  \
  }
