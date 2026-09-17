#pragma once
#include <ArduinoJson.h>

// A nonzero serialization count does not prove that a document or file is
// complete. Never promote a staged settings file after OOM or a short write.
template <typename FileLike>
bool writeCompleteJson(const JsonDocument& doc, FileLike& file, bool pretty = false) {
    if (doc.overflowed()) return false;
    const size_t expected = pretty ? measureJsonPretty(doc) : measureJson(doc);
    const size_t written = pretty ? serializeJsonPretty(doc, file) : serializeJson(doc, file);
    file.flush();
    return written == expected && !file.getWriteError() && file.size() == expected;
}
