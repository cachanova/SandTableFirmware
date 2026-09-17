#pragma once
#include <ESPAsyncWebServer.h>
#include <vector>
#include <deque>
#include <ArduinoJson.h>
#include <freertos/semphr.h>
#include <atomic>
#include <Print.h>
#include <SDCard.hpp>
#include <PolarControl.hpp>
#include <LEDController.hpp>
#include <PresenceAutomation.hpp>
#include <PresenceSensor.hpp>
#include <PlaylistManager.hpp>
#include "Logger.hpp"

class SisyphusWebServer {
public:
    SisyphusWebServer(uint16_t port = 80);
    void begin(PolarControl *polarControl, LEDController *ledController,
               PresenceSensor *presenceSensor);
    void loop(); // Check for pattern queue processing
    void getRequestStats(uint32_t& total, uint32_t& inflight) const;

private:
    AsyncWebServer m_server;
    AsyncEventSource m_events;  // SSE for console logs
    PolarControl *m_polarControl;
    LEDController *m_ledController;
    PresenceSensor *m_presenceSensor;
    PresenceAutomation m_presenceAutomation;

    enum class MotionOwner : uint8_t { NONE, PATTERN, MANUAL, TUNING };
    enum class PendingMotion : uint8_t {
        NONE,
        MANUAL,
        MANUAL_JOG,
        THETA_CONTINUOUS,
        THETA_STRESS,
        THETA_SEGMENT,
        RHO_CONTINUOUS,
        RHO_STRESS,
        RHO_SEGMENT
    };
    MotionOwner m_activeMotion = MotionOwner::NONE;
    PendingMotion m_pendingMotion = PendingMotion::NONE;
    float m_pendingManualTheta = 0.0f;
    float m_pendingManualRho = 0.0f;
    float m_pendingJogTheta = 0.0f;
    float m_pendingJogRho = 0.0f;
    float m_pendingThetaSegmentTarget = 0.0f;
    float m_pendingRhoSegmentTarget = 0.0f;
#ifdef SISYPHUS_RHO_COMMISSIONING
    // Fail-closed after every reboot: relative manual jogs only. Assigning a
    // commissioning origin always requires explicit operator confirmation.
    std::atomic<bool> m_rhoCommissioningMode{false};
#endif

    // Pattern queue management
    String m_queuedPattern;
    String m_currentPattern;  // Currently running pattern filename
    bool m_hasQueuedPattern;
    bool m_singlePatternClearing;     // Run clearing before single pattern
    ClearingPattern m_selectedClearing; // Selected clearing pattern type

    // Playlist management
    PlaylistManager m_playlist;
    bool m_playlistMode;
    bool m_runningClearing;       // True if currently running a clearing pattern
    std::atomic<bool> m_firstPointCleared; // True if we've cleared the lead-in path
    String m_pendingPattern;      // Pattern to run after clearing completes
    ClearingPattern m_activeClearingPattern;

    std::atomic<uint32_t> m_uploadSequence{0};

    // Route handlers
    void handlePosition(AsyncWebServerRequest *request);
    void handleStatus(AsyncWebServerRequest *request);
    void handleErrors(AsyncWebServerRequest *request);
    void handleErrorsClear(AsyncWebServerRequest *request);
    void handleLogs(AsyncWebServerRequest *request);
    void handleLogsText(AsyncWebServerRequest *request);
    void handleLogsClear(AsyncWebServerRequest *request);
    void handlePatternStart(AsyncWebServerRequest *request);
    void handlePatternStop(AsyncWebServerRequest *request);
    void handlePatternPause(AsyncWebServerRequest *request);
    void handlePatternResume(AsyncWebServerRequest *request);
    void handleManualMove(AsyncWebServerRequest *request);
    void handleManualJog(AsyncWebServerRequest *request);
    void handleManualSetHome(AsyncWebServerRequest *request);
    void handlePlaybackStop(AsyncWebServerRequest *request);
    void handleMotionStop(AsyncWebServerRequest *request);
    void handleMotionTelemetry(AsyncWebServerRequest *request);
    void handleRhoServiceModeGet(AsyncWebServerRequest *request);
    void handleRhoServiceModeSet(AsyncWebServerRequest *request);
    void handleHome(AsyncWebServerRequest *request);
    void handleHomeConfirm(AsyncWebServerRequest *request);
    void handleKnownPositionHome(AsyncWebServerRequest *request);
    void handleHomingTrace(AsyncWebServerRequest *request);
    void handleFileList(AsyncWebServerRequest *request);
    void handleFileUpload(AsyncWebServerRequest *request, String filename,
                         size_t index, uint8_t *data, size_t len, bool final);
    void handleFileDelete(AsyncWebServerRequest *request);
    void handleLEDBrightnessGet(AsyncWebServerRequest *request);
    void handleLEDBrightnessSet(AsyncWebServerRequest *request);
    void handleSpeedGet(AsyncWebServerRequest *request);
    void handleSpeedSet(AsyncWebServerRequest *request);
    void handleSystemInfo(AsyncWebServerRequest *request);
    void handlePresenceGet(AsyncWebServerRequest *request);
    void handlePresenceCalibrate(AsyncWebServerRequest *request);
    void handlePresenceSettingsGet(AsyncWebServerRequest *request);
    void handlePresenceSettingsSet(AsyncWebServerRequest *request);
    void handleRoot(AsyncWebServerRequest *request);

