#include "PlaylistManager.hpp"
#include <algorithm>
#include <cctype>
#include <random>

static bool validSimpleName(const String& name, const char* extension) {
  if (name.length() == 0 || name.length() > 80 || name.indexOf('/') >= 0 ||
      name.indexOf('\\') >= 0 || name.indexOf("..") >= 0 ||
      !name.endsWith(extension)) return false;
  for (size_t i = 0; i < name.length(); ++i) {
    const char c = name.charAt(i);
    const bool allowed = isalnum(static_cast<unsigned char>(c)) ||
      c == '-' || c == '_' || c == '.' || c == ' ';
    if (!allowed) return false;
  }
  return true;
}

static bool normalizePlaylistName(String& name) {
  if (name.endsWith(".json")) name.remove(name.length() - 5);
  if (name.length() == 0 || name.length() > 64 || name.indexOf('/') >= 0 ||
      name.indexOf('\\') >= 0 || name.indexOf("..") >= 0) return false;
  for (size_t i = 0; i < name.length(); ++i) {
    const char c = name.charAt(i);
    if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != ' ') {
      return false;
    }
  }
  return true;
}

PlaylistManager::PlaylistManager()
  : m_loop(false),
    m_currentIndex(-1),
    m_clearingEnabled(true),
    m_isFirstPattern(true)
{
}

bool PlaylistManager::addPattern(const String& filename) {
  if (!validSimpleName(filename, ".thr") || m_playlist.size() >= 256) return false;
  m_playlist.push_back(PlaylistItem(filename));
  return true;
}

bool PlaylistManager::removePattern(int index) {
  if (index >= 0 && index < m_playlist.size()) {
    m_playlist.erase(m_playlist.begin() + index);
    if (index <= m_currentIndex) {
      m_currentIndex--;
    }
    return true;
  }
  return false;
}

void PlaylistManager::clear() {
  m_playlist.clear();
  m_currentIndex = -1;
  m_isFirstPattern = true;
}

bool PlaylistManager::movePattern(int fromIndex, int toIndex) {
  if (fromIndex < 0 || fromIndex >= m_playlist.size() ||
      toIndex < 0 || toIndex >= m_playlist.size() || fromIndex == toIndex) {
    return false;
  }

  PlaylistItem item = m_playlist[fromIndex];
  m_playlist.erase(m_playlist.begin() + fromIndex);
  m_playlist.insert(m_playlist.begin() + toIndex, item);

  // Adjust current index if needed
  if (m_currentIndex == fromIndex) {
    m_currentIndex = toIndex;
  } else if (fromIndex < m_currentIndex && toIndex >= m_currentIndex) {
    m_currentIndex--;
  } else if (fromIndex > m_currentIndex && toIndex <= m_currentIndex) {
    m_currentIndex++;
  }
  return true;
}

void PlaylistManager::shuffle() {
  if (m_playlist.empty()) return;

  // Simple Fisher-Yates shuffle using Arduino's random
  for (int i = m_playlist.size() - 1; i > 0; i--) {
    int j = random(i + 1);
    std::swap(m_playlist[i], m_playlist[j]);
  }

  // If we were playing something, try to track it?
  // No, shuffle resets the order. We might lose track of "current".
  // Let's reset index to 0 or -1?
  // User expects shuffle to reorder the *queue*.
  // If playing, maybe keep current item at current index?
  // Too complex. Let's just shuffle everything.
  // Ideally, if playing, current item stays playing.
  // But changing order affects "next".

  // Let's just reset playlist state effectively
  m_currentIndex = -1;
}

void PlaylistManager::reset() {
  m_currentIndex = -1;
  m_isFirstPattern = true;
}

bool PlaylistManager::hasNext() {
  if (m_playlist.empty()) return false;
  if (m_loop) return true;
  return m_currentIndex < (int)m_playlist.size() - 1;
}

