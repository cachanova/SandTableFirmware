#pragma once
#include <deque>
#include <Arduino.h>
#include <SDCard.hpp>
#include <ArduinoJson.h>
#include <ClearingPatternGen.hpp>

struct PlaylistItem {
  String filename;

  PlaylistItem(const String& f)
    : filename(f) {}
};

// Result from getNextPattern - includes clearing info
struct NextPatternResult {
  String filename;
  ClearingPattern clearingPattern;
  bool needsClearing;  // True if this is not the first pattern
};

class PlaylistManager {
public:
  PlaylistManager();

  bool addPattern(const String& filename);
  bool removePattern(int index);
  void clear();
  bool movePattern(int fromIndex, int toIndex);
  void shuffle(); // Randomize the list in-place

  int count() const { return static_cast<int>(m_playlist.size()); }
  const PlaylistItem& getItem(int index) const { return m_playlist[index]; }

  void setLoop(bool enabled) { m_loop = enabled; }
  bool isLoop() const { return m_loop; }

  // Clearing settings
  void setClearingEnabled(bool enabled) { m_clearingEnabled = enabled; }
  bool isClearingEnabled() const { return m_clearingEnabled; }

  // Playlist control
  void reset();
  bool hasNext();
  NextPatternResult getNextPattern();
  int getCurrentIndex() const { return m_currentIndex; }
  void setCurrentIndex(int index);

  // Skip controls
  bool skipToIndex(int index);
  void skipNext();
  void skipPrevious();

  // File I/O
  bool saveToFile(String filename);
  bool loadFromFile(String filename);

private:
  std::deque<PlaylistItem> m_playlist;
  bool m_loop;
  int m_currentIndex;
  bool m_clearingEnabled;
  bool m_isFirstPattern;  // Track if this is the first pattern (no clearing needed)
};
