#pragma once

// Cross-platform Nakama REST client
// Windows: WinHTTP | macOS/Linux: libcurl

#include <string>
#include <iostream>
#include <functional>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <cstdint>
#include <cstdio>
#include <cmath>

#ifdef _WIN32
#  include <windows.h>
#  include <winhttp.h>
#  pragma comment(lib, "winhttp")
   using NakamaU64 = ULONGLONG;
   static NakamaU64 nakama_now_ms() { return GetTickCount64(); }
#else
#  include <curl/curl.h>
#  include <chrono>
#  include <unistd.h>
   using NakamaU64 = uint64_t;
   static NakamaU64 nakama_now_ms() {
       using namespace std::chrono;
       return (NakamaU64)duration_cast<milliseconds>(
           steady_clock::now().time_since_epoch()).count();
   }
#endif

// ============================================================
struct NakamaSession {
    std::string token;
    std::string userId;
    bool isValid = false;
};

struct RemotePlayer {
    std::string userId;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float startX = 0.0f;
    float startY = 0.0f;
    float startZ = 0.0f;
    float startYaw = 0.0f;
    float startPitch = 0.0f;
    float targetX = 0.0f;
    float targetY = 0.0f;
    float targetZ = 0.0f;
    float targetYaw = 0.0f;
    float targetPitch = 0.0f;
    float interpolationProgress = 1.0f;
    float interpolationDuration = 0.10f;
    NakamaU64 lastSnapshotTs = 0;
    NakamaU64 lastSeenLocalMs = 0;
    bool initialized = false;
    bool active = true;
};

struct BlockEvent {
    std::string type;   // "break" or "place"
    int index;
    float minX, maxX, minY, maxY, minZ, maxZ;
    int textureId;
    NakamaU64 seq;
};

// ============================================================
class NakamaClient {
public:
    std::string serverKey, host;
    int port;

    std::mutex playersMutex;
    std::mutex blockEventsMutex;
    std::map<std::string, RemotePlayer> remotePlayers;
    std::atomic<bool> inMatch{false};
    std::atomic<bool> positionPollInFlight{false};
    std::atomic<bool> blockPollInFlight{false};
    NakamaU64 lastSeenEventSeq = 0;
    std::unordered_set<std::string> seenBlockEventKeys;
    std::vector<std::string> seenBlockEventKeyOrder;

    NakamaClient(const std::string& key = "defaultkey",
                 const std::string& h = "127.0.0.1", int p = 7350)
        : serverKey(key), host(h), port(p) {}

    ~NakamaClient() { inMatch = false; }

    // ========================
    // Authentication
    // ========================
    void authenticateDevice(const std::string& deviceId,
                            std::function<void(NakamaSession)> successCb,
                            std::function<void(std::string)> errorCb) {
        std::thread([this, deviceId, successCb, errorCb]() {
            std::string body = "{\"id\":\"" + deviceId + "\"}";
            std::string response = httpRequest("POST",
                "/v2/account/authenticate/device?create=true", body, "");

            if (response.empty()) {
                if (errorCb) errorCb("Failed to connect to Nakama server");
                return;
            }
            std::string token = extractJsonString(response, "token");
            if (token.empty()) {
                if (errorCb) errorCb("Invalid response: " + response);
                return;
            }
            NakamaSession session;
            session.token = token;
            session.isValid = true;

            // Decode userId from JWT payload
            size_t d1 = token.find('.'), d2 = token.find('.', d1 + 1);
            if (d1 != std::string::npos && d2 != std::string::npos) {
                std::string payload = base64Decode(token.substr(d1+1, d2-d1-1));
                session.userId = extractJsonString(payload, "uid");
            }
            if (successCb) successCb(session);
        }).detach();
    }

