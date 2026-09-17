#include "PatternLineReader.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

struct File {
    std::string data;
    size_t offset=0, reads=0;
    int read(uint8_t* dst, size_t capacity) {
        ++reads;
        size_t count=std::min(capacity,data.size()-offset);
        std::memcpy(dst,data.data()+offset,count);offset+=count;return count;
    }
};
int main() {
    char buffer[7], line[8];size_t length=0,position=0,lineLength=0,yields=0;
    bool eof=false,overflow=false,interrupted=false;
    File file{"0 .1\r\n12345678901234567890\n1 .2"};
    auto read=[&]{return readPatternLine(file,buffer,sizeof(buffer),length,position,eof,line,sizeof(line),lineLength,overflow,[]{return false;},[&]{++yields;},interrupted);};
    assert(read() && std::string(line)=="0 .1" && !overflow);
    assert(read() && overflow && std::string(line)=="1234567");
    assert(read() && std::string(line)=="1 .2" && !overflow);
    assert(!read() && eof && !interrupted);
    assert(yields==file.reads);
    File huge{std::string(1000000,'X')};
    char bulk[4096];length=position=0;eof=false;yields=0;
    assert(!readPatternLine(huge,bulk,sizeof(bulk),length,position,eof,line,sizeof(line),lineLength,overflow,
        [&]{return huge.reads>=2;},[&]{++yields;},interrupted));
    assert(interrupted && overflow && !eof && huge.offset==8192 && yields==2);
    std::cout<<"PASS: CRLF/refill/EOF, oversized lines drained without splitting coordinates, queued replacement interrupts huge line within one refill\n";
}
