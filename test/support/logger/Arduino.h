#pragma once
#include <Print.h>
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mutex) do { (void)(mutex); } while (0)
#define portEXIT_CRITICAL(mutex) do { (void)(mutex); } while (0)
inline Print Serial;
inline uint32_t millis() { static uint32_t value = 0; return ++value; }
