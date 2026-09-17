#include "WiFi.h"
#include "SisyphusWebServer.hpp"
#include "WebUI.h"
#include "ManualUI.h"
#include "SettingsUI.h"
#include "FileUI.h"
#include "JsonHelpers.hpp"
#include "BufferedResponse.hpp"
#include "BulkResponseBudget.hpp"
#include "PatternImageResponse.hpp"
#include "StaticContentResponse.hpp"
#include "PolarUtils.hpp"
#include "MakeUnique.hpp"
#include <SemaphoreGuard.hpp>
#include <SDCard.hpp>
#include <ClearingPatternGen.hpp>
#include <ErrorLog.hpp>
#include <RhoAcousticProfile.hpp>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include "UploadCommit.hpp"

static constexpr size_t kResponseBufferSize = 256;
static constexpr unsigned long kFileCacheThrottleMs = 500;

struct UploadContext {
    // An 80-character filename is repeated in the nested directory path.
    char finalPath[192];
    char tempPath[192];
    size_t maxBytes;
    size_t received;
    size_t requestBytes;
    std::atomic<uint32_t>* active;
    int status;
    bool originalImage;
    bool pattern;
    bool staged;
    bool complete;
};

// The SDK owns/free()s _tempObject; explicitly remove abandoned staging files.
static void cleanUploadFile(AsyncWebServerRequest* request) {
    request->_tempFile.close();
    auto* upload = static_cast<UploadContext*>(request->_tempObject);
    if (upload && upload->active) { upload->active->fetch_sub(1); upload->active = nullptr; }
    if (upload && !upload->complete && upload->tempPath[0]) {
        try { SD.remove(upload->tempPath); }
        catch (const std::bad_alloc&) { /* Leave an ignored staging file on SD. */ }
    }
}

static void writeJsonString(Print& out, const String& value) {
    out.print('"');
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value.charAt(i);
        if (c == '"' || c == '\\') {
            out.print('\\');
            out.print(c);
        } else if (c == '\n') {
            out.print("\\n");
        } else if (c == '\r') {
            out.print("\\r");
        } else if (c == '\t') {
            out.print("\\t");
        } else {
            out.print(c);
        }
    }
    out.print('"');
}

static bool validSimpleFilename(const String& filename, const char* extension) {
    if (filename.length() < 5 || filename.length() > 80 ||
        filename.indexOf('/') >= 0 || filename.indexOf('\\') >= 0 ||
        filename.indexOf("..") >= 0 || !filename.endsWith(extension)) {
        return false;
    }
    for (size_t i = 0; i < filename.length(); ++i) {
        const char c = filename.charAt(i);
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ' ';
        if (!allowed) return false;
    }
    return true;
}

static bool parseStrictInt(const String& text, int& value) {
    if (text.length() == 0) return false;
    char* end = nullptr;
    errno = 0;
    const long parsed = strtol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0' ||
        parsed < INT_MIN || parsed > INT_MAX) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

static bool parseStrictUint32(const String& text, uint32_t& value) {
    if (text.length() == 0 || text.charAt(0) == '-') return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = strtoul(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

static bool parseStrictFloat(const String& text, float& value) {
    if (text.length() == 0) return false;
    char* end = nullptr;
    errno = 0;
    const float parsed = strtof(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

static bool parseStrictBool(const String& text, bool& value) {
    if (text == "true" || text == "1") {
        value = true;
        return true;
    }
    if (text == "false" || text == "0") {
        value = false;
        return true;
    }
    return false;
}

static bool requirePatternStorage(AsyncWebServerRequest* request) {
    if (isSDCardReady()) return true;
    request->send(503, "application/json",
        "{\"success\":false,\"message\":\"SD card storage is unavailable\"}");
    return false;
}

// Resolve from the metadata index: starting a known pattern needs no SD opens
// on the AsyncTCP task. The file reader reports any subsequent open failure.
String SisyphusWebServer::resolvePatternPath(const String& filename) {
    if (!isSDCardReady() || !validSimpleFilename(filename, ".thr")) return "";
    const String base = filename.substring(0, filename.length() - 4);
    SemaphoreGuard cacheLock(m_cacheMutex);
    const FileEntry* entry = findFileEntryByBase(base);
    if (!entry) return "";
    return entry->isDirectory ? "/patterns/" + base + "/" + filename
                              : "/patterns/" + filename;
}

static constexpr bool kEnablePatternImages = true;

static const char* getClearingPatternName(ClearingPattern pattern) {
    switch (pattern) {
        case SPIRAL_OUTWARD: return "Spiral Outward";
        case SPIRAL_INWARD: return "Spiral Inward";
        case CONCENTRIC_CIRCLES: return "Concentric Circles";
        case ZIGZAG_RADIAL: return "Zigzag Radial";
        case PETAL_FLOWER: return "Petal Flower";
        case CLEARING_NONE: return "";
        case CLEARING_RANDOM: return "Random";
        default: return "";
    }
}

SisyphusWebServer::SisyphusWebServer(uint16_t port)
    : m_server(port),
      m_events("/api/stream"),
      m_polarControl(nullptr),
      m_ledController(nullptr),
      m_presenceSensor(nullptr),
      m_hasQueuedPattern(false),
      m_singlePatternClearing(false),
      m_selectedClearing(CLEARING_NONE),
      m_playlistMode(false),
      m_runningClearing(false),
      m_firstPointCleared(false),
      m_activeClearingPattern(CLEARING_NONE),
      m_fileListDirty(true) {
    m_cacheMutex = xSemaphoreCreateMutex();
    m_stateMutex = xSemaphoreCreateMutex();
    m_ledMutex = xSemaphoreCreateMutexStatic(&m_ledMutexStorage);
}

void SisyphusWebServer::getRequestStats(uint32_t& total, uint32_t& inflight) const {
    total = m_requestTotal.load();
    inflight = m_requestInflight.load();
}

void SisyphusWebServer::noteRequest(AsyncWebServerRequest *request) {
    if (m_requestTotal.fetch_add(1) == 0) {
        LOG("HTTP callbacks running on Core %d\r\n", xPortGetCoreID());
    }
    const bool imageRequest = request->url() == "/api/pattern/image" ||
        request->url() == "/api/pattern/download";
    const bool bulkRequest = request->method() == HTTP_GET &&
        BulkResponseBudget::isBulkPath(request->url().c_str());
    // Keep each capture within std::function's small-object buffer. Capturing
    // this + request + imageRequest would allocate on every ordinary request.
    if (request->url() == "/api/files/upload") {
        request->onDisconnect([this, request]() {
            cleanUploadFile(request);
            m_requestInflight.fetch_sub(1);
        });
    } else {
        request->onDisconnect([this, imageRequest, bulkRequest]() {
            if (imageRequest) m_imageInflight.fetch_sub(1);
            if (bulkRequest) m_bulkInflight.fetch_sub(1);
            m_requestInflight.fetch_sub(1);
        });
    }
    // Install the potentially allocating callback before publishing admission.
    m_requestInflight.fetch_add(1);
    if (imageRequest) m_imageInflight.fetch_add(1);
    if (bulkRequest) m_bulkInflight.fetch_add(1);
}

void SisyphusWebServer::begin(PolarControl *polarControl,
                              LEDController *ledController,
                              PresenceSensor *presenceSensor) {
    m_polarControl = polarControl;
    m_ledController = ledController;
    m_presenceSensor = presenceSensor;

    // Fragmentation can refuse even a small response object while total heap
    // is healthy. Keep a failed HTTP allocation from rebooting running motion.
    m_server.addMiddleware([this](AsyncWebServerRequest* request, ArMiddlewareNext next) {
        try {
            // AsyncTCP serializes these handlers. noteRequest publishes the
            // slot before this callback returns; disconnect releases it even
            // on cancellation or serialization failure. Keep large TCP bodies
            // from overlapping an image's SD worker/stream allocation.
            if (request->method() == HTTP_GET &&
                BulkResponseBudget::isBulkPath(request->url().c_str()) &&
                !BulkResponseBudget::canStart(m_bulkInflight.load() + m_uploadInflight.load(),
                    heap_caps_get_free_size(MALLOC_CAP_8BIT),
                    heap_caps_get_largest_free_block(MALLOC_CAP_8BIT))) {
                std::unique_ptr<AsyncWebServerResponse> response(
                    request->beginResponse(503, "application/json", kResponseUnavailable));
                response->addHeader("Retry-After", "1");
                request->send(response.release());
                return;
            }
            next();
        } catch (const std::bad_alloc&) {
            try {
                request->send(503, "application/json", kResponseUnavailable);
            } catch (const std::bad_alloc&) {
                // There may not even be room for the SDK's small error reply.
                request->abort();
            }
        }
    });

    // Enable CORS
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");

    // Handle OPTIONS requests (preflight)
    m_server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404);
        }
    });

    // Web interface routes
    m_server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleRoot(request);
    });

    m_server.on("/settings", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        request->send(new StaticContentResponse("text/html",
                      reinterpret_cast<const uint8_t *>(SETTINGS_UI_HTML),
                      sizeof(SETTINGS_UI_HTML) - 1));
    });

    m_server.on("/tuning", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        request->redirect("/settings");
    });

    m_server.on("/manual", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        request->send(new StaticContentResponse("text/html",
                      reinterpret_cast<const uint8_t *>(MANUAL_UI_HTML),
                      sizeof(MANUAL_UI_HTML) - 1));
    });

    m_server.on("/files", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        request->send(new StaticContentResponse("text/html",
                      reinterpret_cast<const uint8_t *>(FILE_UI_HTML),
                      sizeof(FILE_UI_HTML) - 1));
    });

    m_server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleStatus(request);
    });

    m_server.on(AsyncURIMatcher::exact("/api/presence"), HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePresenceGet(request);
    });

    m_server.on(AsyncURIMatcher::exact("/api/presence/calibrate"), HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePresenceCalibrate(request);
    });

    m_server.on(AsyncURIMatcher::exact("/api/settings/presence"), HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePresenceSettingsGet(request);
    });

    m_server.on(AsyncURIMatcher::exact("/api/settings/presence"), HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePresenceSettingsSet(request);
    });

    m_server.on("/api/errors", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleErrors(request);
    });

    m_server.on("/api/errors/clear", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleErrorsClear(request);
    });

    m_server.on(AsyncURIMatcher::exact("/api/logs"), HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleLogs(request);
    });

    m_server.on("/api/logs/text", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleLogsText(request);
    });

    m_server.on("/api/logs/clear", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleLogsClear(request);
    });

    m_server.on("/api/pattern/start", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePatternStart(request);
    });

    m_server.on("/api/pattern/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePatternStop(request);
    });

    m_server.on("/api/pattern/pause", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePatternPause(request);
    });

    m_server.on("/api/pattern/resume", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePatternResume(request);
    });

    m_server.on("/api/manual/move", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleManualMove(request);
    });

    m_server.on("/api/manual/jog", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleManualJog(request);
    });

    m_server.on(AsyncURIMatcher::exact("/api/manual/set-home"), HTTP_POST,
        [this](AsyncWebServerRequest *request) {
            noteRequest(request);
            handleManualSetHome(request);
        });

    m_server.on("/api/motion/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleMotionStop(request);
    });

    m_server.on(AsyncURIMatcher::exact("/api/home/abort"), HTTP_POST,
        [this](AsyncWebServerRequest *request) {
            noteRequest(request);
            SemaphoreGuard stateLock(m_stateMutex);
            clearPlaybackLocked();
            m_activeMotion = MotionOwner::NONE;
            // An abort arriving just after automatic success must still
            // invalidate zero and disable rho, even if state is already IDLE.
            m_polarControl->emergencyStop(true);
            request->send(200, "application/json",
                "{\"success\":true,\"emergency\":true,\"requiresHoming\":true}");
        });

    m_server.on("/api/motion/telemetry", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleMotionTelemetry(request);
    });

    m_server.on("/api/rho-service/mode", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleRhoServiceModeGet(request);
    });

    m_server.on("/api/rho-service/mode", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleRhoServiceModeSet(request);
    });

    m_server.on(AsyncURIMatcher::exact("/api/home"), HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleHome(request);
    });

    m_server.on(AsyncURIMatcher::exact("/api/home/confirm"), HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleHomeConfirm(request);
    });

    m_server.on("/api/tuning/home/known-positions", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleKnownPositionHome(request);
    });

    m_server.on("/api/tuning/homing/trace", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleHomingTrace(request);
    });

    m_server.on("/api/position", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePosition(request);
    });

    m_server.on("/api/files", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleFileList(request);
    });

    // Export the exact installed THR through the same bounded SD worker as PNGs.
    // The metadata index resolves both legacy flat and directory-based patterns.
    m_server.on("/api/pattern/download", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        if (!requirePatternStorage(request)) return;
        if (m_imageInflight > 1 || m_fileScanActive.load()) {
            auto response = std_patch::make_unique<BufferedResponse>("text/plain", 64);
            response->setCode(503);
            response->addHeader("Retry-After", "1");
            request->send(response.release());
            return;
        }
        if (!request->hasParam("file") ||
            !validSimpleFilename(request->getParam("file")->value(), ".thr")) {
            request->send(400);
            return;
        }
        const String filename = request->getParam("file")->value();
        String path;
        size_t size = 0;
        {
            SemaphoreGuard lock(m_cacheMutex);
            const auto* entry = findFileEntryByBase(filename.substring(0, filename.length() - 4));
            if (entry) {
                path = entry->isDirectory ? "/patterns/" + entry->baseName + "/" + filename
                                          : "/patterns/" + filename;
                size = entry->size;
            }
        }
        if (path.isEmpty()) {
            request->send(m_fileListDirty ? 503 : 404);
            return;
        }
        if (!size) {
            auto response = request->beginResponse(200, "text/plain", "");
            response->addHeader("Cache-Control", "no-store");
            request->send(response);
            return;
        }
        auto response = std_patch::make_unique<PatternImageResponse>(path, size, "text/plain");
        if (!response->_sourceValid()) { request->send(503); return; }
        response->addHeader("Cache-Control", "no-store");
        response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
        request->send(response.release());
    });

    if (kEnablePatternImages) {
        m_server.on("/api/pattern/image", HTTP_GET, [this](AsyncWebServerRequest *request) {
            noteRequest(request);

            if (!requirePatternStorage(request)) return;

            // Limit concurrent file operations to prevent FD exhaustion (FATFS limit is low)
            if (m_imageInflight > 1 || m_fileScanActive.load()) {
                std::unique_ptr<AsyncWebServerResponse> response(request->beginResponse(503));
                response->addHeader("Retry-After", "1");
                request->send(response.release());
                return;
            }

            if (!request->hasParam("file")) {
                request->send(400);
                return;
            }
            String file = request->getParam("file")->value();
            if (file.endsWith(".thr")) {
                file = file.substring(0, file.length() - 4);
            }

            // Lookup in cache
            bool found = false;
            time_t imgTime = 0;
            size_t imageSize = 0;
            String pngPath;

            if (m_cacheMutex) {
                SemaphoreGuard cacheLock(m_cacheMutex);
                const FileEntry* entry = findFileEntryByBase(file);
                if (entry && entry->hasImage && entry->pngPath.length() > 0) {
                    found = true;
                    imgTime = entry->imageTime;
                    imageSize = entry->imageSize;
                    pngPath = entry->pngPath;
                    if (request->hasParam("thumbnail") &&
                        request->getParam("thumbnail")->value() == "1" &&
                        entry->thumbnailSize > 0) {
                        pngPath = "/patterns/" + entry->baseName + "/" +
                                  entry->baseName + ".thumb.png";
                        imageSize = entry->thumbnailSize;
                        imgTime = entry->thumbnailTime;
                    }
                }
            }

            if (!found) {
                if (m_fileListDirty || m_fileScanActive) {
                    std::unique_ptr<AsyncWebServerResponse> response(request->beginResponse(503));
                    response->addHeader("Retry-After", "1");
                    request->send(response.release());
                } else {
                    request->send(404);
                }
                return;
            }

            if (found) {
                if (imgTime > 0 && request->hasHeader("If-Modified-Since")) {
                    char timeStr[32];
                    struct tm * tmstruct = gmtime((time_t*)&imgTime);
                    strftime(timeStr, sizeof(timeStr), "%a, %d %b %Y %H:%M:%S GMT", tmstruct);
                    if (request->getHeader("If-Modified-Since")->value() == timeStr) {
                        request->send(304);
                        return;
                    }
                }
                auto response = std_patch::make_unique<PatternImageResponse>(pngPath, imageSize);
                if (!response->_sourceValid()) {
                    response.reset();
                    request->send(503);
                    return;
                }
                response->addHeader("Cache-Control", "public, max-age=31536000, immutable");

                if (imgTime > 0) {
                    char timeStr[32];
                    struct tm * tmstruct = gmtime((time_t*)&imgTime);
                    strftime(timeStr, sizeof(timeStr), "%a, %d %b %Y %H:%M:%S GMT", tmstruct);
                    response->addHeader("Last-Modified", timeStr);
                }

                request->send(response.release());
            } else {
                request->send(404);
            }
        });
    } else {
        m_server.on("/api/pattern/image", HTTP_GET, [this](AsyncWebServerRequest *request) {
            noteRequest(request);
            request->send(404);
        });
    }

    m_server.on("/api/files/upload", HTTP_POST,
        [this](AsyncWebServerRequest *request) {
            UploadContext* upload = static_cast<UploadContext*>(request->_tempObject);
            int status = upload == nullptr ? 400 :
                (upload->status ? upload->status : (upload->staged ? 0 : 400));
            size_t fileParts = 0;
            for (size_t i = 0; i < request->params(); ++i) {
                const auto* param = request->getParam(i);
                if (!param->isPost()) continue;
                if (!param->isFile() || param->name() != "file") status = 400;
                else ++fileParts;
            }
            if (fileParts != 1 || !request->multipartComplete() ||
                (upload && upload->requestBytes != request->contentLength())) status = 400;
            if (!status) {
                const String destination(upload->finalPath);
                status = commitUpload(SD, String(upload->tempPath), destination, String(destination + ".bak"));
                if (!status) {
                    upload->complete = true;
                    m_fileListDirty.store(true);
                    const String base = destination.substring(destination.lastIndexOf('/') + 1,
                                                              destination.length() - 4);
                    if (upload->pattern) SD.remove(destination.substring(0, destination.length() - 4) + ".png");
                    if (upload->originalImage || upload->pattern)
                        SD.remove("/patterns/" + base + "/" + base + ".thumb.png");
                    m_lastUploadActivity.store(millis());
                }
            }
            cleanUploadFile(request);
            request->_tempObject = nullptr;
            if (upload != nullptr) free(upload);
            if (status != 0) {
                request->send(status, "application/json",
                    status == 503
                        ? "{\"success\":false,\"message\":\"Storage busy or unavailable; retry shortly\"}"
                        : "{\"success\":false,\"message\":\"Upload rejected; file was not accepted\"}");
                return;
            }
            auto response = std_patch::make_unique<BufferedResponse>("application/json", 0, true);
            response->print("{\"success\":true}");
            request->send(response.release());
        },
        [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            // Multipart callbacks run before the SDK's request middleware.
            try {
                handleFileUpload(request, filename, index, data, len, final);
            } catch (const std::bad_alloc&) {
                cleanUploadFile(request);
                auto* upload = static_cast<UploadContext*>(request->_tempObject);
                if (upload) upload->status = 503;
                else request->abort();
            }
        }
    );

    m_server.on("/api/files/delete", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleFileDelete(request);
    });

    m_server.on("/api/led/brightness", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleLEDBrightnessGet(request);
    });

    m_server.on("/api/led/brightness", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleLEDBrightnessSet(request);
    });

    m_server.on("/api/speed", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleSpeedGet(request);
    });

    m_server.on("/api/speed", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleSpeedSet(request);
    });

    m_server.on("/api/system/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleSystemInfo(request);
    });

    // Playlist routes
    m_server.on("/api/playlist/add", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistAdd(request);
    });

    m_server.on("/api/playlist/addall", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistAddAll(request);
    });

    m_server.on("/api/playlist/remove", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistRemove(request);
    });

    m_server.on("/api/playlist/clear", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistClear(request);
    });

    // ESPAsyncWebServer's plain-string matcher also matches child paths, so
    // keep this collection route from swallowing /api/playlist/list.
    m_server.on(AsyncURIMatcher::exact("/api/playlist"), HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistGet(request);
    });

    m_server.on("/api/playlist/start", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistStart(request);
    });

    m_server.on("/api/playlist/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistStop(request);
    });

    m_server.on("/api/playlist/loop", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistLoop(request);
    });

    m_server.on("/api/playlist/shuffle", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistShuffle(request);
    });

    m_server.on("/api/playlist/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistSave(request);
    });

    m_server.on("/api/playlist/load", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistLoad(request);
    });

    m_server.on("/api/playlist/list", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistList(request);
    });

    m_server.on("/api/playlist/clearing", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistClearing(request);
    });

    m_server.on("/api/playlist/move", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistMove(request);
    });

    m_server.on("/api/playlist/skipto", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistSkipTo(request);
    });

    m_server.on("/api/playlist/next", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistNext(request);
    });

    m_server.on("/api/playlist/prev", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handlePlaylistPrev(request);
    });

    // Tuning routes
    // Use an exact matcher so the parent does not intercept the driver dump
    // endpoints below (for example /api/tuning/dump/theta).
    m_server.on(AsyncURIMatcher::exact("/api/tuning"), HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleTuningGet(request);
    });

    m_server.on("/api/tuning/motion", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleTuningMotionSet(request);
    });

    m_server.on("/api/tuning/theta", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleTuningThetaDriverSet(request);
    });

    m_server.on("/api/tuning/rho", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleTuningRhoDriverSet(request);
    });

    m_server.on("/api/tuning/homing", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleTuningHomingSet(request);
    });

    m_server.on("/api/tuning/test/theta/continuous", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleTuningTestThetaContinuous(request);
    });

    m_server.on("/api/tuning/test/theta/stress", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleTuningTestThetaStress(request);
    });

    m_server.on("/api/tuning/test/theta/segment", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleTuningTestThetaSegment(request);
    });

    m_server.on("/api/tuning/test/rho/continuous", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleTuningTestRhoContinuous(request);
    });

    m_server.on("/api/tuning/test/rho/stress", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleTuningTestRhoStress(request);
    });

    m_server.on("/api/tuning/test/rho/segment", HTTP_POST, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        handleTuningTestRhoSegment(request);
    });

    // Driver dump routes (for diagnostics)
    m_server.on("/api/tuning/dump/theta", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        auto response = std_patch::make_unique<BufferedResponse>("application/json", kResponseBufferSize);
        m_polarControl->writeThetaDriverSettings(*response);
        request->send(response.release());
    });

    m_server.on("/api/tuning/dump/rho", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        auto response = std_patch::make_unique<BufferedResponse>("application/json", kResponseBufferSize);
        m_polarControl->writeRhoDriverSettings(*response,
            request->hasParam("motionHealth") &&
            request->getParam("motionHealth")->value() == "true");
        request->send(response.release());
    });

    m_server.on("/api/tuning/dump/rho-companion", HTTP_GET, [this](AsyncWebServerRequest *request) {
        noteRequest(request);
        auto response = std_patch::make_unique<BufferedResponse>("application/json", kResponseBufferSize);
        m_polarControl->writeRhoCompanionDriverSettings(*response,
            request->hasParam("motionHealth") &&
            request->getParam("motionHealth")->value() == "true");
        request->send(response.release());
    });

    // SSE for position stream
    m_events.onConnect([this](AsyncEventSourceClient *client) {
        this->broadcastSinglePosition(client); // Send initial position
    });
    m_server.addHandler(&m_events);

    if (xTaskCreatePinnedToCore(fileCacheTask, "FileIndex", 4096, this, 1,
                                &m_fileCacheTask, 0) != pdPASS) {
        ErrorLog::instance().log("ERROR", "SD", "INDEX_TASK_FAILED",
                                 "Could not create pattern index task");
    }
    m_server.begin();
    LOG("Web server started on port 80\r\n");
}