    // ========================
    // Position sync
    // ========================
    void sendPosition(const NakamaSession& session,
                      float x, float y, float z, float yaw, float pitch) {
        if (!inMatch || session.userId.empty()) return;
        NakamaU64 nowMs = nakama_now_ms();
        char valueBuf[256];
        snprintf(valueBuf, sizeof(valueBuf),
            "{\\\"x\\\":%.3f,\\\"y\\\":%.3f,\\\"z\\\":%.3f,"
            "\\\"yaw\\\":%.3f,\\\"pitch\\\":%.3f,\\\"ts\\\":%llu}",
            x, y, z, yaw, pitch, (unsigned long long)nowMs);

        std::string body = std::string("{\"objects\":[{")
            + "\"collection\":\"positions\","
            + "\"key\":\"current\","
            + "\"value\":\"" + valueBuf + "\","
            + "\"permission_read\":2,"
            + "\"permission_write\":1"
            + "}]}";
        std::thread([this, session, body]() {
            httpRequest("PUT", "/v2/storage", body, session.token);
        }).detach();
    }

    void pollPositions(const NakamaSession& session) {
        if (!inMatch) return;
        bool expected = false;
        if (!positionPollInFlight.compare_exchange_strong(expected, true)) return;

        std::thread([this, session]() {
            std::string response = httpRequest("GET",
                "/v2/storage/positions?limit=50", "", session.token);
            if (response.empty()) {
                positionPollInFlight = false;
                return;
            }

            std::lock_guard<std::mutex> lock(playersMutex);

            size_t searchPos = 0;
            while (true) {
                size_t objStart = response.find("\"user_id\"", searchPos);
                if (objStart == std::string::npos) break;
                std::string uid = extractJsonStringAt(response, objStart);

                size_t valuePos = response.find("\"value\"", objStart);
                if (valuePos == std::string::npos || valuePos - objStart > 500) {
                    searchPos = objStart + 10; continue;
                }
                std::string valueStr = extractJsonStringAt(response, valuePos);
                std::string unescaped;
                for (size_t i = 0; i < valueStr.size(); i++) {
                    if (valueStr[i] == '\\' && i+1 < valueStr.size()) { unescaped += valueStr[i+1]; i++; }
                    else unescaped += valueStr[i];
                }

                if (uid != session.userId && !uid.empty()) {
                    NakamaU64 nowMs = nakama_now_ms();
                    NakamaU64 ts = extractJsonULL(unescaped, "ts");
                    const float newX = extractJsonFloat(unescaped, "x");
                    const float newY = extractJsonFloat(unescaped, "y");
                    const float newZ = extractJsonFloat(unescaped, "z");
                    const float newYaw = extractJsonFloat(unescaped, "yaw");
                    const float newPitch = extractJsonFloat(unescaped, "pitch");

                    RemotePlayer& rp = remotePlayers[uid];
                    rp.userId = uid;
                    rp.active = true;
                    rp.lastSeenLocalMs = nowMs;

                    if (!rp.initialized) {
                        rp.x = newX;
                        rp.y = newY;
                        rp.z = newZ;
                        rp.yaw = newYaw;
                        rp.pitch = newPitch;
                        rp.startX = newX;
                        rp.startY = newY;
                        rp.startZ = newZ;
                        rp.startYaw = newYaw;
                        rp.startPitch = newPitch;
                        rp.targetX = newX;
                        rp.targetY = newY;
                        rp.targetZ = newZ;
                        rp.targetYaw = newYaw;
                        rp.targetPitch = newPitch;
                        rp.interpolationProgress = 1.0f;
                        rp.interpolationDuration = 0.10f;
                        rp.lastSnapshotTs = ts;
                        rp.lastSeenLocalMs = nowMs;
                        rp.initialized = true;
                    } else {
                        if (ts > 0 && rp.lastSnapshotTs > 0 && ts <= rp.lastSnapshotTs) {
                            searchPos = valuePos + 10;
                            continue;
                        }

                        float snapshotDeltaSeconds = rp.interpolationDuration;
                        if (ts > 0 && rp.lastSnapshotTs > 0 && ts > rp.lastSnapshotTs) {
                            snapshotDeltaSeconds = static_cast<float>(ts - rp.lastSnapshotTs) / 1000.0f;
                        }
                        rp.lastSnapshotTs = ts;
                        rp.startX = rp.x;
                        rp.startY = rp.y;
                        rp.startZ = rp.z;
                        rp.startYaw = rp.yaw;
                        rp.startPitch = rp.pitch;
                        rp.targetX = newX;
                        rp.targetY = newY;
                        rp.targetZ = newZ;
                        rp.targetYaw = newYaw;
                        rp.targetPitch = newPitch;
                        rp.interpolationProgress = 0.0f;
                        rp.interpolationDuration = std::clamp(snapshotDeltaSeconds, 0.05f, 0.25f);
                    }
                }
                searchPos = valuePos + 10;
            }

            NakamaU64 nowMs = nakama_now_ms();
            for (auto it = remotePlayers.begin(); it != remotePlayers.end();) {
                const NakamaU64 seenMs = it->second.lastSeenLocalMs;
                if (seenMs > 0 && nowMs > seenMs && (nowMs - seenMs) > 6000) {
                    it = remotePlayers.erase(it);
                } else {
                    ++it;
                }
            }

            positionPollInFlight = false;
        }).detach();
    }

