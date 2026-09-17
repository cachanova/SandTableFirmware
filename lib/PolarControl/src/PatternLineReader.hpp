#pragma once
#include <cstddef>
#include <cstdint>

template <typename Reader, typename Pending, typename Yield>
bool readPatternLine(Reader& file, char* buffer, size_t bufferCapacity,
                     size_t& bufLen, size_t& bufPos, bool& eof,
                     char* lineBuf, size_t lineCap, size_t& lineLen, bool& overflow,
                     Pending commandPending, Yield yield, bool& interrupted) {
    lineLen = 0;
    overflow = false;
    interrupted = false;
    while (true) {
        if (bufPos >= bufLen) {
            if (eof) {
                if (lineLen > 0) {
                    lineBuf[lineLen] = '\0';
                    return true;
                }
                return false;
            }
            // A malformed multi-megabyte line must not delay STOP/replacement
            // until its newline. The caller services the mailbox on interruption.
            if (commandPending()) {
                interrupted = true;
                return false;
            }
            yield();
            int readBytes = file.read(reinterpret_cast<uint8_t*>(buffer), bufferCapacity);
            if (readBytes <= 0) {
                eof = true;
                if (lineLen > 0) {
                    lineBuf[lineLen] = '\0';
                    return true;
                }
                return false;
            }
            bufLen = static_cast<size_t>(readBytes);
            bufPos = 0;
        }

        char c = buffer[bufPos++];
        if (c == '\n') {
            lineBuf[lineLen] = '\0';
            return true;
        }
        if (c == '\r') {
            continue;
        }
        if (lineLen + 1 < lineCap) {
            lineBuf[lineLen++] = c;
        } else {
            overflow = true;
        }
    }
}