void SisyphusWebServer::fileCacheTask(void* arg) {
    auto* self = static_cast<SisyphusWebServer*>(arg);
    for (;;) {
        uint32_t waitMs = kFileCacheThrottleMs;
        // Consume dirty BEFORE scanning so uploads/deletes during a scan are
        // never lost. Keep the previous complete index available until swap.
        // Keep the previous index while images are streaming. Rebuilding two
        // indexes alongside SD/TCP buffers fragments the same small heap and
        // makes both transfers and metadata work slower.
        if (self->m_fileListDirty.load() &&
            millis() - self->m_lastUploadActivity.load() >= 750) {
            self->m_fileScanActive.store(true);
            // Publish scan admission before checking image admission. Image
            // handlers increment their count before checking this flag.
            if (self->m_imageInflight.load() != 0 || self->m_uploadInflight.load() != 0) {
                self->m_fileScanActive.store(false);
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kFileCacheThrottleMs));
                continue;
            }
            self->m_fileListDirty.exchange(false);
            try {
                if (!self->updateFileListCache()) {
                    self->m_fileListDirty.store(true);
                    waitMs = 2000;
                }
            } catch (const std::bad_alloc&) {
                // Keep the complete previous index and retry after active
                // responses release memory. An index refresh must not reboot.
                self->m_fileListDirty.store(true);
                waitMs = 2000;
                LOG("Pattern index deferred: allocation unavailable\r\n");
            }
            self->m_fileScanActive.store(false);
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
    }
}

bool SisyphusWebServer::updateFileListCache() {
    const uint32_t started = millis();
    std::deque<FileEntry> newCache;
    std::deque<FileIndexEntry> newIndex;
    if (isSDCardReady()) {
        // Arduino SD.open/exists/openNextFile each repeat stat/open work.
        // Enumerate names once and stat only the two metadata records needed;
        // never open a .thr or .png just to learn its size or modification time.
        std::unique_ptr<DIR, decltype(&closedir)> root(opendir("/sd/patterns"), closedir);
        if (!root) {
            ErrorLog::instance().log("ERROR", "SD", "OPEN_DIR_FAILED",
                                     "Failed to index patterns directory", "/patterns");
            return false;
        }
        while (dirent* file = readdir(root.get())) {
            if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < 24576 ||
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 4096) {
                LOG("Pattern index deferred: memory pressure\r\n");
                return false;
            }
            const String name(file->d_name);
            vTaskDelay(pdMS_TO_TICKS(1));
            if (name.startsWith(".")) continue;
            bool nested = file->d_type == DT_DIR;
            if (file->d_type == DT_UNKNOWN) {
                struct stat info{};
                if (stat(("/sd/patterns/" + name).c_str(), &info) != 0) continue;
                nested = S_ISDIR(info.st_mode);
            }
            const String patternName = nested ? name + ".thr" : name;
            if (!validSimpleFilename(patternName, ".thr")) continue;
            const String base = patternName.substring(0, patternName.length() - 4);
            const String prefix = nested ? "/patterns/" + name + "/" : "/patterns/";
            struct stat patternInfo{};
            if (stat(("/sd" + prefix + patternName).c_str(), &patternInfo) != 0 ||
                !S_ISREG(patternInfo.st_mode)) continue;
            const String pngPath = prefix + base + ".png";
            struct stat imageInfo{};
            const bool hasImage = stat(("/sd" + pngPath).c_str(), &imageInfo) == 0 &&
                S_ISREG(imageInfo.st_mode);
            struct stat thumbnailInfo{};
            const String thumbnailPath = "/sd/patterns/" + base + "/" + base + ".thumb.png";
            const bool hasThumbnail = hasImage &&
                stat(thumbnailPath.c_str(), &thumbnailInfo) == 0 &&
                S_ISREG(thumbnailInfo.st_mode) && thumbnailInfo.st_size > 0;
            FileEntry entry{patternName, base, static_cast<size_t>(patternInfo.st_size),
                            patternInfo.st_mtime, hasImage,
                            hasImage ? imageInfo.st_mtime : 0,
                            hasImage ? static_cast<size_t>(imageInfo.st_size) : 0,
                            hasThumbnail ? static_cast<size_t>(thumbnailInfo.st_size) : 0,
                            hasThumbnail ? thumbnailInfo.st_mtime : 0, nested,
                            hasImage ? pngPath : ""};
            auto existing = std::find_if(newCache.begin(), newCache.end(),
                [&](const FileEntry& item) { return item.baseName == base; });
            // Preserve the resolver's historical preference for a nested
            // pattern when a legacy flat file has the same name.
            if (existing == newCache.end()) newCache.push_back(std::move(entry));
            else if (nested) *existing = std::move(entry);

        }
    }
    for (size_t i = 0; i < newCache.size(); ++i) newIndex.push_back({newCache[i].baseName, i});
    std::sort(newIndex.begin(), newIndex.end(), [](const FileIndexEntry& a, const FileIndexEntry& b) {
        return a.baseName.compareTo(b.baseName) < 0;
    });
    const size_t count = newCache.size();
    {
        SemaphoreGuard cacheLock(m_cacheMutex);
        m_fileCache.swap(newCache);
        m_fileIndex.swap(newIndex);
        m_fileListRevision.fetch_add(1);
    }
    LOG("Pattern index: %u files in %lums (metadata only)\r\n",
        static_cast<unsigned>(count), static_cast<unsigned long>(millis() - started));
    return true;
}

const SisyphusWebServer::FileEntry* SisyphusWebServer::findFileEntryByBase(const String& baseName) const {
    if (m_fileIndex.empty()) return nullptr;

    size_t left = 0;
    size_t right = m_fileIndex.size();
    while (left < right) {
        size_t mid = left + (right - left) / 2;
        int cmp = baseName.compareTo(m_fileIndex[mid].baseName);
        if (cmp == 0) {
            return &m_fileCache[m_fileIndex[mid].index];
        }
        if (cmp < 0) {
            right = mid;
        } else {
            left = mid + 1;
        }
    }
    return nullptr;
}

void SisyphusWebServer::loop() {
    updatePresenceAutomation();
    try {
        processPatternQueue();
    } catch (const std::bad_alloc&) {
        SemaphoreGuard stateLock(m_stateMutex);
        m_polarControl->stop();
        clearPlaybackLocked();
        ErrorLog::instance().log("ERROR", "MOTION", "NO_MEMORY",
                                 "Queued motion canceled: memory unavailable");
    }
    broadcastPosition();
}

