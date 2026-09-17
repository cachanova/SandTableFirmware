#include "PlaylistManager.hpp"
#include <cassert>
#include <iostream>
int main(){
    PlaylistManager playlist;
    for(const char* file:{"A.thr","B.thr","C.thr"})assert(playlist.addPattern(file));
    playlist.setCurrentIndex(-1);assert(playlist.getNextPattern().filename=="A.thr");
    assert(playlist.movePattern(0,2));assert(playlist.getCurrentIndex()==2);
    assert(playlist.getItem(0).filename=="B.thr" && playlist.getItem(2).filename=="A.thr");
    assert(playlist.movePattern(2,0));assert(playlist.getCurrentIndex()==0);
    assert(playlist.saveToFile("roundtrip"));
    const auto original=*storage.files.at("/playlists/roundtrip.json");
    playlist.clear();assert(playlist.addPattern("D.thr"));
    storage.writeLimit=8;assert(!playlist.saveToFile("roundtrip"));
    assert(*storage.files.at("/playlists/roundtrip.json")==original);
    storage.writeLimit=65536;storage.flushFailure=true;assert(!playlist.saveToFile("roundtrip"));
    assert(*storage.files.at("/playlists/roundtrip.json")==original);
    storage.flushFailure=false;storage.failCommit=true;assert(!playlist.saveToFile("roundtrip"));
    assert(*storage.files.at("/playlists/roundtrip.json")==original);
    storage.failCommit=false;assert(playlist.loadFromFile("roundtrip"));assert(playlist.count()==3);
    assert(playlist.getItem(0).filename=="A.thr");
    *storage.files.at("/playlists/roundtrip.json")="{\"items\":[{\"file\":\"E.thr\"},{\"file\":\"../bad.thr\"}]}";
    assert(!playlist.loadFromFile("roundtrip"));assert(playlist.count()==3 && playlist.getItem(0).filename=="A.thr");
    *storage.files.at("/playlists/roundtrip.json")=std::string(32769,' ');
    assert(!playlist.loadFromFile("roundtrip"));assert(playlist.count()==3);
    playlist.clear();for(int i=0;i<256;i++)assert(playlist.addPattern("A.thr"));
    assert(!playlist.addPattern("B.thr"));assert(playlist.count()==256);
    std::cout<<"PASS: actual playlist cursor/reorder/capacity; saved original survives short write, flush and commit failures; invalid loads preserve live playlist\n";
}