NextPatternResult PlaylistManager::getNextPattern() {
  NextPatternResult result;
  result.needsClearing = false;
  result.clearingPattern = CLEARING_NONE;

  if (m_playlist.empty()) {
    return result;
  }

  m_currentIndex++;
  if (m_currentIndex >= m_playlist.size()) {
    if (m_loop) {
      m_currentIndex = 0;
    } else {
      // End of playlist
      m_currentIndex = m_playlist.size(); // Keep at end
      return result;
    }
  }

  const PlaylistItem& item = m_playlist[m_currentIndex];
  result.filename = item.filename;

  // Determine clearing
  if (m_clearingEnabled && !m_isFirstPattern) {
    result.needsClearing = true;
    result.clearingPattern = CLEARING_RANDOM;
  }

  m_isFirstPattern = false;
  return result;
}

void PlaylistManager::setCurrentIndex(int index) {
  if (index >= -1 && index < (int)m_playlist.size()) {
    m_currentIndex = index - 1; // Set to previous so getNextPattern() returns index
    m_isFirstPattern = true; // Skip clearing if jumping manually
  }
}

bool PlaylistManager::skipToIndex(int index) {
  if (index < 0 || index >= static_cast<int>(m_playlist.size())) return false;
  setCurrentIndex(index);
  return true;
}

void PlaylistManager::skipNext() {
  // getNextPattern advances index automatically
}

void PlaylistManager::skipPrevious() {
  if (m_playlist.empty()) return;

  m_currentIndex -= 2; // Go back 2 so next increment lands on previous
  if (m_currentIndex < -1) {
    if (m_loop) {
      m_currentIndex = m_playlist.size() - 2;
    } else {
      m_currentIndex = -1;
    }
  }
  m_isFirstPattern = true;
}

bool PlaylistManager::saveToFile(String filename) {
  if (!normalizePlaylistName(filename)) return false;

  // Use /playlists directory
  if (!SD.exists("/playlists")) {
    SD.mkdir("/playlists");
  }
  String path = "/playlists/" + filename + ".json";
  String tempPath = path + ".tmp";
  String backupPath = path + ".bak";

  JsonDocument doc;
  JsonArray arr = doc["items"].to<JsonArray>();

  for (const auto& item : m_playlist) {
    JsonObject obj = arr.add<JsonObject>();
    obj["file"] = item.filename;
  }

  doc["loop"] = m_loop;
  doc["clearing"] = m_clearingEnabled;

  SD.remove(tempPath);
  File file = SD.open(tempPath.c_str(), FILE_WRITE);
  if (!file) return false;

  if (serializeJson(doc, file) == 0) {
    file.close();
    SD.remove(tempPath);
    return false;
  }
  file.close();

  SD.remove(backupPath);
  const bool hadOriginal = SD.exists(path);
  if (hadOriginal && !SD.rename(path, backupPath)) {
    SD.remove(tempPath);
    return false;
  }
  if (!SD.rename(tempPath, path)) {
    if (hadOriginal) SD.rename(backupPath, path);
    SD.remove(tempPath);
    return false;
  }
  if (hadOriginal) SD.remove(backupPath);
  return true;
}

bool PlaylistManager::loadFromFile(String filename) {
  if (!normalizePlaylistName(filename)) return false;
  String path = "/playlists/" + filename + ".json";

  File file = SD.open(path.c_str(), FILE_READ);
  if (!file) return false;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) return false;
  if (!doc["items"].is<JsonArray>()) return false;

  std::vector<PlaylistItem> loaded;
  JsonArray arr = doc["items"];
  for (JsonObject obj : arr) {
    String f = obj["file"].as<String>();
    if (!validSimpleName(f, ".thr") || loaded.size() >= 256) return false;
    loaded.push_back(PlaylistItem(f));
  }

  m_playlist.swap(loaded);

  if (doc["loop"].is<bool>()) m_loop = doc["loop"];
  if (doc["clearing"].is<bool>()) m_clearingEnabled = doc["clearing"];

  reset();
  return true;
}