static void formatPositionEvent(char* buffer, size_t capacity,
                                const PolarCord_t& position,
                                const PolarVelocity_t& velocity,
                                const CartesianCord_t& norm, bool clear) {
    // Radial and tangential components are perpendicular; no extra sin/cos
    // calls are needed to calculate Cartesian speed.
    const float speed = hypotf(velocity.rho, position.rho * velocity.theta);
    snprintf(buffer, capacity,
             "{\"x\":%.4f,\"y\":%.4f,\"r\":%.1f,\"t\":%.2f,\"vr\":%.2f,\"vt\":%.3f,\"vc\":%.2f%s}",
             norm.x, norm.y, position.rho, position.theta, velocity.rho,
             velocity.theta, speed, clear ? ",\"clear\":1" : "");
}

void SisyphusWebServer::updatePresenceAutomation() {
    if (m_ledController == nullptr) return;

    // getStatus can block on the detector mutex; never call it while holding
    // the LED lock or a manual brightness request could wait behind it.
    const bool havePresence = m_presenceSensor != nullptr;
    PresenceStatus status;
    if (havePresence) status = m_presenceSensor->getStatus();
    // Presence sampling and motion must never hold up manual light commands.
    // A busy LED just skips this fade tick; the next tick uses elapsed time.
    if (xSemaphoreTake(m_ledMutex, 0) != pdTRUE) return;
    m_ledController->update(millis());
    // A user-requested fade owns the output until it lands; presence resumes
    // on the next tick after it finishes.
    if (!havePresence || m_ledController->isFading()) {
        xSemaphoreGive(m_ledMutex);
        return;
    }
    const bool valid = status.available && status.receiving &&
        status.calibrated && !status.suppressed;
    if (!valid) {
        m_presenceAutomation.cancelFade();
        xSemaphoreGive(m_ledMutex);
        return;
    }
    m_presenceAutomation.setAction(m_presenceSensor->getAction());
    uint8_t nextBrightness = m_ledController->getBrightness();
    if (m_presenceAutomation.update(status.motion, nextBrightness,
                                    m_ledController->getTargetBrightness(),
                                    millis(), nextBrightness)) {
        m_ledController->setOutputBrightness(nextBrightness);
    }
    xSemaphoreGive(m_ledMutex);
}

void SisyphusWebServer::broadcastSinglePosition(AsyncEventSourceClient *client) {
    PolarCord_t position;
    PolarVelocity_t velocity;
    uint32_t completed;
    m_polarControl->getPositionSample(position, velocity, completed);
    const CartesianCord_t norm = PolarUtils::toNormalizedCartesian(
        position, m_polarControl->getMaxRho());
    char buffer[196];
    formatPositionEvent(buffer, sizeof(buffer), position, velocity, norm, false);
    try {
        if (client) client->send(buffer, "pos", millis());
        else m_events.send(buffer, "pos", millis());
    } catch (...) {
        // Position is replaceable; the next heartbeat repairs a dropped sample.
    }
}

void SisyphusWebServer::broadcastPosition() {
    const unsigned long now = millis();
    if (now - m_lastPosBroadcast < 66) return;
    m_lastPosBroadcast = now;
    if (m_events.count() == 0) return;
    // Do not build a seconds-long backlog of stale ball positions on slow
    // connections. Resume with the latest position when queued samples drain.
    if (m_events.avgPacketsWaiting() > 2) return;

    PolarCord_t position;
    PolarVelocity_t velocity;
    uint32_t completed;
    bool shouldClear;
    {
        SemaphoreGuard stateLock(m_stateMutex);
        m_polarControl->getPositionSample(position, velocity, completed);
        shouldClear = !m_firstPointCleared.load() && completed >= 1;
        if (shouldClear) m_firstPointCleared.store(true);
    }
    const CartesianCord_t norm = PolarUtils::toNormalizedCartesian(
        position, m_polarControl->getMaxRho());
    const bool unchanged = fabsf(norm.x - m_lastBroadcastX) < 0.0001f &&
        fabsf(norm.y - m_lastBroadcastY) < 0.0001f &&
        fabsf(velocity.rho - m_lastBroadcastVr) < 0.005f &&
        fabsf(velocity.theta - m_lastBroadcastVt) < 0.0005f;
    // Include stopped velocity and a one-second heartbeat even when XY is
    // unchanged. Otherwise the browser can retain a stale speed indefinitely.
    if (unchanged && !shouldClear && now - m_lastPosSent < 1000) return;

    char buffer[196];
    formatPositionEvent(buffer, sizeof(buffer), position, velocity, norm, shouldClear);
    try {
        if (m_events.send(buffer, "pos", now) == AsyncEventSource::DISCARDED) return;
        m_lastBroadcastX = norm.x;
        m_lastBroadcastY = norm.y;
        m_lastBroadcastVr = velocity.rho;
        m_lastBroadcastVt = velocity.theta;
        m_lastPosSent = now;
    } catch (...) {
        // Position is replaceable; the next heartbeat repairs a dropped sample.
    }
}

void SisyphusWebServer::processPatternQueue() {
    SemaphoreGuard stateLock(m_stateMutex);
    auto state = m_polarControl->getState();

    // Non-pattern commands use a single replaceable slot. Pointer drags can
    // therefore update the destination faster than the mechanism can move
    // without building an unbounded queue of stale waypoints.
    if (m_pendingMotion != PendingMotion::NONE) {
        const PendingMotion pending = m_pendingMotion;
        const bool jogReady = pending == PendingMotion::MANUAL_JOG &&
            (state == PolarControl::IDLE || state == PolarControl::INITIALIZED ||
             state == PolarControl::HOMING_FAILED);
        if (state != PolarControl::IDLE && !jogReady) return;

        const float theta = m_pendingManualTheta;
        const float rho = m_pendingManualRho;
        const float jogTheta = m_pendingJogTheta;
        const float jogRho = m_pendingJogRho;
        const float thetaSegmentTarget = m_pendingThetaSegmentTarget;
        const float rhoSegmentTarget = m_pendingRhoSegmentTarget;
        m_pendingMotion = PendingMotion::NONE;

        bool started = false;
        switch (pending) {
            case PendingMotion::MANUAL:
                started = m_polarControl->moveTo(theta, rho);
                m_activeMotion = MotionOwner::MANUAL;
                break;
            case PendingMotion::MANUAL_JOG:
                started = m_polarControl->jogRelative(jogTheta, jogRho);
                m_activeMotion = MotionOwner::MANUAL;
                break;
            case PendingMotion::THETA_CONTINUOUS:
                started = m_polarControl->testThetaContinuous();
                m_activeMotion = MotionOwner::TUNING;
                break;
            case PendingMotion::THETA_STRESS:
                started = m_polarControl->testThetaStress();
                m_activeMotion = MotionOwner::TUNING;
                break;
            case PendingMotion::THETA_SEGMENT:
                started = m_polarControl->testThetaSegment(thetaSegmentTarget);
                m_activeMotion = MotionOwner::TUNING;
                break;
            case PendingMotion::RHO_CONTINUOUS:
                started = m_polarControl->testRhoContinuous();
                m_activeMotion = MotionOwner::TUNING;
                break;
            case PendingMotion::RHO_STRESS:
                started = m_polarControl->testRhoStress();
                m_activeMotion = MotionOwner::TUNING;
                break;
            case PendingMotion::RHO_SEGMENT:
                started = m_polarControl->testRhoSegment(rhoSegmentTarget);
                m_activeMotion = MotionOwner::TUNING;
                break;
            case PendingMotion::NONE:
                break;
        }
        if (!started) {
            m_activeMotion = MotionOwner::NONE;
            ErrorLog::instance().log("ERROR", "MOTION", "START_FAILED",
                                     "Queued motion could not be started");
        }
        return;
    }


    if (state != PolarControl::IDLE) {
        // An unhomed manual jog returns to its entry state when the planner
        // finishes. Clear its owner there too; otherwise Set Home remains
        // blocked even though the jog is complete and motion is stopped.
        if ((state == PolarControl::INITIALIZED ||
             state == PolarControl::HOMING_FAILED) &&
            m_activeMotion == MotionOwner::MANUAL) {
            m_activeMotion = MotionOwner::NONE;
        }
        return;
    }

    // Handle clearing completion for single pattern mode (not playlist)
    if (!m_playlistMode && m_runningClearing && m_pendingPattern.length() > 0) {
        LOG("Single pattern: Starting %s after clearing\r\n", m_pendingPattern.c_str());
        m_polarControl->resetTheta();
        m_currentPattern = m_pendingPattern;
        m_firstPointCleared = false;

        beginPatternLocked(m_pendingPattern);
        m_pendingPattern = "";
        m_runningClearing = false;
        m_singlePatternClearing = false;
        m_activeClearingPattern = CLEARING_NONE;
        return;
    }

    // Handle single pattern queue
    if (m_hasQueuedPattern) {
        if (m_singlePatternClearing && m_selectedClearing != CLEARING_NONE) {
            LOG("Single pattern: Running clearing before %s\r\n", m_queuedPattern.c_str());
            m_pendingPattern = m_queuedPattern;
            m_runningClearing = true;
            m_hasQueuedPattern = false;
            m_queuedPattern = "";

            m_polarControl->resetTheta();

            ClearingPattern pattern = m_selectedClearing;
            if (pattern == CLEARING_RANDOM) pattern = getRandomClearingPattern();

            if (!m_polarControl->startClearing(std_patch::make_unique<ClearingPatternGen>(pattern, m_polarControl->getMaxRho()))) {
                m_runningClearing = false;
                m_singlePatternClearing = false;
                m_activeClearingPattern = CLEARING_NONE;
                beginPatternLocked(m_pendingPattern);
                m_pendingPattern = "";
            } else {
                m_activeClearingPattern = pattern;
                m_activeMotion = MotionOwner::PATTERN;
            }
            return;
        }

        m_currentPattern = m_queuedPattern;
        m_firstPointCleared = false;
        m_polarControl->resetTheta();
        beginPatternLocked(m_queuedPattern);
        m_hasQueuedPattern = false;
        m_queuedPattern = "";
        return;
    }

    // Handle playlist mode
    if (m_playlistMode) {
        if (m_runningClearing && m_pendingPattern.length() > 0) {
            LOG("Playlist: Starting pattern after clearing: %s\r\n", m_pendingPattern.c_str());
            m_polarControl->resetTheta();
            m_currentPattern = m_pendingPattern;
            m_firstPointCleared = false;
            beginPatternLocked(m_pendingPattern);
            m_pendingPattern = "";
            m_runningClearing = false;
            m_activeClearingPattern = CLEARING_NONE;
            return;
        }

        if (m_playlist.hasNext()) {
            NextPatternResult next = m_playlist.getNextPattern();
            if (next.filename.length() > 0) {
                if (next.needsClearing && next.clearingPattern != CLEARING_NONE) {
                    m_pendingPattern = next.filename;
                    m_runningClearing = true;
                    m_polarControl->resetTheta();

                    ClearingPattern pattern = next.clearingPattern;
                    if (pattern == CLEARING_RANDOM) pattern = getRandomClearingPattern();

                    if (!m_polarControl->startClearing(std_patch::make_unique<ClearingPatternGen>(pattern, m_polarControl->getMaxRho()))) {
                        m_runningClearing = false;
                        m_activeClearingPattern = CLEARING_NONE;
                        beginPatternLocked(next.filename);
                    } else {
                        m_activeClearingPattern = pattern;
                        m_activeMotion = MotionOwner::PATTERN;
                    }
                } else {
                    LOG("Playlist: Starting pattern: %s\r\n", next.filename.c_str());
                    m_polarControl->resetTheta();
                    m_currentPattern = next.filename;
                    m_firstPointCleared = false;
                    beginPatternLocked(next.filename);
                }
            }
        } else {
            LOG("Playlist complete\r\n");
            m_playlistMode = false;
            m_runningClearing = false;
            m_pendingPattern = "";
            m_activeClearingPattern = CLEARING_NONE;
            m_activeMotion = MotionOwner::NONE;
        }
    } else {
        m_activeMotion = MotionOwner::NONE;
        m_currentPattern = "";
    }
}

bool SisyphusWebServer::beginPatternLocked(const String& filename) {
    const String path = resolvePatternPath(filename);
    if (path.isEmpty() || !m_polarControl->loadAndRunFile(path)) {
        ErrorLog::instance().log("ERROR", "FILE", "START_FAILED",
                                 "Pattern could not be started", filename.c_str());
        m_currentPattern = "";
        m_activeMotion = MotionOwner::NONE;
        return false;
    }
    m_currentPattern = filename;
    m_activeMotion = MotionOwner::PATTERN;
    return true;
}

void SisyphusWebServer::clearPlaybackLocked() {
    m_pendingMotion = PendingMotion::NONE;
    m_hasQueuedPattern = false;
    m_queuedPattern = "";
    m_currentPattern = "";
    m_pendingPattern = "";
    m_runningClearing = false;
    m_singlePatternClearing = false;
    m_selectedClearing = CLEARING_NONE;
    m_activeClearingPattern = CLEARING_NONE;
    m_playlistMode = false;
    m_firstPointCleared = false;
}

bool SisyphusWebServer::prepareReplacementLocked() {
    const auto state = m_polarControl->getState();
    switch (state) {
        case PolarControl::IDLE:
            clearPlaybackLocked();
            return true;
        case PolarControl::RUNNING:
        case PolarControl::PAUSED:
        case PolarControl::STOPPING:
        case PolarControl::CLEARING:
        case PolarControl::PREPARING:
            clearPlaybackLocked();
            return m_polarControl->stop();
        default:
            return false;
    }
}

bool SisyphusWebServer::prepareManualJogLocked() {
    const auto state = m_polarControl->getState();
    if (state == PolarControl::INITIALIZED ||
        state == PolarControl::HOMING_FAILED) {
        clearPlaybackLocked();
        return true;
    }
    return prepareReplacementLocked();
}

bool SisyphusWebServer::queueTuningTestLocked(PendingMotion motion) {
#ifdef SISYPHUS_RHO_COMMISSIONING
    if (!m_rhoCommissioningMode.load()) return false;
#endif
    if (!prepareReplacementLocked()) return false;
    m_pendingMotion = motion;
    return true;
}

String SisyphusWebServer::getStateString() {
    return JsonHelpers::getStateString(m_polarControl->getState());
}

void SisyphusWebServer::writeStatusJSON(Print& out) {
    String clearingPattern = "";
    if (m_polarControl->getState() == PolarControl::CLEARING) {
        clearingPattern = getClearingPatternName(m_activeClearingPattern);
    }
    JsonHelpers::writeStatusJSON(out, m_polarControl, m_ledController,
        m_presenceSensor, m_currentPattern,
        clearingPattern, m_hasQueuedPattern ? m_queuedPattern : m_pendingPattern,
        m_fileListRevision.load());
}

void SisyphusWebServer::writeFileListJSON(Print& out) {
    JsonHelpers::writeFileListJSON(out);
}