    void updateRemotePlayers(float deltaTime) {
        std::lock_guard<std::mutex> lock(playersMutex);
        for (auto& pair : remotePlayers) {
            RemotePlayer& rp = pair.second;
            if (!rp.initialized) continue;

            if (rp.interpolationProgress >= 1.0f) {
                rp.x = rp.targetX;
                rp.y = rp.targetY;
                rp.z = rp.targetZ;
                rp.yaw = rp.targetYaw;
                rp.pitch = rp.targetPitch;
                continue;
            }

            const float duration = std::max(rp.interpolationDuration, 0.001f);
            rp.interpolationProgress = std::min(1.0f, rp.interpolationProgress + (deltaTime / duration));
            const float t = rp.interpolationProgress;
            const float easedT = t * t * (3.0f - 2.0f * t);

            rp.x = rp.startX + (rp.targetX - rp.startX) * easedT;
            rp.y = rp.startY + (rp.targetY - rp.startY) * easedT;
            rp.z = rp.startZ + (rp.targetZ - rp.startZ) * easedT;
            rp.yaw = lerpAngleDegrees(rp.startYaw, rp.targetYaw, easedT);
            rp.pitch = rp.startPitch + (rp.targetPitch - rp.startPitch) * easedT;
        }
    }

    std::map<std::string, RemotePlayer> getRemotePlayers() {
        std::lock_guard<std::mutex> lock(playersMutex);
        return remotePlayers;
    }

    // ========================
    // Block event sync
    // ========================
    void sendBlockBreak(const NakamaSession& session, int blockIndex) {
        if (!inMatch) return;
        NakamaU64 seq = nakama_now_ms();
        char valueBuf[256];
        snprintf(valueBuf, sizeof(valueBuf),
            "{\\\"type\\\":\\\"break\\\",\\\"idx\\\":%d,\\\"seq\\\":%llu}",
            blockIndex, (unsigned long long)seq);
        _sendBlockEvent(session, valueBuf, seq);
    }

    void sendBlockPlace(const NakamaSession& session,
                        float minX, float maxX, float minY, float maxY,
                        float minZ, float maxZ, int texId) {
        if (!inMatch) return;
        NakamaU64 seq = nakama_now_ms();
        char valueBuf[512];
        snprintf(valueBuf, sizeof(valueBuf),
            "{\\\"type\\\":\\\"place\\\","
            "\\\"minX\\\":%.3f,\\\"maxX\\\":%.3f,"
            "\\\"minY\\\":%.3f,\\\"maxY\\\":%.3f,"
            "\\\"minZ\\\":%.3f,\\\"maxZ\\\":%.3f,"
            "\\\"tex\\\":%d,\\\"seq\\\":%llu}",
            minX, maxX, minY, maxY, minZ, maxZ, texId, (unsigned long long)seq);
        _sendBlockEvent(session, valueBuf, seq);
    }

