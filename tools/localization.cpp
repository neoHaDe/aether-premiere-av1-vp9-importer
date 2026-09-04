// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 neoHaDe

#include "localization.h"

#include <windows.h>

#include <cstring>
#include <cwchar>

namespace aether {

namespace {

// Выбранный язык живёт в обычной переменной, а не в атомике: ставится он
// один раз при запуске, до того как появятся другие потоки. Диагностика
// работает в своём потоке, но язык к тому времени уже выбран и больше
// не меняется — окно пересоздаёт отчёт целиком, а не правит его на ходу.
Language g_language = Language::English;

} // namespace

Language DetectLanguage()
{
    // Язык ИНТЕРФЕЙСА, а не раскладки и не региона. Человек с русской
    // Windows и английской раскладкой ждёт русский; человек с английской
    // Windows в России — английский.
    const LANGID id = GetUserDefaultUILanguage();
    return (PRIMARYLANGID(id) == LANG_RUSSIAN) ? Language::Russian
                                               : Language::English;
}

Language ParseLanguage(const wchar_t* code)
{
    if (!code || !*code) return DetectLanguage();
    if (_wcsicmp(code, L"ru") == 0 || _wcsicmp(code, L"rus") == 0) {
        return Language::Russian;
    }
    if (_wcsicmp(code, L"en") == 0 || _wcsicmp(code, L"eng") == 0) {
        return Language::English;
    }
    // Сюда попадает и "auto", и опечатка. Разницы нет: в обоих случаях
    // спрашиваем систему, а не гадаем.
    return DetectLanguage();
}

Language ParseLanguage(const char* code)
{
    if (!code || !*code) return DetectLanguage();
    if (_stricmp(code, "ru") == 0 || _stricmp(code, "rus") == 0) return Language::Russian;
    if (_stricmp(code, "en") == 0 || _stricmp(code, "eng") == 0) return Language::English;
    return DetectLanguage();
}

const wchar_t* LanguageCode(Language language)
{
    return language == Language::Russian ? L"ru" : L"en";
}

void SetLanguage(Language language) { g_language = language; }

Language CurrentLanguage() { return g_language; }

} // namespace aether