void SisyphusWebServer::writeSystemInfoJSON(Print& out) {
    JsonHelpers::writeSystemInfoJSON(out);
}

void SisyphusWebServer::handleRoot(AsyncWebServerRequest *request) {
    request->send(new StaticContentResponse("text/html",
                  reinterpret_cast<const uint8_t *>(WEB_UI_HTML),
                  sizeof(WEB_UI_HTML) - 1));
}

void SisyphusWebServer::handleStatus(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    auto response = std_patch::make_unique<BufferedResponse>("application/json", 0, true);
    writeStatusJSON(*response);
    request->send(response.release());
}

void SisyphusWebServer::handlePresenceGet(AsyncWebServerRequest *request) {
    auto response = std_patch::make_unique<BufferedResponse>("application/json");
    JsonHelpers::writePresenceJSON(*response, m_presenceSensor);
    request->send(response.release());
}

void SisyphusWebServer::handlePresenceCalibrate(AsyncWebServerRequest *request) {
    if (m_presenceSensor == nullptr) {
        request->send(503, "application/json",
            "{\"success\":false,\"message\":\"Presence sensing is unavailable\"}");
        return;
    }

    const PresenceStatus status = m_presenceSensor->getStatus();
    if (!status.available) {
        request->send(503, "application/json",
            "{\"success\":false,\"message\":\"CSI initialization failed\"}");
        return;
    }
    if (status.suppressed) {
        request->send(409, "application/json",
            status.memoryLimited
                ? "{\"success\":false,\"message\":\"Presence paused to preserve controller memory; retry after other activity finishes\"}"
                : "{\"success\":false,\"message\":\"Wait for table motion and the five-second settling period to finish\"}");
        return;
    }
    if (!status.receiving) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"No fresh CSI samples; check the Wi-Fi connection and gateway\"}");
        return;
    }
    if (!m_presenceSensor->startCalibration()) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Calibration could not start\"}");
        return;
    }

    request->send(202, "application/json",
        "{\"success\":true,\"message\":\"Keep the room empty and the table still until calibration reaches 100%\"}");
}

void SisyphusWebServer::handlePresenceSettingsGet(
    AsyncWebServerRequest *request) {
    if (m_presenceSensor == nullptr) {
        request->send(503, "application/json",
            "{\"success\":false,\"message\":\"Presence sensing is unavailable\"}");
        return;
    }

    const char* action = m_presenceSensor->getAction() ==
        PresenceAction::FADE_LIGHT_ON ? "fade_light_on" : "none";
    auto response = std_patch::make_unique<BufferedResponse>("application/json");
    response->printf(
        "{\"action\":\"%s\",\"fadeDurationMs\":%lu,\"targetBrightness\":%u}",
        action,
        static_cast<unsigned long>(PresenceAutomation::kFadeDurationMs),
        static_cast<unsigned>(JsonHelpers::brightnessPercent(
            m_ledController->getTargetBrightness())));
    request->send(response.release());
}

void SisyphusWebServer::handlePresenceSettingsSet(
    AsyncWebServerRequest *request) {
    if (m_presenceSensor == nullptr) {
        request->send(503, "application/json",
            "{\"success\":false,\"message\":\"Presence sensing is unavailable\"}");
        return;
    }
    if (!request->hasParam("action", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing action parameter\"}");
        return;
    }

    const String value = request->getParam("action", true)->value();
    PresenceAction action;
    if (value == "none") {
        action = PresenceAction::NONE;
    } else if (value == "fade_light_on") {
        action = PresenceAction::FADE_LIGHT_ON;
    } else {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Action must be none or fade_light_on\"}");
        return;
    }

    if (!m_presenceSensor->setAction(action)) {
        request->send(500, "application/json",
            "{\"success\":false,\"message\":\"Presence action could not be saved\"}");
        return;
    }
    {
        SemaphoreGuard ledLock(m_ledMutex);
        m_presenceAutomation.setAction(action);
    }
    request->send(200, "application/json", "{\"success\":true}");
}

void SisyphusWebServer::handleErrors(AsyncWebServerRequest *request) {
    auto response = std_patch::make_unique<BufferedResponse>("application/json");
    ErrorLog::instance().writeJson(*response);
    request->send(response.release());
}

void SisyphusWebServer::handleErrorsClear(AsyncWebServerRequest *request) {
    ErrorLog::instance().clear();
    request->send(200, "application/json", "{\"success\":true}");
}

static bool parseLogQuery(AsyncWebServerRequest *request, uint32_t& since, size_t& limit) {
    since = 0;
    limit = 64;
    if (request->hasParam("since") &&
        !parseStrictUint32(request->getParam("since")->value(), since)) {
        return false;
    }
    if (request->hasParam("limit")) {
        int parsedLimit = 0;
        if (!parseStrictInt(request->getParam("limit")->value(), parsedLimit) ||
            parsedLimit < 1 || parsedLimit > 64) {
            return false;
        }
        limit = static_cast<size_t>(parsedLimit);
    }
    return true;
}

void SisyphusWebServer::handleLogs(AsyncWebServerRequest *request) {
    uint32_t since = 0;
    size_t limit = 64;
    if (!parseLogQuery(request, since, limit)) {
        request->send(400, "application/json",
                      "{\"success\":false,\"message\":\"Invalid since or limit\"}");
        return;
    }
    auto response = std_patch::make_unique<BufferedResponse>("application/json", 1024);
    response->addHeader("Cache-Control", "no-store");
    RuntimeLog::instance().writeJson(*response, since, limit);
    request->send(response.release());
}

void SisyphusWebServer::handleLogsText(AsyncWebServerRequest *request) {
    uint32_t since = 0;
    size_t limit = 64;
    if (!parseLogQuery(request, since, limit)) {
        request->send(400, "text/plain", "Invalid since or limit\n");
        return;
    }
    auto response = std_patch::make_unique<BufferedResponse>("text/plain", 1024);
    response->addHeader("Cache-Control", "no-store");
    RuntimeLog::instance().writeText(*response, since, limit);
    request->send(response.release());
}

void SisyphusWebServer::handleLogsClear(AsyncWebServerRequest *request) {
    RuntimeLog::instance().clear();
    request->send(200, "application/json", "{\"success\":true}");
}

void SisyphusWebServer::handlePatternStart(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (!requirePatternStorage(request)) return;

    if (!request->hasParam("file", true)) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing parameters\"}");
        return;
    }

    String file = request->getParam("file", true)->value();
    String filePath = resolvePatternPath(file);

    if (filePath.length() == 0) {
        request->send(404, "application/json", "{\"success\":false,\"message\":\"File not found\"}");
        return;
    }

    {
        SemaphoreGuard cacheLock(m_cacheMutex);
        const FileEntry* entry = findFileEntryByBase(file.substring(0, file.length() - 4));
        if (entry && entry->size == 0) {
            request->send(422, "application/json",
                "{\"success\":false,\"message\":\"Pattern file is empty\"}");
            return;
        }
    }

    // Check for clearing parameter
    bool useClearing = false;
    ClearingPattern selectedClearing = CLEARING_NONE;
    if (request->hasParam("clearing", true)) {
        int clearingType = 0;
        if (!parseStrictInt(request->getParam("clearing", true)->value(), clearingType) ||
            clearingType < CLEARING_NONE || clearingType > CLEARING_RANDOM) {
            request->send(400, "application/json",
                "{\"success\":false,\"message\":\"Invalid clearing pattern\"}");
            return;
        }
        if (clearingType > 0) {
            useClearing = true;
            selectedClearing = static_cast<ClearingPattern>(clearingType);
        }
    }

    if (!prepareReplacementLocked()) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Motion is unavailable until homing is complete\"}");
        return;
    }

    m_singlePatternClearing = useClearing;
    m_selectedClearing = selectedClearing;
    m_queuedPattern = file;
    m_hasQueuedPattern = true;

    request->send(202, "application/json", "{\"success\":true,\"message\":\"Pattern queued\"}");
}

void SisyphusWebServer::handlePatternStop(AsyncWebServerRequest *request) {
    handlePlaybackStop(request);
}

void SisyphusWebServer::handlePlaybackStop(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    clearPlaybackLocked();
    m_activeMotion = MotionOwner::NONE;
    const auto state = m_polarControl->getState();
    const bool alreadyStopped = state == PolarControl::UNINITIALIZED ||
        state == PolarControl::INITIALIZED || state == PolarControl::IDLE ||
        state == PolarControl::HOMING_REVIEW || state == PolarControl::HOMING_FAILED;
    const bool success = alreadyStopped || m_polarControl->stop();
    request->send(success ? 200 : 500, "application/json",
        success ? "{\"success\":true}" :
                  "{\"success\":false,\"message\":\"Failed to stop playback\"}");
}

void SisyphusWebServer::handleMotionStop(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    clearPlaybackLocked();
    m_activeMotion = MotionOwner::NONE;
    const auto state = m_polarControl->getState();
    const bool activeMotion = state == PolarControl::RUNNING ||
        state == PolarControl::PAUSED || state == PolarControl::STOPPING ||
        state == PolarControl::CLEARING || state == PolarControl::PREPARING ||
        state == PolarControl::HOMING || state == PolarControl::HOMING_REVIEW;
    if (activeMotion) {
        // Abort also invalidates a result that reached review while the
        // request was in flight. Cancel homing STEP/DIR and any UART motion,
        // and discard generated planner events rather than decelerating.
        m_polarControl->emergencyStop();
    }

    request->send(200, "application/json",
        activeMotion
            ? "{\"success\":true,\"emergency\":true,\"requiresHoming\":true}"
            : "{\"success\":true,\"emergency\":false}");
}

static void writeAxisStepTiming(Print& out, const AxisStepTimingTelemetry& timing) {
    out.printf(
        "{\"valid\":%s,\"steps\":%lu,\"outliers\":%lu,\"maxLateUs\":%lu,"
        "\"maxIntervalErrorUs\":%lu,\"last\":{\"epoch\":%lu,"
        "\"scheduledMicros\":%lu,\"actualMicros\":%lu,"
        "\"plannedIntervalUs\":%lu,\"actualIntervalUs\":%lu,\"intervalValid\":%s}}",
        timing.valid ? "true" : "false",
        static_cast<unsigned long>(timing.stepCount),
        static_cast<unsigned long>(timing.outlierCount),
        static_cast<unsigned long>(timing.maxLatenessUs),
        static_cast<unsigned long>(timing.maxAbsIntervalErrorUs),
        static_cast<unsigned long>(timing.outlierEpoch),
        static_cast<unsigned long>(timing.outlierScheduledMicros),
        static_cast<unsigned long>(timing.outlierActualMicros),
        static_cast<unsigned long>(timing.outlierPlannedIntervalUs),
        static_cast<unsigned long>(timing.outlierActualIntervalUs),
        timing.outlierIntervalValid ? "true" : "false");
}

void SisyphusWebServer::handleMotionTelemetry(AsyncWebServerRequest *request) {
    const PolarCord_t position = m_polarControl->getActualPosition();
    const PolarVelocity_t velocity = m_polarControl->getActualVelocity();
    PlannerTelemetry telemetry;
    m_polarControl->getTelemetry(telemetry);
    const uint32_t sampleMicros = micros();

    auto response = std_patch::make_unique<BufferedResponse>("application/json", 1536);
    response->print("{\"state\":\"");
    response->print(getStateString());
    response->printf(
        "\",\"millis\":%lu,\"micros\":%lu,\"position\":{\"rho\":%.4f,\"theta\":%.6f},"
        "\"velocity\":{\"rho\":%.4f,\"theta\":%.6f},"
        "\"planner\":{\"queueDepth\":%lu,\"minQueueDepth\":%lu,"
        "\"underruns\":%lu,\"maxConsecutiveUnderruns\":%lu,"
        "\"completedCount\":%lu,\"timerActive\":%s,\"running\":%s},"
        "\"stepMotion\":{\"epoch\":%lu,\"startMicros\":%lu,"
        "\"stopMicros\":%lu,\"active\":%s}",
        static_cast<unsigned long>(sampleMicros / 1000U),
        static_cast<unsigned long>(sampleMicros), position.rho, position.theta,
        velocity.rho, velocity.theta,
        static_cast<unsigned long>(telemetry.queueDepth),
        static_cast<unsigned long>(telemetry.minQueueDepth),
        static_cast<unsigned long>(telemetry.underruns),
        static_cast<unsigned long>(telemetry.maxConsecutiveUnderruns),
        static_cast<unsigned long>(telemetry.completedCount),
        telemetry.timerActive ? "true" : "false",
        telemetry.running ? "true" : "false",
        static_cast<unsigned long>(telemetry.stepMotionEpoch),
        static_cast<unsigned long>(telemetry.lastStepMotionStartUs),
        static_cast<unsigned long>(telemetry.lastStepMotionStopUs),
        telemetry.stepMotionActive ? "true" : "false");
    // Fixed-size summary, not a per-pulse stream. "last" is the latest
    // outlier, not necessarily the latest pulse; counter deltas expose missed
    // outliers between HTTP samples. Require each valid flag before use.
    response->printf(
        ",\"stepTiming\":{\"source\":\"hardware-timer-tg1-t0\",\"thresholdUs\":%lu,\"callback\":{\"valid\":%s,"
        "\"gaps\":%lu,\"maxGapUs\":%lu,\"lastMicros\":%lu,\"lastGapUs\":%lu},\"theta\":",
        static_cast<unsigned long>(2U * STEP_TIMER_PERIOD_US),
        telemetry.callbackTimingValid ? "true" : "false",
        static_cast<unsigned long>(telemetry.callbackGapCount),
        static_cast<unsigned long>(telemetry.maxCallbackGapUs),
        static_cast<unsigned long>(telemetry.lastCallbackGapMicros),
        static_cast<unsigned long>(telemetry.lastCallbackGapUs));
    writeAxisStepTiming(*response, telemetry.thetaStepTiming);
    response->print(",\"rho\":");
    writeAxisStepTiming(*response, telemetry.rhoStepTiming);
    response->print("}");
#ifdef SISYPHUS_BENCH_MOTION_TEST
    response->print(",\"benchMotionTest\":true");
#else
    response->print(",\"benchMotionTest\":false");
#endif
#ifdef SISYPHUS_THETA_COMMISSIONING
    response->print(",\"commissioningAxis\":\"theta\"");
#elif defined(SISYPHUS_RHO_COMMISSIONING)
    if (m_rhoCommissioningMode.load()) {
        response->print(",\"commissioningAxis\":\"rho\",\"rhoServiceMode\":\"commissioning\"");
    } else {
        response->print(",\"commissioningAxis\":null,\"rhoServiceMode\":\"manual\"");
    }
#else
    response->print(",\"commissioningAxis\":null");
#endif
    response->print("}");
    request->send(response.release());
}

void SisyphusWebServer::handleRhoServiceModeGet(
        AsyncWebServerRequest *request) {
#ifndef SISYPHUS_RHO_COMMISSIONING
    request->send(404, "application/json",
        "{\"success\":false,\"message\":\"RHO service mode is unavailable\"}");
#else
    const bool commissioning = m_rhoCommissioningMode.load();
    auto response = std_patch::make_unique<BufferedResponse>(
        "application/json", 192);
    response->printf(
        "{\"success\":true,\"mode\":\"%s\",\"originConfirmed\":%s,"
        "\"thetaLockedOut\":true,\"patternsLockedOut\":true}",
        commissioning ? "commissioning" : "manual",
        commissioning ? "true" : "false");
    request->send(response.release());
#endif
}