    void pollBlockEvents(const NakamaSession& session,
                         std::function<void(const BlockEvent&)> onEvent) {
        if (!inMatch) return;
        bool expected = false;
        if (!blockPollInFlight.compare_exchange_strong(expected, true)) return;

        std::thread([this, session, onEvent]() {
            std::string response = httpRequest("GET",
                "/v2/storage/blockevents?limit=100", "", session.token);
            if (response.empty()) {
                blockPollInFlight = false;
                return;
            }

            size_t searchPos = 0;
            NakamaU64 maxSeq = lastSeenEventSeq;
            std::vector<BlockEvent> events;

            while (true) {
                size_t valuePos = response.find("\"value\"", searchPos);
                if (valuePos == std::string::npos) break;

                size_t objStart = response.rfind('{', valuePos);
                std::string objHeader = (objStart != std::string::npos)
                    ? response.substr(objStart, valuePos - objStart) : "";

                std::string ownerId = extractJsonString(objHeader, "user_id");
                std::string eventKey = extractJsonString(objHeader, "key");
                if (ownerId == session.userId) { searchPos = valuePos+10; continue; }

                {
                    std::lock_guard<std::mutex> lock(blockEventsMutex);
                    if (!eventKey.empty() && seenBlockEventKeys.find(eventKey) != seenBlockEventKeys.end()) {
                        searchPos = valuePos + 10;
                        continue;
                    }
                }

                std::string valueStr = extractJsonStringAt(response, valuePos);
                std::string unescaped;
                for (size_t i = 0; i < valueStr.size(); i++) {
                    if (valueStr[i] == '\\' && i+1 < valueStr.size()) { unescaped += valueStr[i+1]; i++; }
                    else unescaped += valueStr[i];
                }

                NakamaU64 seq = extractJsonULL(unescaped, "seq");
                BlockEvent ev;
                ev.seq = seq;
                ev.type = extractJsonString(unescaped, "type");
                ev.index = (int)extractJsonULL(unescaped, "idx");
                ev.minX = extractJsonFloat(unescaped, "minX");
                ev.maxX = extractJsonFloat(unescaped, "maxX");
                ev.minY = extractJsonFloat(unescaped, "minY");
                ev.maxY = extractJsonFloat(unescaped, "maxY");
                ev.minZ = extractJsonFloat(unescaped, "minZ");
                ev.maxZ = extractJsonFloat(unescaped, "maxZ");
                ev.textureId = (int)extractJsonULL(unescaped, "tex");
                events.push_back(ev);
                if (seq > maxSeq) maxSeq = seq;

                if (!eventKey.empty()) {
                    std::lock_guard<std::mutex> lock(blockEventsMutex);
                    if (seenBlockEventKeys.insert(eventKey).second) {
                        seenBlockEventKeyOrder.push_back(eventKey);
                        const size_t maxRememberedKeys = 2000;
                        if (seenBlockEventKeyOrder.size() > maxRememberedKeys) {
                            const std::string oldest = seenBlockEventKeyOrder.front();
                            seenBlockEventKeyOrder.erase(seenBlockEventKeyOrder.begin());
                            seenBlockEventKeys.erase(oldest);
                        }
                    }
                }
                searchPos = valuePos + 10;
            }

            lastSeenEventSeq = maxSeq;
            for (const auto& ev : events) onEvent(ev);
            blockPollInFlight = false;
        }).detach();
    }

