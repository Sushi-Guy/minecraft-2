#pragma once

// Nakama REST client - uses WinHTTP (built into Windows, works with MinGW)
// Handles authentication and multiplayer position syncing via storage API.

#include <windows.h>
#include <winhttp.h>
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

#pragma comment(lib, "winhttp")

struct NakamaSession {
    std::string token;
    std::string userId;
    bool isValid = false;
};

// Represents another player's state received over the network
struct RemotePlayer {
    std::string userId;
    float x, y, z;
    float yaw, pitch;
    bool active = true;
};

// A block event (break or place) broadcast to all players
struct BlockEvent {
    std::string type;   // "break" or "place"
    int index;          // block index in level array (for break)
    float minX, maxX, minY, maxY, minZ, maxZ; // (for place)
    int textureId;      // (for place) - serialized as int
    ULONGLONG seq;      // sequence number for deduplication
};

class NakamaClient {
public:
    std::string serverKey;
    std::string host;
    int port;
    
    // Match state
    std::mutex playersMutex;
    std::map<std::string, RemotePlayer> remotePlayers;
    std::atomic<bool> inMatch{false};
    ULONGLONG lastSeenEventSeq = 0; // track which block events we've already applied

    NakamaClient(const std::string& key = "defaultkey", const std::string& h = "127.0.0.1", int p = 7350) 
        : serverKey(key), host(h), port(p) {}

    ~NakamaClient() {
        inMatch = false;
    }

    // ============================
    // Block sync: send block event
    // ============================
    void sendBlockBreak(const NakamaSession& session, int blockIndex) {
        if (!inMatch) return;
        ULONGLONG seq = GetTickCount64();
        char valueBuf[256];
        snprintf(valueBuf, sizeof(valueBuf),
            "{\\\"type\\\":\\\"break\\\",\\\"idx\\\":%d,\\\"seq\\\":%llu}",
            blockIndex, seq);
        _sendBlockEvent(session, valueBuf, seq);
    }

    void sendBlockPlace(const NakamaSession& session, float minX, float maxX,
                        float minY, float maxY, float minZ, float maxZ, int texId) {
        if (!inMatch) return;
        ULONGLONG seq = GetTickCount64();
        char valueBuf[512];
        snprintf(valueBuf, sizeof(valueBuf),
            "{\\\"type\\\":\\\"place\\\",\\\"minX\\\":%.3f,\\\"maxX\\\":%.3f,"
            "\\\"minY\\\":%.3f,\\\"maxY\\\":%.3f,"
            "\\\"minZ\\\":%.3f,\\\"maxZ\\\":%.3f,"
            "\\\"tex\\\":%d,\\\"seq\\\":%llu}",
            minX, maxX, minY, maxY, minZ, maxZ, texId, seq);
        _sendBlockEvent(session, valueBuf, seq);
    }

    // Poll block events and call callback for each new one
    void pollBlockEvents(const NakamaSession& session,
                         std::function<void(const BlockEvent&)> onEvent) {
        if (!inMatch) return;
        std::thread([this, session, onEvent]() {
            std::string response = httpRequest("GET",
                "/v2/storage/blockevents?limit=100",
                "", session.token);
            if (response.empty()) return;

            size_t searchPos = 0;
            ULONGLONG maxSeq = lastSeenEventSeq;
            std::vector<std::pair<ULONGLONG, BlockEvent>> events;

            while (true) {
                size_t valuePos = response.find("\"value\"", searchPos);
                if (valuePos == std::string::npos) break;

                // Find the object start ('{') before this "value" field to locate user_id
                size_t objStart = response.rfind('{', valuePos);
                std::string objHeader = (objStart != std::string::npos)
                    ? response.substr(objStart, valuePos - objStart)
                    : "";

                // Extract user_id for this object and skip our own events
                std::string ownerId = extractJsonString(objHeader, "user_id");
                if (ownerId == session.userId) {
                    searchPos = valuePos + 10;
                    continue;
                }

                // Extract and unescape the value
                std::string valueStr = extractJsonStringAt(response, valuePos);
                std::string unescaped;
                for (size_t i = 0; i < valueStr.size(); i++) {
                    if (valueStr[i] == '\\' && i + 1 < valueStr.size()) { unescaped += valueStr[i+1]; i++; }
                    else unescaped += valueStr[i];
                }

                ULONGLONG seq = extractJsonULL(unescaped, "seq");
                if (seq > lastSeenEventSeq) {
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
                    events.push_back({seq, ev});
                    if (seq > maxSeq) maxSeq = seq;
                }
                searchPos = valuePos + 10;
            }

            std::sort(events.begin(), events.end(),
                [](const auto& a, const auto& b){ return a.first < b.first; });

            lastSeenEventSeq = maxSeq;

            for (const auto& pair : events) {
                onEvent(pair.second);
            }
        }).detach();
    }