void SisyphusWebServer::handleRhoServiceModeSet(
        AsyncWebServerRequest *request) {
#ifndef SISYPHUS_RHO_COMMISSIONING
    request->send(404, "application/json",
        "{\"success\":false,\"message\":\"RHO service mode is unavailable\"}");
#else
    if (!request->hasParam("mode", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing mode\"}");
        return;
    }
    const String mode = request->getParam("mode", true)->value();
    if (mode != "manual" && mode != "commissioning") {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Mode must be manual or commissioning\"}");
        return;
    }

    bool confirmOrigin = false;
    if (mode == "commissioning" &&
        (!request->hasParam("confirmOrigin", true) ||
         !parseStrictBool(
             request->getParam("confirmOrigin", true)->value(), confirmOrigin) ||
         !confirmOrigin)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Commissioning requires explicit physical-origin confirmation\"}");
        return;
    }

    SemaphoreGuard stateLock(m_stateMutex);
    const auto state = m_polarControl->getState();
    if (state != PolarControl::INITIALIZED && state != PolarControl::IDLE &&
        state != PolarControl::HOMING_FAILED) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Stop motion before changing RHO service mode\"}");
        return;
    }
    clearPlaybackLocked();
    m_activeMotion = MotionOwner::NONE;

    if (mode == "commissioning") {
        const DriverAvailability drivers = m_polarControl->getDriverAvailability();
        if (!drivers.rho) {
            request->send(409, "application/json",
                "{\"success\":false,\"message\":\"The main RHO driver is required to set home\"}");
            return;
        }
        m_polarControl->assumeBenchTestOrigin();
        m_rhoCommissioningMode.store(true);
        request->send(200, "application/json",
            "{\"success\":true,\"mode\":\"commissioning\",\"originConfirmed\":true}");
        return;
    }

    m_rhoCommissioningMode.store(false);
    m_polarControl->enterRhoManualServiceMode();
    request->send(200, "application/json",
        "{\"success\":true,\"mode\":\"manual\",\"originConfirmed\":false}");
#endif
}

void SisyphusWebServer::handlePatternPause(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (m_activeMotion != MotionOwner::PATTERN) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"No pattern is running\"}");
        return;
    }
    bool success = m_polarControl->pause();

    if (success) {
        request->send(200, "application/json", "{\"success\":true}");
    } else {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Cannot pause\"}");
    }
}

void SisyphusWebServer::handlePatternResume(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (m_activeMotion != MotionOwner::PATTERN) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"No pattern is paused\"}");
        return;
    }
    bool success = m_polarControl->resume();

    if (success) {
        request->send(200, "application/json", "{\"success\":true}");
    } else {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Cannot resume\"}");
    }
}

void SisyphusWebServer::handleManualMove(AsyncWebServerRequest *request) {
#if defined(SISYPHUS_THETA_COMMISSIONING) || defined(SISYPHUS_RHO_COMMISSIONING)
    request->send(409, "application/json",
        "{\"success\":false,\"message\":\"Manual motion is disabled in commissioning mode\"}");
    return;
#endif
    if (!request->hasParam("theta", true) || !request->hasParam("rho", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing theta or rho\"}");
        return;
    }

    float theta = 0.0f;
    float rho = 0.0f;
    if (!parseStrictFloat(request->getParam("theta", true)->value(), theta) ||
        !parseStrictFloat(request->getParam("rho", true)->value(), rho) ||
        theta < -PI || theta > PI || rho < 0.0f || rho > m_polarControl->getMaxRho()) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Target is outside the table\"}");
        return;
    }

    SemaphoreGuard stateLock(m_stateMutex);
    if (!prepareReplacementLocked()) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Manual motion is unavailable until homing is complete\"}");
        return;
    }

    m_pendingManualTheta = theta;
    m_pendingManualRho = rho;
    m_pendingMotion = PendingMotion::MANUAL;
    request->send(202, "application/json",
        "{\"success\":true,\"message\":\"Manual target queued\"}");
}

void SisyphusWebServer::handleManualSetHome(
        AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    const auto state = m_polarControl->getState();
    if ((state != PolarControl::INITIALIZED && state != PolarControl::IDLE &&
         state != PolarControl::HOMING_FAILED) ||
        m_activeMotion != MotionOwner::NONE ||
        m_pendingMotion != PendingMotion::NONE) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Stop motion before setting home\"}");
        return;
    }

    const DriverAvailability drivers = m_polarControl->getDriverAvailability();
    bool requiredDriversReady = drivers.rhoAxis();
#ifndef SISYPHUS_RHO_COMMISSIONING
    requiredDriversReady = requiredDriversReady && drivers.thetaAxis();
#endif
    if (!requiredDriversReady) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Required motor drivers are unavailable\"}");
        return;
    }
    clearPlaybackLocked();
    if (!m_polarControl->setCurrentPositionAsHome()) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Could not set home in the current state\"}");
        return;
    }
#ifdef SISYPHUS_RHO_COMMISSIONING
    m_rhoCommissioningMode.store(true);
#endif
    request->send(200, "application/json",
        "{\"success\":true,\"homed\":true,\"rho\":0,\"theta\":0}");
}

void SisyphusWebServer::handleManualJog(AsyncWebServerRequest *request) {
#ifdef SISYPHUS_THETA_COMMISSIONING
    request->send(409, "application/json",
        "{\"success\":false,\"message\":\"Manual motion is disabled in commissioning mode\"}");
    return;
#elif defined(SISYPHUS_RHO_COMMISSIONING)
    if (m_rhoCommissioningMode.load()) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Switch to manual RHO mode before jogging\"}");
        return;
    }
#endif
    if (!request->hasParam("axis", true) ||
        !request->hasParam("amount", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing axis or amount\"}");
        return;
    }

    const String axis = request->getParam("axis", true)->value();
    float amount = 0.0f;
    if (!parseStrictFloat(request->getParam("amount", true)->value(), amount) ||
        (axis != "theta" && axis != "rho") ||
        (fabsf(amount) != 1.0f && fabsf(amount) != 10.0f &&
         fabsf(amount) != 100.0f)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Jog must be +/-1, 10, or 100 on theta or rho\"}");
        return;
    }
#ifdef SISYPHUS_RHO_COMMISSIONING
    if (axis != "rho") {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Theta remains locked out in RHO service firmware\"}");
        return;
    }
#endif

    const DriverAvailability drivers = m_polarControl->getDriverAvailability();
    if ((axis == "theta" && !drivers.thetaAxis()) ||
        (axis == "rho" && !drivers.rhoAxis())) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"That motor driver is disconnected\"}");
        return;
    }

    SemaphoreGuard stateLock(m_stateMutex);
    if (!prepareManualJogLocked()) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Manual jog is unavailable while homing or awaiting confirmation\"}");
        return;
    }

    m_pendingJogTheta = axis == "theta" ? amount * PI / 180.0f : 0.0f;
    m_pendingJogRho = axis == "rho" ? amount : 0.0f;
    m_pendingMotion = PendingMotion::MANUAL_JOG;
    request->send(202, "application/json",
        "{\"success\":true,\"message\":\"Manual jog queued\"}");
}

void SisyphusWebServer::handleHome(AsyncWebServerRequest *request) {
#if defined(SISYPHUS_THETA_COMMISSIONING)
    request->send(409, "application/json",
        "{\"success\":false,\"message\":\"Homing is disabled in commissioning mode\"}");
    return;
#elif defined(SISYPHUS_RHO_COMMISSIONING)
    if (!m_rhoCommissioningMode.load()) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Confirm the physical origin before bounded homing\"}");
        return;
    }
#else
    if (!Config::kEnableUnknownPositionRhoHoming) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Unknown-position RHO homing is not qualified; use manual Set Home after verifying physical zero\"}");
        return;
    }
#endif
    SemaphoreGuard stateLock(m_stateMutex);
    auto state = m_polarControl->getState();
    if (state != PolarControl::IDLE && state != PolarControl::INITIALIZED &&
        state != PolarControl::HOMING_FAILED) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"System must be idle to home\"}");
        return;
    }

    clearPlaybackLocked();
    m_activeMotion = MotionOwner::NONE;
    LOG("Web request: Homing device...\r\n");
#ifdef SISYPHUS_RHO_COMMISSIONING
    if (!m_polarControl->home(true)) {
#else
    if (!m_polarControl->home()) {
#endif
        request->send(500, "application/json",
            "{\"success\":false,\"message\":\"Could not start homing\"}");
        return;
    }

    request->send(202, "application/json",
        "{\"success\":true,\"message\":\"Homing started\"}");
}

void SisyphusWebServer::handleKnownPositionHome(
        AsyncWebServerRequest *request) {
#ifndef SISYPHUS_RHO_COMMISSIONING
    request->send(404, "application/json",
        "{\"success\":false,\"message\":\"Known-position homing is commissioning-only\"}");
#else
    float rhoStartMm = 0.0f;
    float companionStartMm = 0.0f;
    bool confirmed = false;
    if (!request->hasParam("rhoStartMm", true) ||
        !parseStrictFloat(
            request->getParam("rhoStartMm", true)->value(), rhoStartMm) ||
        !request->hasParam("companionStartMm", true) ||
        !parseStrictFloat(
            request->getParam("companionStartMm", true)->value(),
            companionStartMm) ||
        !request->hasParam("confirmKnownPositions", true) ||
        !parseStrictBool(
            request->getParam("confirmKnownPositions", true)->value(),
            confirmed) ||
        !confirmed || rhoStartMm < -1.0f || rhoStartMm > Config::kRhoKnownStartMaximumMm ||
        companionStartMm < -1.0f || companionStartMm > Config::kRhoKnownStartMaximumMm) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Explicit known RHO positions within the qualification travel limit are required\"}");
        return;
    }

    SemaphoreGuard stateLock(m_stateMutex);
    const auto state = m_polarControl->getState();
    if (state != PolarControl::INITIALIZED && state != PolarControl::IDLE &&
        state != PolarControl::HOMING_FAILED) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Stop motion before known-position homing\"}");
        return;
    }
    if (m_rhoCommissioningMode.load() &&
        std::fabs(rhoStartMm - companionStartMm) <= 0.05f) {
        const PolarCord_t position = m_polarControl->getActualPosition();
        if (std::fabs(position.rho - rhoStartMm) > 0.05f) {
            request->send(409, "application/json",
                "{\"success\":false,\"message\":\"Logical rho position does not match the known synchronized start\"}");
            return;
        }
    }
    clearPlaybackLocked();
    m_activeMotion = MotionOwner::NONE;
    if (!m_polarControl->homeFromKnownRhoPositions(
            rhoStartMm, companionStartMm)) {
        request->send(500, "application/json",
            "{\"success\":false,\"message\":\"Could not start known-position homing\"}");
        return;
    }
    request->send(202, "application/json",
        "{\"success\":true,\"message\":\"Known-position homing started\"}");
#endif
}

void SisyphusWebServer::handleHomingTrace(
        AsyncWebServerRequest *request) {
    // The host polls at 4 Hz and deduplicates by total sample index. A smaller
    // response avoids retaining a 49 KB stream plus a full ring copy while
    // asynchronous responses or normal-motion queue allocations remain live.
    constexpr size_t kTraceCapacity = 256;
    std::unique_ptr<HomingTraceSample[]> samples(
        new (std::nothrow) HomingTraceSample[kTraceCapacity]);
    if (!samples) {
        request->send(503, "application/json",
            "{\"success\":false,\"message\":\"Trace buffer allocation failed\"}");
        return;
    }
    size_t total = 0;
    const size_t count = m_polarControl->getHomingTrace(
        samples.get(), kTraceCapacity, &total);
    const HomingStatus status = m_polarControl->getHomingStatus();
    auto response = std_patch::make_unique<BufferedResponse>(
        "application/json", 256 + count * 128);
    response->printf("{\"cycle\":%lu,\"count\":%u,\"total\":%lu,\"samples\":[",
        static_cast<unsigned long>(status.cycle), static_cast<unsigned>(count),
        static_cast<unsigned long>(total));
    for (size_t index = 0; index < count; ++index) {
        const HomingTraceSample& sample = samples[index];
        if (index != 0) response->print(',');
        response->printf(
            "{\"a\":%u,\"p\":%u,\"n\":%u,\"t\":%lu,\"s\":%lu,\"o\":%ld,\"g\":%u,\"v\":%s",
            sample.axis, sample.phase, sample.pass,
            static_cast<unsigned long>(sample.elapsedMs),
            static_cast<unsigned long>(sample.steps),
            static_cast<long>(sample.legOriginSteps), sample.stallGuard,
            sample.valid ? "true" : "false");
        if (sample.contactBaseline != 0) {
            response->printf(",\"b\":%u,\"h\":%u",
                sample.contactBaseline, sample.contactThreshold);
        }
        response->print('}');
    }
    response->print("]}");
    request->send(response.release());
}

void SisyphusWebServer::handleHomeConfirm(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (!request->hasParam("successful", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing successful parameter\"}");
        return;
    }

    const String value = request->getParam("successful", true)->value();
    if (value != "true" && value != "false" && value != "1" && value != "0") {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Invalid successful parameter\"}");
        return;
    }

    const bool successful = value == "true" || value == "1";
    if (!m_polarControl->confirmHome(successful)) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"No homing result is awaiting review\"}");
        return;
    }

    request->send(200, "application/json", successful
        ? "{\"success\":true,\"message\":\"Home position confirmed\"}"
        : "{\"success\":true,\"message\":\"Homing rejected; retry required\"}");
}

void SisyphusWebServer::handleFileList(AsyncWebServerRequest *request) {
    if (!isSDCardReady()) {
        request->send(200, "application/json",
            "{\"storageAvailable\":false,\"files\":[]}");
        return;
    }

    bool isEmpty = true;
    if (m_cacheMutex) {
        SemaphoreGuard cacheLock(m_cacheMutex);
        isEmpty = m_fileCache.empty();
    }

    // If cache is empty and we are marked dirty, it means we are likely initializing
    if (isEmpty && (m_fileListDirty || m_fileScanActive)) {
        std::unique_ptr<AsyncWebServerResponse> response(request->beginResponse(503, "application/json", "{\"success\":false,\"message\":\"File list generating...\"}"));
        response->addHeader("Retry-After", "1");
        request->send(response.release());
        return;
    }

    auto response = std_patch::make_unique<BufferedResponse>("application/json", kResponseBufferSize);
    response->print("{\"storageAvailable\":");
    response->print(isSDCardReady() ? "true" : "false");
    response->printf(",\"loading\":%s,\"revision\":%lu,\"files\":[",
                     (m_fileListDirty || m_fileScanActive) ? "true" : "false",
                     static_cast<unsigned long>(m_fileListRevision.load()));

    if (m_cacheMutex) {
        SemaphoreGuard cacheLock(m_cacheMutex);
        for (size_t i = 0; i < m_fileCache.size(); i++) {
            if (i > 0) response->print(",");
            response->print("{\"name\":");
            writeJsonString(*response, m_fileCache[i].name);
            response->printf(",\"size\":%u,\"time\":%u,\"hasImage\":%s,\"imageTime\":%u,\"hasThumbnail\":%s,\"thumbnailTime\":%u}",
                m_fileCache[i].size, m_fileCache[i].time,
                m_fileCache[i].hasImage ? "true" : "false", m_fileCache[i].imageTime,
                m_fileCache[i].thumbnailSize > 0 ? "true" : "false", m_fileCache[i].thumbnailTime);
        }
    }

    response->print("]}");
    request->send(response.release());
}

