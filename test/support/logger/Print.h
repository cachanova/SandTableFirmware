#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <string>
class Print {
public:
    std::string text;
    void print(const char* value) { text += value; }
    void print(char value) { text += value; }
    void print(uint32_t value) { text += std::to_string(value); }
    void printf(const char* format, ...) {
        char buffer[512];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        text += buffer;
    }
};
