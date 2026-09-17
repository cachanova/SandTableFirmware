#include "UploadCommit.hpp"
#include <cassert>
#include <map>
#include <set>
#include <string>
#include <iostream>
struct Storage {
    std::map<std::string,std::string> files{{"staged","new"},{"final","original"}};
    std::set<std::string> refuse;
    bool exists(const std::string& p) { return files.count(p); }
    bool remove(const std::string& p) { return files.erase(p); }
    bool rename(const std::string& a,const std::string& b) {
        if(refuse.count(a+">"+b)||!exists(a)||exists(b)) return false;
        files[b]=files[a];files.erase(a);return true;
    }
};
int main() {
    for(const auto& fail: {"", "final>backup", "staged>final", "backup>final"}) {
        Storage fs;
        if(*fail) fs.refuse.insert(fail);
        if(std::string(fail)=="backup>final") fs.refuse.insert("staged>final");
        int status=commitUpload(fs,std::string("staged"),std::string("final"),std::string("backup"));
        if(!*fail) { assert(status==0);assert(fs.files.at("final")=="new");assert(!fs.exists("backup")); }
        else if(std::string(fail)=="backup>final") { assert(status==507);assert(fs.files.at("backup")=="original"); }
        else { assert(status==500);assert(fs.files.at("final")=="original");assert(fs.files.at("staged")=="new"); }
    }
    Storage fs;fs.files["backup"]="unrecovered original";
    assert(commitUpload(fs,std::string("staged"),std::string("final"),std::string("backup"))==409);
    assert(fs.files.at("backup")=="unrecovered original");assert(fs.files.at("final")=="original");
    std::cout<<"PASS upload promotion, rename failures, rollback, and retained recovery backup\n";
}
