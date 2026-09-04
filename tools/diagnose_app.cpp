// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 neoHaDe

// AetherDiagnose.exe — диагностика без окна.
//
//   AetherDiagnose.exe [--json] [файл]
//   AetherDiagnose.exe --settings-json
//   AetherDiagnose.exe --set enabled=on decode=auto cache_memory_mb=512 preview_cache=on preview_cache_mb=2048
//   AetherDiagnose.exe --cache-json
//   AetherDiagnose.exe --clear-preview-cache

#include "app_diagnose.h"
#include "localization.h"

#include "../src/AV1Settings.h"
#include "../src/PreviewCache.h"

#include <windows.h>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <io.h>
#include <fcntl.h>
#include <string>

namespace {

std::wstring Utf8Wide(const std::string& utf8)
{
    if (utf8.empty()) return {};
    const int need = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (need <= 1) return {};
    std::wstring out((size_t)need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &out[0], need);
    out.pop_back();
    return out;
}

std::string WideUtf8(const wchar_t* wide)
{
    if (!wide || !*wide) return {};
    const int need = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 1) return {};
    std::string out((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &out[0], need, nullptr, nullptr);
    out.pop_back();
    return out;
}

bool SplitKV(const wchar_t* arg, std::string* key, std::string* value)
{
    const wchar_t* eq = wcschr(arg, L'=');
    if (!eq || eq == arg) return false;
    *key = WideUtf8(std::wstring(arg, eq).c_str());
    *value = WideUtf8(eq + 1);
    return !key->empty();
}

bool ApplySet(av1imp::Settings* s, const std::string& key, const std::string& value, std::string* err)
{
    if (key == "enabled") {
        if (value == "on" || value == "true" || value == "1") { s->enabled = true; return true; }
        if (value == "off" || value == "false" || value == "0") { s->enabled = false; return true; }
        *err = "invalid enabled value";
        return false;
    }
    if (key == "lang") {
        // Принимаем только то, что понимаем, и отказываем на остальном.
        // Настройка живёт строкой и разбирается при показе, но записать
        // в файл опечатку с чужого конца — значит потом гадать, почему
        // язык «не переключается»: он бы молча падал в auto.
        if (value == "auto" || value == "ru" || value == "en") {
            s->language = value;
            return true;
        }
        *err = "invalid lang value";
        return false;
    }
    if (key == "decode") {
        if (value == "auto") { s->decode = av1imp::DecodeMode::Auto; return true; }
        if (value == "software") { s->decode = av1imp::DecodeMode::Software; return true; }
        if (value == "hardware") { s->decode = av1imp::DecodeMode::Hardware; return true; }
        *err = "invalid decode value";
        return false;
    }
    if (key == "cache_memory_mb") {
        errno = 0;
        char* end = nullptr;
        const unsigned long v = strtoul(value.c_str(), &end, 10);
        if (!end || end == value.c_str() || *end != '\0' || errno == ERANGE) {
            *err = "invalid cache_memory_mb"; return false;
        }
        s->memoryCacheMB = av1imp::SnapMemoryCacheMB((uint32_t)v);
        return true;
    }
    if (key == "preview_cache") {
        if (value == "on" || value == "true" || value == "1") { s->previewCache = true; return true; }
        if (value == "off" || value == "false" || value == "0") { s->previewCache = false; return true; }
        *err = "invalid preview_cache value";
        return false;
    }
    if (key == "preview_cache_mb") {
        errno = 0;
        char* end = nullptr;
        const unsigned long v = strtoul(value.c_str(), &end, 10);
        if (!end || end == value.c_str() || *end != '\0' || errno == ERANGE) {
            *err = "invalid preview_cache_mb"; return false;
        }
        s->previewCacheMB = av1imp::ClampPreviewCacheMB((uint32_t)v);
        return true;
    }
    *err = "unknown key";
    return false;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    bool json = false;
    bool settingsJson = false;
    bool cacheJson = false;
    bool clearCache = false;
    bool doSet = false;
    std::wstring file;

    av1imp::Settings pending = av1imp::CurrentSettings();
    bool haveSet = false;

    // Язык берём из настроек, а панель может перебить его ключом --lang:
    // ей нужно показать отчёт на том языке, который выбран в ней самой,
    // не дожидаясь, пока выбор доедет до файла настроек.
    aether::SetLanguage(aether::ParseLanguage(
        Utf8Wide(av1imp::CurrentSettings().language).c_str()));

    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--lang") == 0 && i + 1 < argc) {
            aether::SetLanguage(aether::ParseLanguage(argv[++i]));
        }
        else if (wcscmp(argv[i], L"--json") == 0) json = true;
        else if (wcscmp(argv[i], L"--settings-json") == 0) settingsJson = true;
        else if (wcscmp(argv[i], L"--cache-json") == 0) cacheJson = true;
        else if (wcscmp(argv[i], L"--clear-preview-cache") == 0) clearCache = true;
        else if (wcscmp(argv[i], L"--set") == 0) {
            doSet = true;
        } else if (doSet && wcschr(argv[i], L'=')) {
            std::string key, value, err;
            if (!SplitKV(argv[i], &key, &value) || !ApplySet(&pending, key, value, &err)) {
                _setmode(_fileno(stderr), _O_U8TEXT);
                fwprintf(stderr, L"{\"ok\":false,\"error\":\"%s\"}\n",
                         Utf8Wide(err.empty() ? "bad assignment" : err).c_str());
                return 2;
            }
            haveSet = true;
        } else if (doSet) {
            _setmode(_fileno(stderr), _O_U8TEXT);
            fwprintf(stderr, L"{\"ok\":false,\"error\":\"expected key=value\"}\n");
            return 2;
        } else if (file.empty() && argv[i][0] != L'-') {
            file = argv[i];
        } else if (argv[i][0] == L'-') {
            _setmode(_fileno(stderr), _O_U8TEXT);
            fwprintf(stderr, L"unknown argument\n");
            return 2;
        }
    }

    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);

    if (doSet && !haveSet) {
        fwprintf(stdout, L"{\"ok\":false,\"error\":\"no settings supplied\"}\n");
        return 2;
    }

    if (haveSet) {
        if (!av1imp::SaveSettings(pending)) {
            fwprintf(stdout, L"{\"ok\":false,\"error\":\"save failed\"}\n");
            return 1;
        }
        av1imp::ReloadSettingsForTests();
        fwprintf(stdout, L"%s\n", Utf8Wide(av1imp::SettingsJson()).c_str());
        return 0;
    }

    if (settingsJson) {
        fwprintf(stdout, L"%s\n", Utf8Wide(av1imp::SettingsJson()).c_str());
        return 0;
    }

    if (clearCache) {
        av1imp::PreviewCache::Instance().Clear();
        fwprintf(stdout, L"%s\n", Utf8Wide(av1imp::PreviewCache::Instance().Json()).c_str());
        return 0;
    }

    if (cacheJson) {
        fwprintf(stdout, L"%s\n", Utf8Wide(av1imp::PreviewCache::Instance().Json()).c_str());
        return 0;
    }

    const aether::Report r = aether::Run(file, [json](const std::wstring& step) {
        fwprintf(stderr, L"  ... %s\n", step.c_str());
    });

    fwprintf(stdout, L"%s", json ? r.Json().c_str() : r.PlainText().c_str());
    return r.AnyFailed() ? 1 : 0;
}
