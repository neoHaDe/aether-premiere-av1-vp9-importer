// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 neoHaDe

#include "AV1Settings.h"
#include "IniSettings.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <vector>

namespace av1imp {

namespace {

const DWORD kSoftwareNeedsThreads = 8;

const uint32_t kMemoryChoices[] = { 0, 128, 256, 512, 1024, 2048, 4096 };
const uint32_t kPreviewMinMB = 256;
const uint32_t kPreviewMaxMB = 20480;
const uint32_t kPreviewDefaultMB = 2048;

bool LooksDisabled(const wchar_t* v)
{
    return _wcsicmp(v, L"0") == 0 || _wcsicmp(v, L"off") == 0 ||
           _wcsicmp(v, L"false") == 0 || _wcsicmp(v, L"no") == 0;
}

bool LooksDisabledNarrow(const char* v)
{
    return _stricmp(v, "off") == 0 || _stricmp(v, "0") == 0 ||
           _stricmp(v, "false") == 0 || _stricmp(v, "no") == 0;
}

bool LooksEnabledNarrow(const char* v)
{
    return _stricmp(v, "on") == 0 || _stricmp(v, "1") == 0 ||
           _stricmp(v, "true") == 0 || _stricmp(v, "yes") == 0;
}

std::mutex g_iniMutex;
bool g_iniLoaded = false;
std::string g_iniText;
bool g_settingsParsed = false;
Settings g_settings;
wchar_t g_settingsOverride[MAX_PATH] = {};

std::string LoadIniTextUnlocked()
{
    if (g_iniLoaded) return g_iniText;
    g_iniLoaded = true;

    const wchar_t* path = SettingsFilePath();
    if (!path[0]) return g_iniText;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) return g_iniText;

    char chunk[4096];
    size_t got = 0;
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        g_iniText.append(chunk, got);
    }
    fclose(f);
    return g_iniText;
}

std::string LoadIniText()
{
    std::lock_guard<std::mutex> lock(g_iniMutex);
    return LoadIniTextUnlocked();
}

void RememberIniText(std::string text)
{
    std::lock_guard<std::mutex> lock(g_iniMutex);
    g_iniText = std::move(text);
    g_iniLoaded = true;
    g_settingsParsed = false;
}

const wchar_t* SettingsFolder()
{
    static wchar_t folder[MAX_PATH] = {};
    if (folder[0]) return folder;

    wchar_t* base = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
        swprintf_s(folder, MAX_PATH, L"%s\\Aether", base);

        wchar_t oldFile[MAX_PATH] = {};
        wchar_t newFile[MAX_PATH] = {};
        swprintf_s(oldFile, MAX_PATH, L"%s\\AV1Importer\\settings.ini", base);
        swprintf_s(newFile, MAX_PATH, L"%s\\Aether\\settings.ini", base);
        CoTaskMemFree(base);

        if (GetFileAttributesW(newFile) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(oldFile) != INVALID_FILE_ATTRIBUTES) {
            CreateDirectoryW(folder, nullptr);
            CopyFileW(oldFile, newFile, TRUE);
        }
    }
    return folder;
}

uint32_t ParseU32(const std::string& text, uint32_t fallback)
{
    if (text.empty()) return fallback;
    errno = 0;
    char* end = nullptr;
    const unsigned long v = strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE) return fallback;
    if (v > 0xFFFFFFFFul) return fallback;
    return (uint32_t)v;
}

DecodeMode ParseDecode(const std::string& value)
{
    if (_stricmp(value.c_str(), "software") == 0) return DecodeMode::Software;
    if (_stricmp(value.c_str(), "hardware") == 0) return DecodeMode::Hardware;
    return DecodeMode::Auto;
}

const char* DecodeName(DecodeMode mode)
{
    switch (mode) {
        case DecodeMode::Software: return "software";
        case DecodeMode::Hardware: return "hardware";
        default: return "auto";
    }
}

bool ParseOnOff(const std::string& value, bool fallback)
{
    if (LooksDisabledNarrow(value.c_str())) return false;
    if (LooksEnabledNarrow(value.c_str())) return true;
    return fallback;
}

Settings ParseFileSettings(const std::string& text)
{
    Settings s;
    std::string value;
    if (GetIniValue(text, "enabled", &value)) s.enabled = ParseOnOff(value, true);
    value.clear();
    if (GetIniValue(text, "decode", &value)) s.decode = ParseDecode(value);
    value.clear();
    if (GetIniValue(text, "cache_memory_mb", &value)) {
        s.memoryCacheMB = SnapMemoryCacheMB(ParseU32(value, 512));
    }
    value.clear();
    if (GetIniValue(text, "preview_cache", &value)) {
        s.previewCache = ParseOnOff(value, false);
    }
    value.clear();
    if (GetIniValue(text, "preview_cache_mb", &value)) {
        s.previewCacheMB = ClampPreviewCacheMB(ParseU32(value, kPreviewDefaultMB));
    }
    value.clear();
    // Значение не разбираем и не проверяем: ядру оно ни о чём не говорит,
    // а разберёт его тот, кто показывает надписи. Неизвестное слово там
    // означает «как в системе», поэтому испортить настройку опечаткой нельзя.
    if (GetIniValue(text, "lang", &value)) s.language = value;
    return s;
}