    // ========================
    // REST API: Authentication
    // ========================
    void authenticateDevice(const std::string& deviceId,
                            std::function<void(NakamaSession)> successCb,
                            std::function<void(std::string)> errorCb) {
        std::thread([this, deviceId, successCb, errorCb]() {
            std::string body = "{\"id\":\"" + deviceId + "\"}";
            std::string response = httpRequest("POST", "/v2/account/authenticate/device?create=true", body, "");
            
            if (response.empty()) {
                if (errorCb) errorCb("Failed to connect to Nakama server");
                return;
            }

            // Extract token
            std::string token = extractJsonString(response, "token");
            if (token.empty()) {
                if (errorCb) errorCb("Invalid response: " + response);
                return;
            }

            NakamaSession session;
            session.token = token;
            session.isValid = true;

            // Decode user ID from JWT payload
            size_t dot1 = token.find('.');
            size_t dot2 = token.find('.', dot1 + 1);
            if (dot1 != std::string::npos && dot2 != std::string::npos) {
                std::string payload = base64Decode(token.substr(dot1 + 1, dot2 - dot1 - 1));
                session.userId = extractJsonString(payload, "uid");
            }

            if (successCb) successCb(session);
        }).detach();
    }

    // ===========================================
    // Multiplayer: Write our position to storage
    // ===========================================
    void sendPosition(const NakamaSession& session, float x, float y, float z, float yaw, float pitch) {
        if (!inMatch || session.userId.empty()) return;

        // Nakama storage write: PUT /v2/storage
        // Value must be a JSON string (escaped), wrapped in "objects" array
        ULONGLONG nowMs = GetTickCount64();
        char valueBuf[256];
        snprintf(valueBuf, sizeof(valueBuf),
            "{\\\"x\\\":%.3f,\\\"y\\\":%.3f,\\\"z\\\":%.3f,\\\"yaw\\\":%.3f,\\\"pitch\\\":%.3f,\\\"ts\\\":%llu}",
            x, y, z, yaw, pitch, nowMs);

        std::string body = "{\"objects\":[{"
            "\"collection\":\"positions\","
            "\"key\":\"current\","
            "\"value\":\"" + std::string(valueBuf) + "\","
            "\"permission_read\":2,"
            "\"permission_write\":1"
            "}]}";

        std::thread([this, session, body]() {
            std::string resp = httpRequest("PUT", "/v2/storage", body, session.token);
            // Silently ignore errors for position updates
        }).detach();
    }

    // ===========================================
    // Multiplayer: Read other players' positions
    // ===========================================
    void pollPositions(const NakamaSession& session) {
        if (!inMatch) return;

        std::thread([this, session]() {
            // List ALL public objects in "positions" collection (empty user_id = all users)
            std::string response = httpRequest("GET", 
                "/v2/storage/positions?limit=50", 
                "", session.token);
            
            if (response.empty()) return;

            std::lock_guard<std::mutex> lock(playersMutex);

            // Parse response - find all objects with user_id and value
            // Response format: {"objects":[{"collection":"positions","key":"current",
            //   "user_id":"xxx","value":"...","version":"..."},...],...}
            
            // Mark all players inactive, then reactivate ones we find
            for (auto& pair : remotePlayers) {
                pair.second.active = false;
            }

            size_t searchPos = 0;
            while (true) {
                // Find next object block
                size_t objStart = response.find("\"user_id\"", searchPos);
                if (objStart == std::string::npos) break;
                
                // Extract user_id
                std::string uid = extractJsonStringAt(response, objStart);
                
                // Find the "value" field near this user_id
                size_t valuePos = response.find("\"value\"", objStart);
                if (valuePos == std::string::npos || valuePos - objStart > 500) {
                    searchPos = objStart + 10;
                    continue;
                }
                
                std::string valueStr = extractJsonStringAt(response, valuePos);
                
                // The value is an escaped JSON string, unescape it
                std::string unescaped;
                for (size_t i = 0; i < valueStr.size(); i++) {
                    if (valueStr[i] == '\\' && i + 1 < valueStr.size()) {
                        unescaped += valueStr[i + 1];
                        i++;
                    } else {
                        unescaped += valueStr[i];
                    }
                }

                // Skip our own position
                if (uid != session.userId && !uid.empty()) {
                    // Check timestamp - ignore players idle for > 5 seconds
                    ULONGLONG nowMs = GetTickCount64();
                    ULONGLONG theirTs = (ULONGLONG)extractJsonFloat(unescaped, "ts");
                    // theirTs is in ms; skip if older than 5000ms
                    if (theirTs > 0 && nowMs > theirTs && (nowMs - theirTs) > 5000) {
                        remotePlayers.erase(uid);
                        searchPos = valuePos + 10;
                        continue;
                    }

                    RemotePlayer rp;
                    rp.userId = uid;
                    rp.x = extractJsonFloat(unescaped, "x");
                    rp.y = extractJsonFloat(unescaped, "y");
                    rp.z = extractJsonFloat(unescaped, "z");
                    rp.yaw = extractJsonFloat(unescaped, "yaw");
                    rp.pitch = extractJsonFloat(unescaped, "pitch");
                    rp.active = true;
                    remotePlayers[uid] = rp;
                }

                searchPos = valuePos + 10;
            }

            // Remove inactive players
            for (auto it = remotePlayers.begin(); it != remotePlayers.end();) {
                if (!it->second.active) it = remotePlayers.erase(it);
                else ++it;
            }
        }).detach();
    }