void SisyphusWebServer::handleFileUpload(AsyncWebServerRequest *request, String filename,
                                        size_t index, uint8_t *data, size_t len, bool final) {
    // Coalesce a batch of uploads into one refresh after its writes settle.
    m_lastUploadActivity.store(millis());
    if (index == 0) {
        // One file per request. Reject extra parts without replacing/leaking
        // the context and staging handle belonging to the first part.
        if (request->_tempObject) {
            cleanUploadFile(request);
            static_cast<UploadContext*>(request->_tempObject)->status = 400;
            return;
        }
        noteRequest(request);
        UploadContext* upload = static_cast<UploadContext*>(calloc(1, sizeof(UploadContext)));
        request->_tempObject = upload;
        if (upload == nullptr) {
            request->abort();
            return;
        }

        upload->requestBytes = request->contentLength();
        uint32_t expected = 0;
        if (!m_uploadInflight.compare_exchange_strong(expected, 1)) {
            upload->status = 503;
            return;
        }
        upload->active = &m_uploadInflight;
        if (m_fileScanActive.load() || !isSDCardReady() ||
            !BulkResponseBudget::canStart(0, heap_caps_get_free_size(MALLOC_CAP_8BIT),
                                           heap_caps_get_largest_free_block(MALLOC_CAP_8BIT))) {
            upload->status = 503;
            return;
        }

        if (!validSimpleFilename(filename, ".thr") &&
            !validSimpleFilename(filename, ".png")) {
            upload->status = 400;
            return;
        }

        const bool thumbnail = request->hasParam("thumbnail") &&
            request->getParam("thumbnail")->value() == "1";
        if (thumbnail && !filename.endsWith(".png")) {
            upload->status = 400;
            return;
        }
        upload->originalImage = filename.endsWith(".png") && !thumbnail;
        upload->pattern = filename.endsWith(".thr");
        const String basename = filename.substring(0, filename.length() - 4);
        const String dirPath = "/patterns/" + basename;
        String finalPath = dirPath + "/" + (thumbnail ? basename + ".thumb.png" : filename);
        if (upload->originalImage) {
            SemaphoreGuard lock(m_cacheMutex);
            const auto* entry = findFileEntryByBase(basename);
            // A PNG-only refresh must replace the image alongside a legacy
            // flat THR. Otherwise the index keeps serving the old flat PNG.
            if (entry && !entry->isDirectory) finalPath = "/patterns/" + filename;
        }
        const uint32_t sequence = m_uploadSequence.fetch_add(1) + 1;
        const String tempPath = dirPath + "/.upload-" + String(sequence);
        upload->maxBytes = thumbnail ? 128U * 1024U :
            (filename.endsWith(".thr") ? 8U * 1024U * 1024U : 2U * 1024U * 1024U);
        snprintf(upload->finalPath, sizeof(upload->finalPath), "%s", finalPath.c_str());
        snprintf(upload->tempPath, sizeof(upload->tempPath), "%s", tempPath.c_str());

        if (!SD.exists("/patterns") && !SD.mkdir("/patterns")) {
            upload->status = 500;
            return;
        }
        if (!SD.exists(dirPath) && !SD.mkdir(dirPath)) {
            upload->status = 500;
            return;
        }

        SD.remove(upload->tempPath);
        request->_tempFile = SD.open(upload->tempPath, FILE_WRITE);
        if (!request->_tempFile) {
            upload->status = 500;
            ErrorLog::instance().log("ERROR", "SD", "OPEN_WRITE_FAILED",
                                     "Failed to open upload staging file", upload->tempPath);
            return;
        }
        request->_tempFile.setBufferSize(512);
        LOG("Upload start: %s\r\n", filename.c_str());
    }

    UploadContext* upload = static_cast<UploadContext*>(request->_tempObject);
    if (upload == nullptr || upload->status != 0) return;

    if (upload->staged || index != upload->received || len > upload->maxBytes - upload->received) {
        upload->status = 413;
        request->_tempFile.close();
        SD.remove(upload->tempPath);
        return;
    }

    if (len > 0 && request->_tempFile.write(data, len) != len) {
        upload->status = 507;
        request->_tempFile.close();
        SD.remove(upload->tempPath);
        ErrorLog::instance().log("ERROR", "SD", "UPLOAD_WRITE_FAILED",
                                 "Short write while staging an upload", upload->tempPath);
        return;
    }
    upload->received += len;

    if (final) {
        request->_tempFile.flush();
        if (!upload->received) {
            request->_tempFile.close();
            SD.remove(upload->tempPath);
            upload->status = 400;
            return;
        }
        const bool completeWrite = !request->_tempFile.getWriteError() &&
            request->_tempFile.size() == upload->received;
        request->_tempFile.close();
        if (!completeWrite) {
            upload->status = 507;
            SD.remove(upload->tempPath);
            return;
        }
        // Only stage here. Another part or malformed HTTP/MIME ending can
        // still reject this request; promotion belongs to its final handler.
        upload->staged = true;

    }
}

void SisyphusWebServer::handleFileDelete(AsyncWebServerRequest *request) {
    if (!requirePatternStorage(request)) return;
    if (!request->hasParam("file", true)) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing filename\"}");
        return;
    }

    String filename = request->getParam("file", true)->value();
    if (!validSimpleFilename(filename, ".thr")) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid filename\"}");
        return;
    }

    String basename = filename;
    if (basename.endsWith(".thr")) basename = basename.substring(0, basename.length() - 4);

    const String dirPath = "/patterns/" + basename;
    const String flatPattern = "/patterns/" + filename;
    const String flatImage = "/patterns/" + basename + ".png";
    bool found = false;
    bool deleted = true;
    if (SD.exists(dirPath)) {
        found = true;
        deleted = removeDirectoryRecursive(dirPath.c_str());
    }
    // Legacy flat patterns may also have a thumbnail-only directory. Removing
    // that directory alone must not report success while the .thr remains.
    for (const String* path : {&flatPattern, &flatImage}) {
        if (SD.exists(*path)) {
            found = true;
            if (!SD.remove(*path)) deleted = false;
        }
    }
    if (found) m_fileListDirty.store(true); // Refresh even after partial deletion.
    if (found && deleted) {
        request->send(200, "application/json", "{\"success\":true}");
    } else {
        request->send(found ? 500 : 404, "application/json", found
            ? "{\"success\":false,\"message\":\"Could not delete all pattern files\"}"
            : "{\"success\":false,\"message\":\"File not found\"}");
    }
}

void SisyphusWebServer::handleLEDBrightnessGet(AsyncWebServerRequest *request) {
    JsonDocument doc;
    LEDController::Diagnostics diagnostics;
    uint8_t brightness, target;
    uint32_t lastRequestUs, maxRequestUs, lastLockWaitUs, maxLockWaitUs;
    {
        SemaphoreGuard ledLock(m_ledMutex);
        brightness = m_ledController->getBrightness();
        target = m_ledController->getTargetBrightness();
        diagnostics = m_ledController->getDiagnostics();
        lastRequestUs = m_ledLastRequestUs;
        maxRequestUs = m_ledMaxRequestUs;
        lastLockWaitUs = m_ledLastLockWaitUs;
        maxLockWaitUs = m_ledMaxLockWaitUs;
    }
    doc["brightness"] = JsonHelpers::brightnessPercent(brightness);
    doc["targetBrightness"] = JsonHelpers::brightnessPercent(target);
    doc["pin"] = m_ledController->getPin();
    doc["pwmReady"] = diagnostics.ready;
    doc["pwmDuty"] = diagnostics.duty; // Raw register readback: 0–256, 256 = full on.
    doc["pwmFrequencyHz"] = diagnostics.frequencyHz;
    doc["writeCount"] = diagnostics.writes;
    doc["writeErrors"] = diagnostics.errors;
    doc["lastWriteUs"] = diagnostics.lastWriteUs;
    doc["maxWriteUs"] = diagnostics.maxWriteUs;
    doc["lastRequestUs"] = lastRequestUs;
    doc["maxRequestUs"] = maxRequestUs;
    doc["lastLockWaitUs"] = lastLockWaitUs;
    doc["maxLockWaitUs"] = maxLockWaitUs;

    auto response = std_patch::make_unique<BufferedResponse>("application/json", kResponseBufferSize);
    if (doc.overflowed()) throw std::bad_alloc();
    serializeJson(doc, *response);
    request->send(response.release());
}

void SisyphusWebServer::handleLEDBrightnessSet(AsyncWebServerRequest *request) {
    const uint32_t startedUs = micros();
    if (!request->hasParam("brightness", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing brightness parameter\"}");
        return;
    }

    int brightness = 0;
    if (!parseStrictInt(request->getParam("brightness", true)->value(), brightness) ||
        brightness < 0 || brightness > 100) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Brightness must be 0-100\"}");
        return;
    }

    uint8_t ledValue = map(brightness, 0, 100, 0, 255);
    const uint32_t lockStartedUs = micros();
    if (xSemaphoreTake(m_ledMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        request->send(503, "application/json",
            "{\"success\":false,\"message\":\"Light control busy; try again\"}");
        return;
    }
    const uint32_t lockWaitUs = micros() - lockStartedUs;
    m_presenceAutomation.cancelFade();
    const bool applied = m_ledController->setBrightness(ledValue, millis());
    const uint32_t requestUs = micros() - startedUs;
    m_ledLastRequestUs = requestUs;
    m_ledMaxRequestUs = std::max(m_ledMaxRequestUs, requestUs);
    m_ledLastLockWaitUs = lockWaitUs;
    m_ledMaxLockWaitUs = std::max(m_ledMaxLockWaitUs, lockWaitUs);
    xSemaphoreGive(m_ledMutex);
    if (!applied) {
        request->send(503, "application/json",
            "{\"success\":false,\"message\":\"LED controller not ready\"}");
        return;
    }

    LOG("LED brightness target set to: %d%% (%u/255), handler %luus, lock %luus\r\n", brightness,
        static_cast<unsigned>(ledValue), static_cast<unsigned long>(requestUs),
        static_cast<unsigned long>(lockWaitUs));

    request->send(200, "application/json", "{\"success\":true}");
}

void SisyphusWebServer::handleSpeedGet(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["speed"] = m_polarControl->getSpeed();

    auto response = std_patch::make_unique<BufferedResponse>("application/json", kResponseBufferSize);
    if (doc.overflowed()) throw std::bad_alloc();
    serializeJson(doc, *response);
    request->send(response.release());
}

void SisyphusWebServer::handleSpeedSet(AsyncWebServerRequest *request) {
    if (!request->hasParam("speed", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing speed parameter\"}");
        return;
    }

    int speed = 0;
    if (!parseStrictInt(request->getParam("speed", true)->value(), speed) ||
        speed < 1 || speed > 10) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Speed must be 1-10\"}");
        return;
    }

    m_polarControl->setSpeed(speed);

    LOG("Speed set to: %d\r\n", speed);

    request->send(200, "application/json", "{\"success\":true}");
}

void SisyphusWebServer::handleSystemInfo(AsyncWebServerRequest *request) {
    auto response = std_patch::make_unique<BufferedResponse>("application/json", kResponseBufferSize);
    writeSystemInfoJSON(*response);
    request->send(response.release());
}

// ============================================================================
// Playlist Handlers
// ============================================================================

void SisyphusWebServer::handlePlaylistAdd(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (!requirePatternStorage(request)) return;
    if (!request->hasParam("file", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing parameters\"}");
        return;
    }

    String file = request->getParam("file", true)->value();
    const String path = resolvePatternPath(file);
    if (path.length() == 0 || !m_playlist.addPattern(file)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Invalid pattern\"}");
        return;
    }

    request->send(200, "application/json", "{\"success\":true}");
}

void SisyphusWebServer::handlePlaylistAddAll(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (!requirePatternStorage(request)) return;
    int count = 0;
    {
        SemaphoreGuard cacheLock(m_cacheMutex);
        if (m_fileCache.empty() && (m_fileListDirty || m_fileScanActive)) {
            request->send(503, "application/json",
                "{\"success\":false,\"message\":\"Pattern library is still loading\"}");
            return;
        }
        for (const auto& entry : m_fileCache) {
            if (m_playlist.addPattern(entry.name)) ++count;
        }
    }

    auto response = std_patch::make_unique<BufferedResponse>("application/json", kResponseBufferSize);
    response->printf("{\"success\":true,\"count\":%d}", count);
    request->send(response.release());
}

void SisyphusWebServer::handlePlaylistRemove(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (!request->hasParam("index", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing index\"}");
        return;
    }

    int index = 0;
    if (!parseStrictInt(request->getParam("index", true)->value(), index) ||
        !m_playlist.removePattern(index)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Invalid playlist index\"}");
        return;
    }

    request->send(200, "application/json", "{\"success\":true}");
}

void SisyphusWebServer::handlePlaylistClear(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    m_playlist.clear();
    request->send(200, "application/json", "{\"success\":true}");
}

void SisyphusWebServer::handlePlaylistGet(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    auto response = std_patch::make_unique<BufferedResponse>("application/json", kResponseBufferSize);
    response->print("{\"loop\":");
    response->print(m_playlist.isLoop() ? "true" : "false");
    response->print(",\"playing\":");
    response->print(m_playlistMode ? "true" : "false");
    response->printf(",\"currentIndex\":%d", m_playlist.getCurrentIndex());
    response->printf(",\"count\":%d", m_playlist.count());
    response->print(",\"clearingEnabled\":");
    response->print(m_playlist.isClearingEnabled() ? "true" : "false");
    response->print(",\"items\":[");

    bool first = true;
    for (int i = 0; i < m_playlist.count(); i++) {
        const PlaylistItem& item = m_playlist.getItem(i);
        if (!first) response->print(',');
        response->print("{\"filename\":");
        writeJsonString(*response, item.filename);
        response->print('}');
        first = false;
    }

    response->print("]}");
    request->send(response.release());
}

void SisyphusWebServer::handlePlaylistStart(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (m_playlist.count() == 0) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Playlist is empty\"}");
        return;
    }

    if (!prepareReplacementLocked()) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Motion is unavailable until homing is complete\"}");
        return;
    }

    m_playlist.reset();
    m_playlistMode = true;

    LOG("Playlist started\r\n");
    request->send(202, "application/json", "{\"success\":true}");
}

void SisyphusWebServer::handlePlaylistStop(AsyncWebServerRequest *request) {
    handlePlaybackStop(request);
}

void SisyphusWebServer::handlePlaylistLoop(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (!request->hasParam("enabled", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing enabled parameter\"}");
        return;
    }

    bool enabled = false;
    if (!parseStrictBool(request->getParam("enabled", true)->value(), enabled)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Invalid enabled parameter\"}");
        return;
    }

    m_playlist.setLoop(enabled);

    request->send(200, "application/json", "{\"success\":true}");
}