    void leaveMatch() {
        inMatch = false;
        std::lock_guard<std::mutex> lock(playersMutex);
        remotePlayers.clear();
        positionPollInFlight = false;
        blockPollInFlight = false;

        std::lock_guard<std::mutex> eventsLock(blockEventsMutex);
        seenBlockEventKeys.clear();
        seenBlockEventKeyOrder.clear();
    }

private:
    static float lerpAngleDegrees(float from, float to, float t) {
        float delta = std::fmod(to - from, 360.0f);
        if (delta > 180.0f) delta -= 360.0f;
        if (delta < -180.0f) delta += 360.0f;
        return from + delta * t;
    }

    void _sendBlockEvent(const NakamaSession& session,
                         const char* valueBuf, NakamaU64 seq) {
        std::string key = "ev_" + std::to_string((unsigned long long)seq);
        std::string body = std::string("{\"objects\":[{")
            + "\"collection\":\"blockevents\","
            + "\"key\":\"" + key + "\","
            + "\"value\":\"" + valueBuf + "\","
            + "\"permission_read\":2,"
            + "\"permission_write\":1"
            + "}]}";
        std::thread([this, session, body]() {
            httpRequest("PUT", "/v2/storage", body, session.token);
        }).detach();
    }

    // ========================
    // JSON helpers
    // ========================
    // NOTE: Nakama re-serializes stored JSON with spaces after colons.
    static std::string extractJsonString(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t start = json.find(search);
        if (start == std::string::npos) return "";
        start += search.length();
        while (start < json.size() && json[start] == ' ') start++;
        if (start >= json.size() || json[start] != '"') return "";
        start++;
        size_t end = start;
        while (end < json.size() && json[end] != '"') {
            if (json[end] == '\\') end++;
            end++;
        }
        return json.substr(start, end - start);
    }

    static std::string extractJsonStringAt(const std::string& json, size_t keyPos) {
        size_t colonPos = json.find(':', keyPos);
        if (colonPos == std::string::npos) return "";
        size_t quoteStart = json.find('"', colonPos + 1);
        if (quoteStart == std::string::npos) return "";
        quoteStart++;
        size_t quoteEnd = quoteStart;
        while (quoteEnd < json.size() && json[quoteEnd] != '"') {
            if (json[quoteEnd] == '\\') quoteEnd++;
            quoteEnd++;
        }
        return json.substr(quoteStart, quoteEnd - quoteStart);
    }

    static float extractJsonFloat(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t start = json.find(search);
        if (start == std::string::npos) return 0.0f;
        start += search.length();
        while (start < json.size() && (json[start] == ' ' || json[start] == '"')) start++;
        size_t end = start;
        while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != '"' && json[end] != ' ') end++;
        try { return std::stof(json.substr(start, end - start)); }
        catch (...) { return 0.0f; }
    }

    static NakamaU64 extractJsonULL(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t start = json.find(search);
        if (start == std::string::npos) return 0;
        start += search.length();
        while (start < json.size() && (json[start] == ' ' || json[start] == '"')) start++;
        size_t end = start;
        while (end < json.size() && json[end] >= '0' && json[end] <= '9') end++;
        if (end == start) return 0;
        try { return (NakamaU64)std::stoull(json.substr(start, end - start)); }
        catch (...) { return 0; }
    }

    // ========================
    // HTTP
    // ========================
    std::string httpRequest(const std::string& method, const std::string& path,
                            const std::string& body, const std::string& bearerToken) {
#ifdef _WIN32
        return httpRequestWinHTTP(method, path, body, bearerToken);
#else
        return httpRequestCurl(method, path, body, bearerToken);
#endif
    }