    // Get a snapshot of the current remote players (thread-safe)
    std::map<std::string, RemotePlayer> getRemotePlayers() {
        std::lock_guard<std::mutex> lock(playersMutex);
        return remotePlayers;
    }

    void leaveMatch() {
        inMatch = false;
        std::lock_guard<std::mutex> lock(playersMutex);
        remotePlayers.clear();
    }

private:
    // Write a block event to the server using current user's storage slot
    void _sendBlockEvent(const NakamaSession& session, const char* valueBuf, ULONGLONG seq) {
        std::string body = std::string("{\"objects\":[{")
            + "\"collection\":\"blockevents\","
            + "\"key\":\"latest\","
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
    // NOTE: Nakama re-serializes stored JSON with spaces after colons (e.g. "key": "value"),
    // so all extractors must skip optional whitespace after the colon.
    static std::string extractJsonString(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t start = json.find(search);
        if (start == std::string::npos) return "";
        start += search.length();
        // Skip optional whitespace before the opening quote
        while (start < json.size() && json[start] == ' ') start++;
        if (start >= json.size() || json[start] != '"') return "";
        start++; // skip opening quote
        size_t end = start;
        while (end < json.size() && json[end] != '"') {
            if (json[end] == '\\') end++; // skip escaped chars
            end++;
        }
        return json.substr(start, end - start);
    }

    // Extract string value from a "key":"value" starting at a known position of "key"
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

    // Extract a 64-bit unsigned integer JSON value (avoids float precision loss)
    static ULONGLONG extractJsonULL(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t start = json.find(search);
        if (start == std::string::npos) return 0;
        start += search.length();
        while (start < json.size() && (json[start] == ' ' || json[start] == '"')) start++;
        size_t end = start;
        while (end < json.size() && json[end] >= '0' && json[end] <= '9') end++;
        if (end == start) return 0;
        try { return std::stoull(json.substr(start, end - start)); }
        catch (...) { return 0; }
    }

    // ========================
    // HTTP
    // ========================
    std::string httpRequest(const std::string& method, const std::string& path, 
                            const std::string& body, const std::string& bearerToken) {
        std::wstring wHost(host.begin(), host.end());
        std::wstring wMethod(method.begin(), method.end());
        
        HINTERNET hSession = WinHttpOpen(L"NakamaClient/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return "";

        HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), port, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

        std::wstring wPath(path.begin(), path.end());
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, wMethod.c_str(), wPath.c_str(),
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

        // Auth header
        if (!bearerToken.empty()) {
            std::string authHeader = "Authorization: Bearer " + bearerToken;
            std::wstring wAuth(authHeader.begin(), authHeader.end());
            WinHttpAddRequestHeaders(hRequest, wAuth.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
        } else {
            std::string authStr = serverKey + ":";
            std::string authHeader = "Authorization: Basic " + base64Encode(authStr);
            std::wstring wAuth(authHeader.begin(), authHeader.end());
            WinHttpAddRequestHeaders(hRequest, wAuth.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
        }

        WinHttpAddRequestHeaders(hRequest, L"Content-Type: application/json", -1, WINHTTP_ADDREQ_FLAG_ADD);

        BOOL bResults;
        if (!body.empty()) {
            bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                (LPVOID)body.c_str(), body.length(), body.length(), 0);
        } else {
            bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        }

        if (!bResults) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }
        bResults = WinHttpReceiveResponse(hRequest, NULL);
        if (!bResults) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

        std::string result;
        DWORD dwSize = 0, dwDownloaded = 0;
        do {
            dwSize = 0;
            WinHttpQueryDataAvailable(hRequest, &dwSize);
            if (dwSize == 0) break;
            char* buffer = new char[dwSize + 1];
            ZeroMemory(buffer, dwSize + 1);
            WinHttpReadData(hRequest, buffer, dwSize, &dwDownloaded);
            result.append(buffer, dwDownloaded);
            delete[] buffer;
        } while (dwSize > 0);

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // ========================
    // Base64
    // ========================
    static std::string base64Encode(const std::string& input) {
        static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        int i = 0;
        unsigned char bytes[3];
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
            if (i == 2) result += chars[((bytes[1] & 0x0f) << 2)];
            else result += '=';
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
            int v = T[c];
            if (v == -1) continue;
            val = (val << 6) + v;
            bits += 6;
            if (bits >= 0) {
                result.push_back(char((val >> bits) & 0xFF));
                bits -= 8;
            }
        }
        return result;
    }
};