Settings ApplyEnv(Settings s)
{
    wchar_t env[64] = {};
    if (GetEnvironmentVariableW(L"AETHER_ENABLED", env, 64) > 0) {
        s.enabled = !LooksDisabled(env);
    }
    if (GetEnvironmentVariableW(L"AV1IMPORTER_HARDWARE", env, 64) > 0) {
        s.decode = LooksDisabled(env) ? DecodeMode::Software : DecodeMode::Hardware;
    }
    if (GetEnvironmentVariableW(L"AETHER_CACHE_MB", env, 64) > 0) {
        char narrow[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, env, -1, narrow, (int)sizeof(narrow), nullptr, nullptr);
        s.memoryCacheMB = SnapMemoryCacheMB(ParseU32(narrow, s.memoryCacheMB));
    }
    if (GetEnvironmentVariableW(L"AETHER_PREVIEW_CACHE", env, 64) > 0) {
        s.previewCache = !LooksDisabled(env);
    }
    if (GetEnvironmentVariableW(L"AETHER_PREVIEW_CACHE_MB", env, 64) > 0) {
        char narrow[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, env, -1, narrow, (int)sizeof(narrow), nullptr, nullptr);
        s.previewCacheMB = ClampPreviewCacheMB(ParseU32(narrow, s.previewCacheMB));
    }
    return s;
}

