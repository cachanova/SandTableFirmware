#pragma once
#include <Arduino.h>
#include <map>
#include <memory>
#include <algorithm>
#include <cstring>
#include <cstdint>
constexpr int FILE_READ=0,FILE_WRITE=1;
struct TestStorage {
    std::map<std::string,std::shared_ptr<std::string>> files;
    size_t writeLimit=65536;
    bool flushFailure=false,failCommit=false;
};
inline TestStorage storage;
class File {
    std::shared_ptr<std::string> data;
    size_t offset=0;
public:
    File()=default;
    explicit File(std::shared_ptr<std::string> d):data(std::move(d)){}
    explicit operator bool() const{return bool(data);}
    size_t size() const{return data?data->size():0;}
    size_t write(uint8_t byte){return write(&byte,1);}
    size_t write(const uint8_t* bytes,size_t length){size_t n=std::min(length,storage.writeLimit>size()?storage.writeLimit-size():0);data->append(reinterpret_cast<const char*>(bytes),n);return n;}
    int read(){return data && offset<data->size()?static_cast<unsigned char>((*data)[offset++]):-1;}
    size_t readBytes(char* bytes,size_t length){size_t n=std::min(length,data->size()-offset);std::memcpy(bytes,data->data()+offset,n);offset+=n;return n;}
    void flush(){}
    int getWriteError()const{return storage.flushFailure;}
    void close(){data.reset();}
};
struct SDClass {
    bool exists(const String& path){return storage.files.count(path.c_str());}
    bool mkdir(const char*){return true;}
    bool remove(const String& path){return storage.files.erase(path.c_str())!=0;}
    bool rename(const String& from,const String& to){
        if(storage.failCommit && from.endsWith(".tmp"))return false;
        auto it=storage.files.find(from.c_str());if(it==storage.files.end())return false;
        storage.files[to.c_str()]=it->second;storage.files.erase(it);return true;
    }
    File open(const String& path,int mode){
        auto it=storage.files.find(path.c_str());
        if(it==storage.files.end()){
            if(mode==FILE_READ)return {};
            it=storage.files.emplace(path.c_str(),std::make_shared<std::string>()).first;
        }
        return File(it->second);
    }
};
inline SDClass SD;