void SisyphusWebServer::handlePlaylistShuffle(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    m_playlist.shuffle();
    request->send(200, "application/json", "{\"success\":true}");
}

void SisyphusWebServer::handlePlaylistSave(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (!requirePatternStorage(request)) return;
    if (!request->hasParam("name", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing name parameter\"}");
        return;
    }

    String name = request->getParam("name", true)->value();

    if (m_playlist.saveToFile(name)) {
        request->send(200, "application/json", "{\"success\":true}");
    } else {
        ErrorLog::instance().log("ERROR", "PLAYLIST", "SAVE_FAILED",
                                 "Failed to save playlist", name.c_str());
        request->send(500, "application/json",
            "{\"success\":false,\"message\":\"Failed to save playlist\"}");
    }
}

void SisyphusWebServer::handlePlaylistLoad(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (!requirePatternStorage(request)) return;
    if (!request->hasParam("name", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing name parameter\"}");
        return;
    }

    String name = request->getParam("name", true)->value();

    if (m_playlist.loadFromFile(name)) {
        request->send(200, "application/json", "{\"success\":true}");
    } else {
        ErrorLog::instance().log("ERROR", "PLAYLIST", "LOAD_FAILED",
                                 "Playlist not found", name.c_str());
        request->send(404, "application/json",
            "{\"success\":false,\"message\":\"Playlist not found\"}");
    }
}

void SisyphusWebServer::handlePlaylistList(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (!isSDCardReady()) {
        request->send(200, "application/json",
            "{\"storageAvailable\":false,\"playlists\":[]}");
        return;
    }
    auto response = std_patch::make_unique<BufferedResponse>("application/json", kResponseBufferSize);
    response->print("{\"storageAvailable\":true,\"playlists\":[");
    bool first = true;

    // List all .json files in /playlists directory
    File root = SD.open("/playlists");
    if (root) {
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                String filename = String(file.name());
                // Remove leading slash if present
                if (filename.startsWith("/")) filename = filename.substring(1);

                if (filename.endsWith(".json")) {
                    // Remove .json extension
                    filename = filename.substring(0, filename.length() - 5);
                    if (!first) response->print(',');
                    writeJsonString(*response, filename);
                    first = false;
                }
            }
            file.close();
            file = root.openNextFile();
        }
        root.close();
    }

    response->print("]}");
    request->send(response.release());
}

void SisyphusWebServer::handlePlaylistClearing(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (!request->hasParam("enabled", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing enabled parameter\"}");
        return;
    }

    bool enabled = false;
    if (!parseStrictBool(request->getParam("enabled", true)->value(), enabled)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Invalid enabled parameter\"}");
        return;
    }

    m_playlist.setClearingEnabled(enabled);

    LOG("Clearing patterns %s\r\n", enabled ? "enabled" : "disabled");

    request->send(200, "application/json", "{\"success\":true}");
}

void SisyphusWebServer::handlePlaylistMove(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (!request->hasParam("from", true) || !request->hasParam("to", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing from/to parameters\"}");
        return;
    }

    int fromIndex = 0;
    int toIndex = 0;
    if (!parseStrictInt(request->getParam("from", true)->value(), fromIndex) ||
        !parseStrictInt(request->getParam("to", true)->value(), toIndex) ||
        !m_playlist.movePattern(fromIndex, toIndex)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Invalid playlist indices\"}");
        return;
    }

    request->send(200, "application/json", "{\"success\":true}");
}

void SisyphusWebServer::handlePlaylistSkipTo(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (!request->hasParam("index", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing index parameter\"}");
        return;
    }

    int index = 0;
    if (!parseStrictInt(request->getParam("index", true)->value(), index) ||
        !m_playlist.skipToIndex(index)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Invalid playlist index\"}");
        return;
    }

    // Stop current pattern and skip to the requested one
    m_polarControl->stop();

    // If playlist mode is active, the next pattern will be picked up automatically
    request->send(200, "application/json", "{\"success\":true}");
}

void SisyphusWebServer::handlePlaylistNext(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (m_playlist.count() == 0) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Playlist is empty\"}");
        return;
    }

    // Stop current pattern and skip to next
    m_polarControl->stop();
    m_playlist.skipNext();

    request->send(200, "application/json", "{\"success\":true}");
}

void SisyphusWebServer::handlePlaylistPrev(AsyncWebServerRequest *request) {
    SemaphoreGuard stateLock(m_stateMutex);
    if (m_playlist.count() == 0) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Playlist is empty\"}");
        return;
    }

    // Stop current pattern and skip to previous
    m_polarControl->stop();
    m_playlist.skipPrevious();

    request->send(200, "application/json", "{\"success\":true}");
}

// ============================================================================
// Position and Path Tracking
// ============================================================================

void SisyphusWebServer::handlePosition(AsyncWebServerRequest *request) {
    // Get actual position from stepper motors (used for display)
    PolarCord_t actualPos = m_polarControl->getActualPosition();
    PolarVelocity_t actualVel = m_polarControl->getActualVelocity();
    float maxRho = m_polarControl->getMaxRho();

    // Debug log (throttled)
    static unsigned long lastLog = 0;
    if (millis() - lastLog > 5000) {
        lastLog = millis();
        LOG("API Position: rho=%.2f theta=%.2f\r\n", actualPos.rho, actualPos.theta);
    }

    auto response = std_patch::make_unique<BufferedResponse>("application/json", kResponseBufferSize);

    if (isnan(actualPos.rho) || isnan(actualPos.theta)) {
        response->printf("{\"current\":null,\"maxRho\":%.2f}", maxRho);
    } else {
        // Convert to normalized Cartesian coordinates (0-1 range)
        CartesianCord_t norm = PolarUtils::toNormalizedCartesian(actualPos, maxRho);

        // Normalize to 0-1 range (center at 0.5,0.5)
        float cosT = cosf(actualPos.theta);
        float sinT = sinf(actualPos.theta);
        float cartVelX = actualVel.rho * cosT - actualPos.rho * sinT * actualVel.theta;
        float cartVelY = actualVel.rho * sinT + actualPos.rho * cosT * actualVel.theta;
        float cartVel = sqrtf(cartVelX * cartVelX + cartVelY * cartVelY);

        response->printf(
            "{\"current\":{\"x\":%.4f,\"y\":%.4f,\"rho\":%.2f,\"theta\":%.2f,\"vr\":%.2f,\"vt\":%.3f,\"vc\":%.2f},\"maxRho\":%.2f}",
            norm.x, norm.y, actualPos.rho, actualPos.theta, actualVel.rho, actualVel.theta, cartVel, maxRho);
    }

    request->send(response.release());
}

// ============================================================================
// Tuning Handlers
// ============================================================================

// Helper to serialize DriverSettings to JSON
static void driverSettingsToJson(JsonObject& obj, const DriverSettings& settings) {
    // Current settings (mA)
    obj["runCurrent"] = settings.runCurrent;
    obj["holdCurrent"] = settings.holdCurrent;
    obj["holdDelay"] = settings.holdDelay;
    obj["powerDownDelay"] = settings.powerDownDelay;
    obj["highSensitivityCurrentScale"] = settings.highSensitivityCurrentScale;
    obj["chopperOffTime"] = settings.chopperOffTime;
    obj["hysteresisStart"] = settings.hysteresisStart;
    obj["hysteresisEnd"] = settings.hysteresisEnd;
    obj["blankTime"] = settings.blankTime;

    // Microstepping
    obj["microsteps"] = settings.microsteps;
    obj["interpolationEnabled"] = settings.interpolationEnabled;

    // StealthChop settings
    obj["stealthChopEnabled"] = settings.stealthChopEnabled;
    obj["stealthChopThreshold"] = settings.stealthChopThreshold;
    obj["pwmFrequency"] = settings.pwmFrequency;
    obj["pwmRegulation"] = settings.pwmRegulation;
    obj["pwmLimit"] = settings.pwmLimit;
    obj["standstillMode"] = settings.standstillMode;
    obj["automaticCurrentScaling"] = settings.automaticCurrentScaling;
    obj["automaticGradientAdaptation"] = settings.automaticGradientAdaptation;
    obj["pwmOffset"] = settings.pwmOffset;
    obj["pwmGradient"] = settings.pwmGradient;

    // CoolStep settings
    obj["coolStepEnabled"] = settings.coolStepEnabled;
    obj["coolStepLowerThreshold"] = settings.coolStepLowerThreshold;
    obj["coolStepUpperThreshold"] = settings.coolStepUpperThreshold;
    obj["coolStepCurrentIncrement"] = settings.coolStepCurrentIncrement;
    obj["coolStepMeasurementCount"] = settings.coolStepMeasurementCount;
    obj["coolStepThreshold"] = settings.coolStepThreshold;
}