    // Playlist handlers
    void handlePlaylistAdd(AsyncWebServerRequest *request);
    void handlePlaylistAddAll(AsyncWebServerRequest *request);
    void handlePlaylistRemove(AsyncWebServerRequest *request);
    void handlePlaylistClear(AsyncWebServerRequest *request);
    void handlePlaylistGet(AsyncWebServerRequest *request);
    void handlePlaylistStart(AsyncWebServerRequest *request);
    void handlePlaylistStop(AsyncWebServerRequest *request);
    void handlePlaylistLoop(AsyncWebServerRequest *request);
    void handlePlaylistShuffle(AsyncWebServerRequest *request);
    void handlePlaylistMove(AsyncWebServerRequest *request);
    void handlePlaylistSkipTo(AsyncWebServerRequest *request);
    void handlePlaylistPrev(AsyncWebServerRequest *request);
    void handlePlaylistNext(AsyncWebServerRequest *request);
    void handlePlaylistSave(AsyncWebServerRequest *request);
    void handlePlaylistLoad(AsyncWebServerRequest *request);
    void handlePlaylistList(AsyncWebServerRequest *request);
    void handlePlaylistClearing(AsyncWebServerRequest *request);

    // Tuning handlers
    void handleTuningGet(AsyncWebServerRequest *request);
    void handleTuningMotionSet(AsyncWebServerRequest *request);
    void handleTuningThetaDriverSet(AsyncWebServerRequest *request);
    void handleTuningRhoDriverSet(AsyncWebServerRequest *request);
    void handleTuningHomingSet(AsyncWebServerRequest *request);
    void handleTuningTestThetaContinuous(AsyncWebServerRequest *request);
    void handleTuningTestThetaStress(AsyncWebServerRequest *request);
    void handleTuningTestThetaSegment(AsyncWebServerRequest *request);
    void handleTuningTestRhoContinuous(AsyncWebServerRequest *request);
    void handleTuningTestRhoStress(AsyncWebServerRequest *request);
    void handleTuningTestRhoSegment(AsyncWebServerRequest *request);

    // Helper methods
    void processPatternQueue();
    void clearPlaybackLocked();
    bool beginPatternLocked(const String& filename);
    bool prepareReplacementLocked();
    bool prepareManualJogLocked();
    bool queueTuningTestLocked(PendingMotion motion);
    void updatePresenceAutomation();
    void broadcastPosition(); // New streaming method
    void broadcastSinglePosition(AsyncEventSourceClient *client = nullptr);
    void writeStatusJSON(Print& out);
    void writeFileListJSON(Print& out);
    void writeSystemInfoJSON(Print& out);
    String getStateString();
    void noteRequest(AsyncWebServerRequest *request);
    
    bool updateFileListCache();
    static void fileCacheTask(void* arg);
    String resolvePatternPath(const String& filename);
    struct FileEntry;
    const FileEntry* findFileEntryByBase(const String& baseName) const;

    unsigned long m_lastPosBroadcast = 0; // Timer for position streaming
    float m_lastBroadcastX = -1.0f;
    float m_lastBroadcastY = -1.0f;
    float m_lastBroadcastVr = 0.0f;
    float m_lastBroadcastVt = 0.0f;
    unsigned long m_lastPosSent = 0;
    std::atomic<uint32_t> m_requestTotal{0};
    std::atomic<uint32_t> m_requestInflight{0};
    std::atomic<uint32_t> m_imageInflight{0};
    std::atomic<uint32_t> m_bulkInflight{0};
    std::atomic<uint32_t> m_lastUploadActivity{0};

    std::atomic<bool> m_fileListDirty; // Flag to trigger regeneration of cache
    std::atomic<bool> m_fileScanActive{false};
    std::atomic<uint32_t> m_fileListRevision{0};
    TaskHandle_t m_fileCacheTask = nullptr;

    struct FileEntry {
        String name;
        String baseName;
        size_t size;
        time_t time;
        bool hasImage;
        time_t imageTime;
        size_t imageSize;
        size_t thumbnailSize;
        time_t thumbnailTime;
        bool isDirectory;
        String pngPath;
    };
    struct FileIndexEntry {
        String baseName;
        size_t index;
    };
    // Segmented storage avoids multi-kilobyte contiguous reallocations while
    // old metadata and active HTTP responses share a fragmented ESP32 heap.
    std::deque<FileEntry> m_fileCache;
    std::deque<FileIndexEntry> m_fileIndex;
    SemaphoreHandle_t m_cacheMutex = nullptr;
    SemaphoreHandle_t m_stateMutex = nullptr;


};