#ifdef _WIN32
    std::string httpRequestWinHTTP(const std::string& method, const std::string& path,
                                   const std::string& body, const std::string& bearerToken) {
        std::wstring wHost(host.begin(), host.end());
        std::wstring wMethod(method.begin(), method.end());

        HINTERNET hSession = WinHttpOpen(L"NakamaClient/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return "";

        HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), port, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

        std::wstring wPath(path.begin(), path.end());
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, wMethod.c_str(), wPath.c_str(),
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

        if (!bearerToken.empty()) {
            std::string ah = "Authorization: Bearer " + bearerToken;
            std::wstring wah(ah.begin(), ah.end());
            WinHttpAddRequestHeaders(hRequest, wah.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
        } else {
            std::string as = serverKey + ":";
            std::string ah = "Authorization: Basic " + base64Encode(as);
            std::wstring wah(ah.begin(), ah.end());
            WinHttpAddRequestHeaders(hRequest, wah.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
        }
        WinHttpAddRequestHeaders(hRequest, L"Content-Type: application/json", -1, WINHTTP_ADDREQ_FLAG_ADD);

        BOOL ok = body.empty()
            ? WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            : WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)body.c_str(), body.length(), body.length(), 0);

        if (!ok || !WinHttpReceiveResponse(hRequest, NULL)) {
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return "";
        }

        std::string result;
        DWORD dwSize = 0, dwDownloaded = 0;
        do {
            WinHttpQueryDataAvailable(hRequest, &dwSize);
            if (!dwSize) break;
            char* buf = new char[dwSize + 1]();
            WinHttpReadData(hRequest, buf, dwSize, &dwDownloaded);
            result.append(buf, dwDownloaded);
            delete[] buf;
        } while (dwSize > 0);

        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return result;
    }
#else
    static size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, std::string* out) {
        out->append(ptr, size * nmemb);
        return size * nmemb;
    }

    std::string httpRequestCurl(const std::string& method, const std::string& path,
                                const std::string& body, const std::string& bearerToken) {
        CURL* curl = curl_easy_init();
        if (!curl) return "";

        std::string url = "http://" + host + ":" + std::to_string(port) + path;
        std::string result;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        if (!bearerToken.empty()) {
            std::string auth = "Authorization: Bearer " + bearerToken;
            headers = curl_slist_append(headers, auth.c_str());
        } else {
            std::string auth = "Authorization: Basic " + base64Encode(serverKey + ":");
            headers = curl_slist_append(headers, auth.c_str());
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
        } else if (method == "PUT") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
        } else if (method == "DELETE") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            if (!body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
            }
        }
        // GET is default

        curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return result;
    }
#endif

    // ========================
    // Base64
    // ========================
    static std::string base64Encode(const std::string& input) {
        static const char* chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        int i = 0; unsigned char bytes[3];
        for (size_t pos = 0; pos < input.size(); pos++) {
            bytes[i++] = input[pos];
            if (i == 3) {
                result += chars[(bytes[0] & 0xfc) >> 2];
                result += chars[((bytes[0] & 0x03) << 4) | ((bytes[1] & 0xf0) >> 4)];
                result += chars[((bytes[1] & 0x0f) << 2) | ((bytes[2] & 0xc0) >> 6)];
                result += chars[bytes[2] & 0x3f];
                i = 0;
            }
        }
        if (i) {
            for (int j = i; j < 3; j++) bytes[j] = '\0';
            result += chars[(bytes[0] & 0xfc) >> 2];
            result += chars[((bytes[0] & 0x03) << 4) | ((bytes[1] & 0xf0) >> 4)];
            result += (i == 2) ? chars[((bytes[1] & 0x0f) << 2)] : '=';
            result += '=';
        }
        return result;
    }

    static std::string base64Decode(const std::string& input) {
        static const int T[256] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
            52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
            15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
            41,42,43,44,45,46,47,48,49,50,51
        };
        std::string result;
        int val = 0, bits = -8;
        for (unsigned char c : input) {
            if (c == '=' || c >= 128) break;
            int v = T[c]; if (v == -1) continue;
            val = (val << 6) + v; bits += 6;
            if (bits >= 0) { result.push_back(char((val >> bits) & 0xFF)); bits -= 8; }
        }
        return result;
    }
};