bool WriteSettingsFile(const std::string& updated)
{
    const wchar_t* folder = SettingsFolder();
    if (!folder[0]) return false;
    if (!CreateDirectoryW(folder, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }

    const wchar_t* path = SettingsFilePath();
    std::wstring temporary = path;
    temporary += L".tmp";

    FILE* output = nullptr;
    if (_wfopen_s(&output, temporary.c_str(), L"wb") != 0 || !output) return false;
    const bool written =
        fwrite(updated.data(), 1, updated.size(), output) == updated.size() &&
        fflush(output) == 0;
    const bool closed = fclose(output) == 0;
    if (!written || !closed) {
        DeleteFileW(temporary.c_str());
        return false;
    }

    if (!MoveFileExW(temporary.c_str(), path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    RememberIniText(updated);
    return true;
}

bool EnabledUnlessTurnedOff(const wchar_t* envName, const char* key)
{
    wchar_t env[64] = {};
    if (GetEnvironmentVariableW(envName, env, 64) > 0) {
        return !LooksDisabled(env);
    }

    const std::string text = LoadIniText();
    std::string value;
    if (!GetIniValue(text, key, &value)) return true;
    return !LooksDisabledNarrow(value.c_str());
}

std::string JsonEscape(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if (c == '\\' || c == '"') {
            out += '\\';
            out += (char)c;
        } else if (c < 0x20) {
            char buf[8];
            sprintf_s(buf, "\\u%04x", c);
            out += buf;
        } else {
            out += (char)c;
        }
    }
    return out;
}

} // namespace

uint32_t SnapMemoryCacheMB(uint32_t mb)
{
    uint32_t best = kMemoryChoices[0];
    uint32_t bestDist = mb > best ? mb - best : best - mb;
    for (uint32_t choice : kMemoryChoices) {
        const uint32_t dist = mb > choice ? mb - choice : choice - mb;
        if (dist < bestDist) {
            best = choice;
            bestDist = dist;
        }
    }
    return best;
}

uint32_t ClampPreviewCacheMB(uint32_t mb)
{
    if (mb < kPreviewMinMB) return kPreviewMinMB;
    if (mb > kPreviewMaxMB) return kPreviewMaxMB;
    return mb;
}

const wchar_t* SettingsFilePath()
{
    static wchar_t path[MAX_PATH] = {};
    if (g_settingsOverride[0]) return g_settingsOverride;
    if (path[0]) return path;

    const wchar_t* folder = SettingsFolder();
    if (folder[0]) swprintf_s(path, MAX_PATH, L"%s\\settings.ini", folder);
    return path;
}

void SetSettingsFilePathForTests(const wchar_t* path)
{
    std::lock_guard<std::mutex> lock(g_iniMutex);
    if (path && path[0]) {
        wcsncpy_s(g_settingsOverride, path, _TRUNCATE);
    } else {
        g_settingsOverride[0] = 0;
    }
    g_iniText.clear();
    g_iniLoaded = false;
    g_settingsParsed = false;
}

void ReloadSettingsForTests()
{
    std::lock_guard<std::mutex> lock(g_iniMutex);
    g_iniText.clear();
    g_iniLoaded = false;
    g_settingsParsed = false;
}

Settings CurrentSettings()
{
    std::lock_guard<std::mutex> lock(g_iniMutex);
    if (!g_settingsParsed) {
        g_settings = ApplyEnv(ParseFileSettings(LoadIniTextUnlocked()));
        g_settingsParsed = true;
    }
    return g_settings;
}

bool SaveSettings(const Settings& settings)
{
    const Settings clamped = {
        settings.enabled,
        settings.decode,
        SnapMemoryCacheMB(settings.memoryCacheMB),
        settings.previewCache,
        ClampPreviewCacheMB(settings.previewCacheMB),
        settings.language.empty() ? std::string("auto") : settings.language,
    };

    std::string existing;
    FILE* input = nullptr;
    const wchar_t* path = SettingsFilePath();
    const errno_t openResult = _wfopen_s(&input, path, L"rb");
    if (openResult == 0 && input) {
        char chunk[4096];
        size_t got = 0;
        while ((got = fread(chunk, 1, sizeof(chunk), input)) > 0) {
            existing.append(chunk, got);
        }
        if (ferror(input)) {
            fclose(input);
            return false;
        }
        fclose(input);
    } else {
        const DWORD attributes = GetFileAttributesW(path);
        const DWORD error = GetLastError();
        if (attributes != INVALID_FILE_ATTRIBUTES ||
            (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)) {
            return false;
        }
    }

    if (existing.empty()) {
        existing = "; AV1 / VP9 Importer for Premiere Pro\r\n"
                   "; enabled = on | off\r\n"
                   "; decode = auto | software | hardware\r\n"
                   "; cache_memory_mb = 0 | 128 | 256 | 512 | 1024 | 2048 | 4096\r\n"
                   "; preview_cache = on | off\r\n"
                   "; preview_cache_mb = 256 .. 20480\r\n";
    }

    std::string updated = existing;
    updated = SetIniValue(updated, "enabled", clamped.enabled ? "on" : "off");
    updated = SetIniValue(updated, "decode", DecodeName(clamped.decode));
    updated = SetIniValue(updated, "cache_memory_mb", std::to_string(clamped.memoryCacheMB));
    updated = SetIniValue(updated, "preview_cache", clamped.previewCache ? "on" : "off");
    updated = SetIniValue(updated, "preview_cache_mb", std::to_string(clamped.previewCacheMB));
    updated = SetIniValue(updated, "lang",
                          clamped.language.empty() ? "auto" : clamped.language);
    return WriteSettingsFile(updated);
}

uint64_t MemoryCacheLimitBytes()
{
    return (uint64_t)CurrentSettings().memoryCacheMB * 1024ull * 1024ull;
}

bool PluginEnabled()
{
    return CurrentSettings().enabled;
}

std::string SettingsJson()
{
    const Settings s = CurrentSettings();
    const std::string file = [] {
        const wchar_t* path = SettingsFilePath();
        if (!path || !path[0]) return std::string();
        const int need = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
        if (need <= 1) return std::string();
        std::string out((size_t)need, '\0');
        WideCharToMultiByte(CP_UTF8, 0, path, -1, &out[0], need, nullptr, nullptr);
        out.pop_back();
        return out;
    }();

    std::string json = "{";
    json += "\"enabled\":";
    json += s.enabled ? "true" : "false";
    json += ",\"decode\":\"";
    json += DecodeName(s.decode);
    json += "\",\"lang\":\"";
    json += s.language.empty() ? "auto" : s.language;
    json += "\",\"cache_memory_mb\":";
    json += std::to_string(s.memoryCacheMB);
    json += ",\"preview_cache\":";
    json += s.previewCache ? "true" : "false";
    json += ",\"preview_cache_mb\":";
    json += std::to_string(s.previewCacheMB);
    json += ",\"file\":\"";
    json += JsonEscape(file);
    json += "\"}";
    return json;
}

DecodeMode CurrentMode()
{
    return CurrentSettings().decode;
}

bool SaveMode(DecodeMode mode)
{
    Settings s = ParseFileSettings(LoadIniText());
    s.decode = mode;
    return SaveSettings(s);
}

bool PreferHardware()
{
    switch (CurrentMode()) {
        case DecodeMode::Software: return false;
        case DecodeMode::Hardware: return true;
        default: break;
    }

    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors < kSoftwareNeedsThreads;
}

bool AsyncDeliveryEnabled()
{
    return EnabledUnlessTurnedOff(L"AETHER_ASYNC", "async");
}

bool YuvEnabled()
{
    return EnabledUnlessTurnedOff(L"AETHER_YUV", "yuv");
}

bool Yuv10Enabled()
{
    return EnabledUnlessTurnedOff(L"AETHER_YUV10", "yuv10");
}

} // namespace av1imp
