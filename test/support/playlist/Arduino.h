#pragma once
#include <string>
#include <cstdlib>
#include <utility>
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
class String {
    std::string value;
public:
    String()=default;
    String(const char* s):value(s?s:""){}
    String(std::string s):value(std::move(s)){}
    size_t length() const{return value.size();}
    const char* c_str() const{return value.c_str();}
    char charAt(size_t i) const{return value.at(i);}
    int indexOf(char c) const{auto p=value.find(c);return p==value.npos?-1:static_cast<int>(p);}
    int indexOf(const char* s) const{auto p=value.find(s);return p==value.npos?-1:static_cast<int>(p);}
    bool endsWith(const char* s) const{std::string suffix(s);return value.size()>=suffix.size() && value.compare(value.size()-suffix.size(),suffix.size(),suffix)==0;}
    void remove(size_t i){value.erase(i);}
    bool concat(const char* s){value+=s;return true;}
    friend String operator+(const String& a,const String& b){return a.value+b.value;}
    friend bool operator==(const String& a,const String& b){return a.value==b.value;}
    friend bool operator!=(const String& a,const String& b){return !(a==b);}
};
inline long random(long maximum){return std::rand()%maximum;}
