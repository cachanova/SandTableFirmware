#include <Logger.hpp>
#include <cassert>
#include <iostream>
#include <string>

static size_t count(const std::string& text, const std::string& token) {
    size_t result = 0;
    for (size_t at = 0; (at = text.find(token, at)) != std::string::npos; at += token.size())
        ++result;
    return result;
}

int main() {
    static_assert(sizeof(RuntimeLog) < 7000, "Keep the short diagnostic ring bounded");
    auto& log = RuntimeLog::instance();
    for (unsigned i = 1; i <= 70; ++i) log.printf("line-%u\n", i);
    assert(count(Serial.text, "line-") == 70); // Full serial output is unchanged.
    assert(log.nextId() == 71 && log.droppedCount() == 38);
    Print json;
    log.writeJson(json);
    assert(count(json.text, "\"id\":") == 32);
    assert(json.text.find("\"id\":39,") != std::string::npos);
    assert(json.text.find("\"id\":38,") == std::string::npos);
    Print recent;
    log.writeText(recent, 68);
    assert(count(recent.text, "line-") == 2);
    Print none;
    log.writeJson(none, 0, 0);
    assert(none.text.find("\"entries\":[]") != std::string::npos);
    log.clear();
    assert(log.nextId() == 71 && log.droppedCount() == 0);
    log.printf("after-clear\n");
    Print after;
    log.writeJson(after);
    assert(count(after.text, "\"id\":") == 1);
    assert(after.text.find("\"id\":71,") != std::string::npos);
    std::cout << "PASS: 32-entry overwrite, dropped count, cursor/limit/clear, and full serial retention\n";
}