static bool parseUnsignedParam(AsyncWebServerRequest *request, const char* name,
                               uint32_t& value) {
    if (!request->hasParam(name, true)) return true;
    const String text = request->getParam(name, true)->value();
    if (text.length() == 0 || text.charAt(0) == '-') return false;
    char* end = nullptr;
    errno = 0;
    unsigned long parsed = strtoul(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

static bool parseFloatParam(AsyncWebServerRequest *request, const char* name,
                            float& value) {
    if (!request->hasParam(name, true)) return true;
    const String text = request->getParam(name, true)->value();
    char* end = nullptr;
    errno = 0;
    float parsed = strtof(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

static bool parseBoolParam(AsyncWebServerRequest *request, const char* name,
                           bool& value) {
    if (!request->hasParam(name, true)) return true;
    const String text = request->getParam(name, true)->value();
    if (text == "true" || text == "1") {
        value = true;
        return true;
    }
    if (text == "false" || text == "0") {
        value = false;
        return true;
    }
    return false;
}

// Parse into wide temporary values before narrowing to the driver fields.
static bool parseDriverSettings(AsyncWebServerRequest *request, DriverSettings& settings) {
    uint32_t runCurrent = settings.runCurrent;
    uint32_t holdCurrent = settings.holdCurrent;
    uint32_t holdDelay = settings.holdDelay;
    uint32_t powerDownDelay = settings.powerDownDelay;
    uint32_t chopperOffTime = settings.chopperOffTime;
    uint32_t hysteresisStart = settings.hysteresisStart;
    uint32_t hysteresisEnd = settings.hysteresisEnd;
    uint32_t blankTime = settings.blankTime;
    uint32_t microsteps = settings.microsteps;
    uint32_t pwmFrequency = settings.pwmFrequency;
    uint32_t pwmRegulation = settings.pwmRegulation;
    uint32_t pwmLimit = settings.pwmLimit;
    uint32_t standstillMode = settings.standstillMode;
    uint32_t pwmOffset = settings.pwmOffset;
    uint32_t pwmGradient = settings.pwmGradient;
    uint32_t stealthThreshold = settings.stealthChopThreshold;
    uint32_t coolLower = settings.coolStepLowerThreshold;
    uint32_t coolUpper = settings.coolStepUpperThreshold;
    uint32_t coolIncrement = settings.coolStepCurrentIncrement;
    uint32_t coolCount = settings.coolStepMeasurementCount;
    uint32_t coolThreshold = settings.coolStepThreshold;

    if (!parseUnsignedParam(request, "runCurrent", runCurrent) || runCurrent > UINT16_MAX ||
        !parseUnsignedParam(request, "holdCurrent", holdCurrent) || holdCurrent > UINT16_MAX ||
        !parseUnsignedParam(request, "holdDelay", holdDelay) || holdDelay > UINT8_MAX ||
        !parseUnsignedParam(request, "powerDownDelay", powerDownDelay) || powerDownDelay > UINT8_MAX ||
        !parseUnsignedParam(request, "chopperOffTime", chopperOffTime) || chopperOffTime > UINT8_MAX ||
        !parseUnsignedParam(request, "hysteresisStart", hysteresisStart) || hysteresisStart > UINT8_MAX ||
        !parseUnsignedParam(request, "hysteresisEnd", hysteresisEnd) || hysteresisEnd > UINT8_MAX ||
        !parseUnsignedParam(request, "blankTime", blankTime) || blankTime > UINT8_MAX ||
        !parseUnsignedParam(request, "microsteps", microsteps) || microsteps > UINT16_MAX ||
        !parseUnsignedParam(request, "pwmFrequency", pwmFrequency) || pwmFrequency > UINT8_MAX ||
        !parseUnsignedParam(request, "pwmRegulation", pwmRegulation) || pwmRegulation > UINT8_MAX ||
        !parseUnsignedParam(request, "pwmLimit", pwmLimit) || pwmLimit > UINT8_MAX ||
        !parseUnsignedParam(request, "standstillMode", standstillMode) || standstillMode > UINT8_MAX ||
        !parseUnsignedParam(request, "pwmOffset", pwmOffset) || pwmOffset > UINT8_MAX ||
        !parseUnsignedParam(request, "pwmGradient", pwmGradient) || pwmGradient > UINT8_MAX ||
        !parseUnsignedParam(request, "stealthChopThreshold", stealthThreshold) ||
        !parseUnsignedParam(request, "coolStepLowerThreshold", coolLower) || coolLower > UINT8_MAX ||
        !parseUnsignedParam(request, "coolStepUpperThreshold", coolUpper) || coolUpper > UINT8_MAX ||
        !parseUnsignedParam(request, "coolStepCurrentIncrement", coolIncrement) || coolIncrement > UINT8_MAX ||
        !parseUnsignedParam(request, "coolStepMeasurementCount", coolCount) || coolCount > UINT8_MAX ||
        !parseUnsignedParam(request, "coolStepThreshold", coolThreshold) ||
        !parseBoolParam(request, "highSensitivityCurrentScale", settings.highSensitivityCurrentScale) ||
        !parseBoolParam(request, "interpolationEnabled", settings.interpolationEnabled) ||
        !parseBoolParam(request, "stealthChopEnabled", settings.stealthChopEnabled) ||
        !parseBoolParam(request, "automaticCurrentScaling", settings.automaticCurrentScaling) ||
        !parseBoolParam(request, "automaticGradientAdaptation", settings.automaticGradientAdaptation) ||
        !parseBoolParam(request, "coolStepEnabled", settings.coolStepEnabled)) {
        return false;
    }

    settings.runCurrent = static_cast<uint16_t>(runCurrent);
    settings.holdCurrent = static_cast<uint16_t>(holdCurrent);
    settings.holdDelay = static_cast<uint8_t>(holdDelay);
    settings.powerDownDelay = static_cast<uint8_t>(powerDownDelay);
    settings.chopperOffTime = static_cast<uint8_t>(chopperOffTime);
    settings.hysteresisStart = static_cast<uint8_t>(hysteresisStart);
    settings.hysteresisEnd = static_cast<uint8_t>(hysteresisEnd);
    settings.blankTime = static_cast<uint8_t>(blankTime);
    settings.microsteps = static_cast<uint16_t>(microsteps);
    settings.pwmFrequency = static_cast<uint8_t>(pwmFrequency);
    settings.pwmRegulation = static_cast<uint8_t>(pwmRegulation);
    settings.pwmLimit = static_cast<uint8_t>(pwmLimit);
    settings.standstillMode = static_cast<uint8_t>(standstillMode);
    settings.pwmOffset = static_cast<uint8_t>(pwmOffset);
    settings.pwmGradient = static_cast<uint8_t>(pwmGradient);
    settings.stealthChopThreshold = stealthThreshold;
    settings.coolStepLowerThreshold = static_cast<uint8_t>(coolLower);
    settings.coolStepUpperThreshold = static_cast<uint8_t>(coolUpper);
    settings.coolStepCurrentIncrement = static_cast<uint8_t>(coolIncrement);
    settings.coolStepMeasurementCount = static_cast<uint8_t>(coolCount);
    settings.coolStepThreshold = coolThreshold;
    return true;
}

static void sendTuningUpdateResult(AsyncWebServerRequest* request,
                                   TuningUpdateResult result) {
    switch (result) {
        case TuningUpdateResult::UPDATED:
            request->send(200, "application/json", "{\"success\":true}");
            return;
        case TuningUpdateResult::REJECTED:
            request->send(409, "application/json",
                "{\"success\":false,\"message\":\"Settings are unsafe or the system is busy\"}");
            return;
        case TuningUpdateResult::DRIVER_VERIFY_FAILED:
            request->send(502, "application/json",
                "{\"success\":false,\"message\":\"Driver UART verification failed; update was not committed\"}");
            return;
        case TuningUpdateResult::SAVE_FAILED:
            request->send(500, "application/json",
                "{\"success\":false,\"message\":\"Settings were not changed because flash storage failed\"}");
            return;
    }
}

void SisyphusWebServer::handleTuningGet(AsyncWebServerRequest *request) {
    JsonDocument doc;

    // Motion settings
    const MotionSettings& motion = m_polarControl->getMotionSettings();
    JsonObject motionObj = doc["motion"].to<JsonObject>();
    motionObj["rMaxVelocity"] = motion.rMaxVelocity;
    motionObj["rMaxAccel"] = motion.rMaxAccel;
    motionObj["rMaxJerk"] = motion.rMaxJerk;
    motionObj["tMaxVelocity"] = motion.tMaxVelocity;
    motionObj["tMaxAccel"] = motion.tMaxAccel;
    motionObj["ballMaxVelocity"] = motion.ballMaxVelocity;
    motionObj["ballMaxAccel"] = motion.ballMaxAccel;
    motionObj["cornerTolerance"] = motion.cornerTolerance;
    motionObj["tMaxJerk"] = motion.tMaxJerk;

    // Driver settings
    const DriverSettings& theta = m_polarControl->getThetaDriverSettings();
    JsonObject thetaObj = doc["thetaDriver"].to<JsonObject>();
    driverSettingsToJson(thetaObj, theta);

    const DriverSettings& rho = m_polarControl->getRhoDriverSettings();
    JsonObject rhoObj = doc["rhoDriver"].to<JsonObject>();
    driverSettingsToJson(rhoObj, rho);

    HomingSettings homing = m_polarControl->getHomingSettings();
    JsonObject homingObj = doc["homing"].to<JsonObject>();
    homingObj["triggerPercent"] = homing.triggerPercent;
    homingObj["consecutiveSamples"] = homing.consecutiveSamples;
    homingObj["minimumTravelMs"] = homing.minimumTravelMs;
    homingObj["runCurrent"] = Config::kRhoHomingRunCurrentMa;
    homingObj["holdCurrent"] = Config::kRhoHomingHoldCurrentMa;
    homingObj["microsteps"] = Config::kRhoHomingMicrosteps;
    homingObj["velocityMmS"] = Config::kRhoHomingVelocityMmPerSecond;
    homingObj["startupEntry"] = Config::kRhoStartupEntry;
#if defined(SISYPHUS_BENCH_MOTION_TEST) || defined(SISYPHUS_THETA_COMMISSIONING) || defined(SISYPHUS_RHO_COMMISSIONING)
    homingObj["autoHomeOnBoot"] = false;
    homingObj["unknownPositionEnabled"] = false;
#else
    homingObj["autoHomeOnBoot"] = Config::kAutoHomeOnBoot;
    homingObj["unknownPositionEnabled"] = Config::kEnableUnknownPositionRhoHoming;
#endif
    homingObj["knownStartMaximumMm"] = Config::kRhoKnownStartMaximumMm;
    homingObj["startupProbeMm"] = Config::kRhoStartupEntry ? Config::kRhoStartupProbeMm : 0;
    homingObj["runwayMm"] = Config::kRhoStartupEntry
        ? Config::kRhoStartupRunwayMm : Config::kRhoHomingRunwayMm;
    homingObj["cycleTimeoutMs"] = Config::kRhoHomingCycleTimeoutMs;
    homingObj["verificationBackoffMm"] =
        homing.verificationBackoffMm;
    homingObj["maximumOverrunMm"] = Config::kRhoHomingMaximumOverrunMm;
    homingObj["coarseMinimumTravelMm"] =
        Config::kRhoStartupEntry
            ? Config::kRhoStartupRunwayMm - Config::kRhoHomingApproachAgreementMm
            : Config::kRhoHomingRunwayMm - Config::kRhoHomingCoarseRunwayMarginMm;
    homingObj["approachAgreementMm"] =
        Config::kRhoHomingApproachAgreementMm;
    homingObj["requiredConsecutiveContacts"] = 3;
    homingObj["maximumContactAttempts"] = Config::kRhoHomingMaximumContactAttempts;
    homingObj["maximumTotalOverrunMm"] = Config::kRhoHomingMaximumTotalOverrunMm -
        (Config::kRhoStartupEntry ? Config::kRhoStartupProbeMm : 0);
    homingObj["candidateStrategy"] = "clustered-soft-votes";
    homingObj["traceContactMarkers"] = true;
    homingObj["rollingSearch"] = true;
    homingObj["retryArmingTracksBackoff"] = true;
    homingObj["pulseSource"] = "hardware-timer";
    homingObj["companionMotorEnabled"] = Config::kRhoCompanionMotorEnabled;
    homingObj["inactiveHoldStrategy"] = Config::kRhoCompanionMotorEnabled
        ? "vactual-u256" : "disabled-bridge";

    JsonObject limitsObj = doc["limits"].to<JsonObject>();
    limitsObj["thetaMaxRunCurrentMa"] = Config::kThetaMaxRunCurrentMa;
    limitsObj["rhoMaxRunCurrentMa"] = Config::kRhoMaxRunCurrentMa;
    limitsObj["driverSenseResistorOhms"] = Config::kDriverSenseResistorOhms;
    limitsObj["driverSenseResistorVerified"] =
        Config::kDriverSenseResistorVerified;
    limitsObj["rhoMaxUnmeasuredCurrentRegister"] =
        Config::kRhoMaxUnmeasuredCurrentRegister;
    doc["persistenceAvailable"] = m_polarControl->tuningPersistenceAvailable();

    auto response = std_patch::make_unique<BufferedResponse>("application/json", kResponseBufferSize);
    if (doc.overflowed()) throw std::bad_alloc();
    serializeJson(doc, *response);
    request->send(response.release());
}

void SisyphusWebServer::handleTuningMotionSet(AsyncWebServerRequest *request) {
    MotionSettings settings = m_polarControl->getMotionSettings();

    if (!parseFloatParam(request, "rMaxVelocity", settings.rMaxVelocity) ||
        !parseFloatParam(request, "rMaxAccel", settings.rMaxAccel) ||
        !parseFloatParam(request, "rMaxJerk", settings.rMaxJerk) ||
        !parseFloatParam(request, "tMaxVelocity", settings.tMaxVelocity) ||
        !parseFloatParam(request, "tMaxAccel", settings.tMaxAccel) ||
        !parseFloatParam(request, "ballMaxVelocity", settings.ballMaxVelocity) ||
        !parseFloatParam(request, "ballMaxAccel", settings.ballMaxAccel) ||
        !parseFloatParam(request, "cornerTolerance", settings.cornerTolerance) ||
        !parseFloatParam(request, "tMaxJerk", settings.tMaxJerk)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Malformed numeric setting\"}");
        return;
    }

    sendTuningUpdateResult(request,
        m_polarControl->saveMotionSettings(settings));
}

void SisyphusWebServer::handleTuningThetaDriverSet(AsyncWebServerRequest *request) {
    DriverSettings settings = m_polarControl->getThetaDriverSettings();
    if (!parseDriverSettings(request, settings)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Malformed driver setting\"}");
        return;
    }
    sendTuningUpdateResult(request,
        m_polarControl->saveThetaDriverSettings(settings));
}

void SisyphusWebServer::handleTuningRhoDriverSet(AsyncWebServerRequest *request) {
    DriverSettings settings = m_polarControl->getRhoDriverSettings();
    if (!parseDriverSettings(request, settings)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Malformed driver setting\"}");
        return;
    }
    sendTuningUpdateResult(request,
        m_polarControl->saveRhoDriverSettings(settings));
}

void SisyphusWebServer::handleTuningHomingSet(AsyncWebServerRequest *request) {
    HomingSettings settings = m_polarControl->getHomingSettings();
    uint32_t triggerPercent = settings.triggerPercent;
    uint32_t consecutiveSamples = settings.consecutiveSamples;
    uint32_t minimumTravelMs = settings.minimumTravelMs;
    uint32_t verificationBackoffMm = settings.verificationBackoffMm;
    if (!parseUnsignedParam(request, "triggerPercent", triggerPercent) || triggerPercent > UINT8_MAX ||
        !parseUnsignedParam(request, "consecutiveSamples", consecutiveSamples) || consecutiveSamples > UINT8_MAX ||
        !parseUnsignedParam(request, "minimumTravelMs", minimumTravelMs) || minimumTravelMs > UINT16_MAX ||
        !parseUnsignedParam(request, "verificationBackoffMm", verificationBackoffMm) || verificationBackoffMm > UINT16_MAX) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Malformed homing setting\"}");
        return;
    }
    settings.triggerPercent = static_cast<uint8_t>(triggerPercent);
    settings.consecutiveSamples = static_cast<uint8_t>(consecutiveSamples);
    settings.minimumTravelMs = static_cast<uint16_t>(minimumTravelMs);
    settings.verificationBackoffMm = static_cast<uint16_t>(verificationBackoffMm);
    sendTuningUpdateResult(request,
        m_polarControl->saveHomingSettings(settings));
}

void SisyphusWebServer::handleTuningTestThetaContinuous(AsyncWebServerRequest *request) {
#ifdef SISYPHUS_RHO_COMMISSIONING
    request->send(409, "application/json",
        "{\"success\":false,\"message\":\"Theta motion is disabled in rho commissioning mode\"}");
    return;
#endif
    SemaphoreGuard stateLock(m_stateMutex);
    if (!queueTuningTestLocked(PendingMotion::THETA_CONTINUOUS)) {
        request->send(409, "application/json", "{\"success\":false,\"message\":\"Motion is unavailable until homing is complete\"}");
        return;
    }
    request->send(202, "application/json", "{\"success\":true,\"message\":\"Test queued\"}");
}

void SisyphusWebServer::handleTuningTestThetaStress(AsyncWebServerRequest *request) {
#ifdef SISYPHUS_RHO_COMMISSIONING
    request->send(409, "application/json",
        "{\"success\":false,\"message\":\"Theta motion is disabled in rho commissioning mode\"}");
    return;
#endif
    SemaphoreGuard stateLock(m_stateMutex);
    if (!queueTuningTestLocked(PendingMotion::THETA_STRESS)) {
        request->send(409, "application/json", "{\"success\":false,\"message\":\"Motion is unavailable until homing is complete\"}");
        return;
    }
    request->send(202, "application/json", "{\"success\":true,\"message\":\"Test queued\"}");
}

void SisyphusWebServer::handleTuningTestThetaSegment(
        AsyncWebServerRequest *request) {
#ifndef SISYPHUS_THETA_COMMISSIONING
    request->send(409, "application/json",
        "{\"success\":false,\"message\":\"Segmented theta tests require theta commissioning mode\"}");
    return;
#else
    if (!request->hasParam("targetRadians", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing targetRadians\"}");
        return;
    }
    float target = 0.0f;
    if (!parseStrictFloat(
            request->getParam("targetRadians", true)->value(), target) ||
        target < 0.0f || target > 2.0f * PI) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"targetRadians must be within 0..2pi\"}");
        return;
    }
    SemaphoreGuard stateLock(m_stateMutex);
    if (!queueTuningTestLocked(PendingMotion::THETA_SEGMENT)) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Theta segment is unavailable\"}");
        return;
    }
    m_pendingThetaSegmentTarget = target;
    request->send(202, "application/json",
        "{\"success\":true,\"message\":\"Theta segment queued\"}");
#endif
}

void SisyphusWebServer::handleTuningTestRhoContinuous(AsyncWebServerRequest *request) {
#ifdef SISYPHUS_THETA_COMMISSIONING
    request->send(409, "application/json",
        "{\"success\":false,\"message\":\"Rho motion is disabled in theta commissioning mode\"}");
    return;
#endif
    SemaphoreGuard stateLock(m_stateMutex);
    if (!queueTuningTestLocked(PendingMotion::RHO_CONTINUOUS)) {
        request->send(409, "application/json", "{\"success\":false,\"message\":\"Motion is unavailable until homing is complete\"}");
        return;
    }
    request->send(202, "application/json", "{\"success\":true,\"message\":\"Test queued\"}");
}

void SisyphusWebServer::handleTuningTestRhoStress(AsyncWebServerRequest *request) {
#ifdef SISYPHUS_THETA_COMMISSIONING
    request->send(409, "application/json",
        "{\"success\":false,\"message\":\"Rho motion is disabled in theta commissioning mode\"}");
    return;
#endif
    SemaphoreGuard stateLock(m_stateMutex);
    if (!queueTuningTestLocked(PendingMotion::RHO_STRESS)) {
        request->send(409, "application/json", "{\"success\":false,\"message\":\"Motion is unavailable until homing is complete\"}");
        return;
    }
    request->send(202, "application/json", "{\"success\":true,\"message\":\"Test queued\"}");
}

void SisyphusWebServer::handleTuningTestRhoSegment(AsyncWebServerRequest *request) {
#ifndef SISYPHUS_RHO_COMMISSIONING
    request->send(409, "application/json",
        "{\"success\":false,\"message\":\"Segmented rho tests require rho commissioning mode\"}");
    return;
#else
    if (!request->hasParam("targetMm", true)) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing targetMm\"}");
        return;
    }
    float target = 0.0f;
    if (!parseStrictFloat(request->getParam("targetMm", true)->value(), target) ||
        target < 0.0f || target > Config::kRhoKnownStartMaximumMm) {
        request->send(400, "application/json",
            "{\"success\":false,\"message\":\"targetMm exceeds the qualification travel limit\"}");
        return;
    }
    SemaphoreGuard stateLock(m_stateMutex);
    if (!queueTuningTestLocked(PendingMotion::RHO_SEGMENT)) {
        request->send(409, "application/json",
            "{\"success\":false,\"message\":\"Rho segment is unavailable\"}");
        return;
    }
    m_pendingRhoSegmentTarget = target;
    request->send(202, "application/json",
        "{\"success\":true,\"message\":\"Rho segment queued\"}");
#endif
}
