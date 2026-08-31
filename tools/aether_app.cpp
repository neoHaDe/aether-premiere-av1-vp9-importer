// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 neoHaDe

// Aether.exe — окно плагина: настройки и диагностика.
//
// Отдельная программа, а не панель внутри Premiere, и это осознанно: она нужна
// ровно тогда, когда Premiere из-за поломки в драйвере не запускается, а также
// через полгода после установки, когда установщика давно нет.
//
// Одна программа на две страницы, а не две программы: окно, шрифты, разметка
// и пересчёт под DPI пишутся один раз. Установщик зовёт `Aether.exe --diagnose`
// с последней страницы, и человек сразу попадает куда надо.
//
// Чистый Win32 без сторонних библиотек: программа ставится вместе с плагином,
// и тащить ради двух страниц лишние зависимости незачем.
//
// Все размеры даны в точках при 96 DPI и проходят через Dp(). Голых чисел
// в координатах быть не должно ни одного — иначе на экране со 150% разъедется.

#include "app_diagnose.h"

#include "../src/AV1Settings.h"
#include "../src/PreviewCache.h"
#include "../src/AV1Version.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace {

enum {
    ID_NAV_SETTINGS = 100,
    ID_NAV_CACHE,
    ID_NAV_DIAGNOSE,

    ID_AUTO = 200,
    ID_SOFTWARE,
    ID_HARDWARE,
    ID_ENABLED,
    ID_SAVE,

    ID_PICK = 300,
    ID_RUN,
    ID_COPY,
    ID_LIST,

    ID_MEMORY_CACHE = 350,
    ID_PREVIEW_CACHE,
    ID_PREVIEW_LIMIT,
    ID_CLEAR_PREVIEW,
    ID_LANGUAGE,

    ID_CLOSE = 400,
};

// Своё сообщение: рабочий поток закончил и положил отчёт
const UINT WM_DIAG_DONE = WM_APP + 1;
const UINT WM_DIAG_STEP = WM_APP + 2;

// ---------------------------------------------------------------- разметка
const int kNavWidth  = 190;
const int kWindowW   = 780;
const int kWindowH   = 560;
const int kMargin    = 22;
const int kBarHeight = 58;
const int kButtonW   = 116;
const int kButtonH   = 30;

// Палитра целиком, а не разрозненные цвета: тёмная тема — это тот же макет
// с другими числами, и когда числа лежат в одном месте, вторая тема стоит
// одной таблицы, а не переписывания рисования.
struct Palette {
    COLORREF window;   // содержимое
    COLORREF nav;      // полоса навигации слева
    COLORREF bar;      // полоса кнопок снизу
    COLORREF line;     // волосяные разделители
    COLORREF text;
    COLORREF note;     // пояснения и второстепенное
    COLORREF navPick;  // подложка выбранного пункта
    COLORREF accent;
    COLORREF good;
    COLORREF bad;
    COLORREF warn;

    // Кнопки и переключатели рисуем сами, поэтому цвета нужны свои
    COLORREF ctrl;        // заливка кнопки
    COLORREF ctrlHot;     // под курсором
    COLORREF ctrlDown;    // нажата
    COLORREF ctrlEdge;    // рамка
    COLORREF onAccent;    // текст поверх заливки accent
    COLORREF disabled;
};

const Palette kLight = {
    RGB(0xFF, 0xFF, 0xFF), RGB(0xF6, 0xF6, 0xF6), RGB(0xF3, 0xF3, 0xF3),
    RGB(0xE1, 0xE1, 0xE1), RGB(0x00, 0x00, 0x00), RGB(0x5B, 0x5B, 0x5B),
    RGB(0xE7, 0xEF, 0xFB), RGB(0x00, 0x67, 0xC0),
    RGB(0x10, 0x7C, 0x10), RGB(0xC4, 0x2B, 0x1C), RGB(0x9D, 0x5D, 0x00),
    RGB(0xFD, 0xFD, 0xFD), RGB(0xF5, 0xF5, 0xF5), RGB(0xF0, 0xF0, 0xF0),
    RGB(0xD0, 0xD0, 0xD0), RGB(0xFF, 0xFF, 0xFF), RGB(0xA0, 0xA0, 0xA0),
};

// Тёмная снята с Windows 11: содержимое #202020, панели чуть светлее.
// Цвета исходов сделаны бледнее светлых — насыщенный красный на тёмном
// выжигает глаз, а различать надо не яркостью, а оттенком.
const Palette kDark = {
    RGB(0x20, 0x20, 0x20), RGB(0x27, 0x27, 0x27), RGB(0x27, 0x27, 0x27),
    RGB(0x3A, 0x3A, 0x3A), RGB(0xFF, 0xFF, 0xFF), RGB(0xA6, 0xA6, 0xA6),
    RGB(0x33, 0x39, 0x42), RGB(0x4C, 0xC2, 0xFF),
    RGB(0x6C, 0xCB, 0x5F), RGB(0xFF, 0x99, 0xA4), RGB(0xFC, 0xE1, 0x00),
    RGB(0x2D, 0x2D, 0x2D), RGB(0x38, 0x38, 0x38), RGB(0x27, 0x27, 0x27),
    RGB(0x45, 0x45, 0x45), RGB(0x00, 0x1A, 0x2B), RGB(0x6E, 0x6E, 0x6E),
};

enum class Page { Settings, Cache, Diagnose };
enum class UiLanguage { English, Russian };

struct App {
    HWND  wnd = nullptr;
    UINT  dpi = 96;
    Page  page = Page::Settings;

    HFONT body = nullptr;
    HFONT head = nullptr;
    HFONT nav  = nullptr;

    // Навигация
    HWND navSettings = nullptr;
    HWND navCache    = nullptr;
    HWND navDiagnose = nullptr;
    HWND brand       = nullptr;
    HWND version     = nullptr;
    HWND languageCombo = nullptr;

    // Страница настроек
    HWND setHead = nullptr;
    HWND enabled = nullptr;
    HWND enabledNote = nullptr;
    HWND radio[3] = {};
    HWND note[3]  = {};
    HWND setHint = nullptr;
    HWND save    = nullptr;

    // Страница кэша
    HWND cacheHead = nullptr;
    HWND memoryLabel = nullptr;
    HWND memoryCombo = nullptr;
    HWND memoryNote = nullptr;
    HWND previewToggle = nullptr;
    HWND previewNote = nullptr;
    HWND previewLimitLabel = nullptr;
    HWND previewLimitCombo = nullptr;
    HWND cacheUsage = nullptr;
    HWND clearPreview = nullptr;

    // Страница диагностики
    HWND diagHead = nullptr;
    HWND diagNote = nullptr;
    HWND fileBox  = nullptr;
    HWND pick     = nullptr;
    HWND list     = nullptr;
    HWND status   = nullptr;
    HWND run      = nullptr;
    HWND copy     = nullptr;

    HWND close = nullptr;

    // Тема. Держится палитрой и тремя кистями: кисти нужны на каждый
    // WM_CTLCOLOR*, а создавать их там заново — верный способ утечь.
    // Какой переключатель отмечен. Держим сами: у рисуемой вручную кнопки
    // нет состояния «отмечена», Windows его больше не ведёт.
    int      choice = 0;          // 0 авто, 1 процессор, 2 видеокарта
    bool     enabledChoice = true;
    bool     previewChoice = false;
    UiLanguage language = UiLanguage::English;

    bool     dark = false;
    Palette  pal  = kLight;
    HBRUSH   brWindow = nullptr;
    HBRUSH   brNav    = nullptr;
    HBRUSH   brBar    = nullptr;

    std::wstring   userFile;
    aether::Report report;
    bool           haveReport = false;
    bool           running    = false;
};

App g;

const wchar_t* Text(const wchar_t* english, const wchar_t* russian)
{
    return g.language == UiLanguage::Russian ? russian : english;
}

UiLanguage SystemLanguage()
{
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0 &&
        _wcsnicmp(locale, L"ru", 2) == 0) {
        return UiLanguage::Russian;
    }
    return UiLanguage::English;
}

UiLanguage LoadLanguage()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Aether", 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return SystemLanguage();
    }

    wchar_t value[8] = {};
    DWORD type = 0, size = sizeof(value);
    const LSTATUS status = RegQueryValueExW(key, L"UiLanguage", nullptr, &type,
                                             reinterpret_cast<BYTE*>(value), &size);
    RegCloseKey(key);
    if (status == ERROR_SUCCESS && type == REG_SZ && _wcsicmp(value, L"ru") == 0) {
        return UiLanguage::Russian;
    }
    if (status == ERROR_SUCCESS && type == REG_SZ && _wcsicmp(value, L"en") == 0) {
        return UiLanguage::English;
    }
    return SystemLanguage();
}

void SaveLanguage(UiLanguage language)
{
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Aether", 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, &disposition) != ERROR_SUCCESS) {
        return;
    }
    const wchar_t* value = language == UiLanguage::Russian ? L"ru" : L"en";
    RegSetValueExW(key, L"UiLanguage", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(value),
                   static_cast<DWORD>((wcslen(value) + 1) * sizeof(*value)));
    RegCloseKey(key);
}

int Dp(int v) { return MulDiv(v, g.dpi, 96); }

UINT DpiOf(HWND w)
{
    using Fn = UINT (WINAPI*)(HWND);
    static Fn fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (fn) {
        const UINT d = fn(w);
        if (d) return d;
    }
    HDC dc = GetDC(w);
    const UINT d = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(w, dc);
    return d ? d : 96;
}

// Какая тема стоит в системе.
//
// Спрашиваем реестр, а не UISettings из WinRT: ради одного числа тащить
// WinRT в программу на чистом Win32 несоразмерно. Ключ этот Windows ведёт
// с 2016 года и меняет ровно тогда, когда человек переключает тему.
// -1 — как в системе, 0 — светлая насильно, 1 — тёмная насильно.
// Ключи --light и --dark нужны не для красоты: без них вторую тему нечем
// проверить, кроме как переключая настройку всей системы, а это уже влезание
// в чужой рабочий стол. Пригодится и в поддержке: «сними окно светлым».
int g_forceTheme = -1;

bool SystemPrefersDark()
{
    if (g_forceTheme >= 0) return g_forceTheme == 1;

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD light = 1, size = sizeof(light), type = 0;
    const LSTATUS r = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                                       (LPBYTE)&light, &size);
    RegCloseKey(key);

    if (r != ERROR_SUCCESS || type != REG_DWORD) return false;
    return light == 0;
}

// Тёмный режим для приложения целиком.
//
// Без него всё, что рисует движок тем, остаётся светлым: подпись переключателя
// выходит чёрной на чёрном, у кнопки — белая обводка. Наш SetTextColor движку
// не указ, он берёт цвет из темы сам.
//
// Включается это тремя функциями uxtheme, у которых НЕТ ИМЁН — только номера.
// Microsoft их не документировала и, судя по всему, не собирается; ими
// пользуются все настольные программы с тёмной темой, потому что другого
// способа нет. Отсюда осторожность:
//
//   * берём по номеру и только если библиотека их отдала;
//   * только на Windows 10 1809 и новее, где они появились;
//   * не нашлись — просто остаёмся при светлых элементах, а не падаем.
//
// Честная альтернатива — рисовать переключатели и кнопки самим, полностью.
// Это надёжнее, но там свой кружок, своя рамка, своё наведение мыши и свой
// фокус, то есть заметно больше кода ради того же самого вида.
enum class PreferredAppMode { Default, AllowDark, ForceDark, ForceLight, Max };

using FnSetPreferredAppMode     = PreferredAppMode (WINAPI*)(PreferredAppMode);
using FnAllowDarkModeForWindow  = BOOL             (WINAPI*)(HWND, BOOL);
using FnFlushMenuThemes         = void             (WINAPI*)();

FnSetPreferredAppMode    g_setAppMode   = nullptr;
FnAllowDarkModeForWindow g_allowForWnd  = nullptr;
FnFlushMenuThemes        g_flushMenus   = nullptr;

// Номер сборки Windows. RtlGetVersion, а не GetVersionEx: второй врёт всем,
// у кого в манифесте не перечислена нужная версия, и врёт молча.
DWORD WindowsBuild()
{
    using FnRtlGetVersion = LONG (WINAPI*)(RTL_OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return 0;

    FnRtlGetVersion fn = reinterpret_cast<FnRtlGetVersion>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
    if (!fn) return 0;

    RTL_OSVERSIONINFOW vi = { sizeof(vi) };
    if (fn(&vi) != 0) return 0;
    return vi.dwBuildNumber;
}

void LoadDarkModeApi()
{
    static bool tried = false;
    if (tried) return;
    tried = true;

    if (WindowsBuild() < 17763) return;   // раньше тёмного режима не было вовсе

    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;

    g_setAppMode = reinterpret_cast<FnSetPreferredAppMode>(
        reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))));
    g_allowForWnd = reinterpret_cast<FnAllowDarkModeForWindow>(
        reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(133))));
    g_flushMenus = reinterpret_cast<FnFlushMenuThemes>(
        reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))));
}

HFONT MakeFont(int tenthsOfPoint, int weight)
{
    return CreateFontW(-MulDiv(tenthsOfPoint, g.dpi, 720), 0, 0, 0, weight, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void MakeFonts()
{
    if (g.body) DeleteObject(g.body);
    if (g.head) DeleteObject(g.head);
    if (g.nav)  DeleteObject(g.nav);
    g.body = MakeFont(90,  FW_NORMAL);
    g.head = MakeFont(120, FW_SEMIBOLD);
    g.nav  = MakeFont(95,  FW_NORMAL);
}

HWND Add(const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0)
{
    return CreateWindowExW(ex, cls, text, WS_CHILD | WS_CLIPSIBLINGS | style, 0, 0, 0, 0,
                           g.wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
}

void ApplyFonts()
{
    struct Pair { HWND h; HFONT f; };
    const Pair pairs[] = {
        { g.brand, g.head }, { g.version, g.body },
        { g.navSettings, g.nav }, { g.navCache, g.nav }, { g.navDiagnose, g.nav },
        { g.languageCombo, g.body },
        { g.setHead, g.head }, { g.cacheHead, g.head }, { g.diagHead, g.head },
        { g.enabled, g.body }, { g.enabledNote, g.body },
        { g.radio[0], g.body }, { g.radio[1], g.body }, { g.radio[2], g.body },
        { g.note[0], g.body }, { g.note[1], g.body }, { g.note[2], g.body },
        { g.setHint, g.body }, { g.diagNote, g.body },
        { g.memoryLabel, g.body }, { g.memoryCombo, g.body }, { g.memoryNote, g.body },
        { g.previewToggle, g.body }, { g.previewNote, g.body },
        { g.previewLimitLabel, g.body }, { g.previewLimitCombo, g.body },
        { g.cacheUsage, g.body }, { g.clearPreview, g.body },
        { g.fileBox, g.body }, { g.pick, g.body }, { g.list, g.body },
        { g.status, g.body }, { g.run, g.body }, { g.copy, g.body },
        { g.save, g.body }, { g.close, g.body },
    };
    for (const Pair& p : pairs) {
        if (p.h) SendMessageW(p.h, WM_SETFONT, (WPARAM)p.f, TRUE);
    }
}

void SetComboText(HWND combo, int index, const wchar_t* text, uint32_t data)
{
    if (!combo) return;
    if (SendMessageW(combo, CB_GETCOUNT, 0, 0) <= index) return;
    const LRESULT selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    SendMessageW(combo, CB_DELETESTRING, index, 0);
    const LRESULT inserted = SendMessageW(combo, CB_INSERTSTRING, index, (LPARAM)text);
    if (inserted != CB_ERR) SendMessageW(combo, CB_SETITEMDATA, inserted, data);
    if (selected == index) SendMessageW(combo, CB_SETCURSEL, index, 0);
}

void UpdateUiText()
{
    SetWindowTextW(g.version, (std::wstring(Text(L"version ", L"версия ")) +
                               AETHER_VERSION_WSTR).c_str());
    SetWindowTextW(g.navSettings, Text(L"Settings", L"Настройки"));
    SetWindowTextW(g.navCache, Text(L"Cache", L"Кэш"));
    SetWindowTextW(g.navDiagnose, Text(L"Diagnostics", L"Диагностика"));

    SetWindowTextW(g.setHead, Text(L"How to decode AV1 and VP9 video",
                                   L"Как распаковывать видео AV1 и VP9"));
    SetWindowTextW(g.enabled, Text(L"Aether enabled", L"Aether включён"));
    SetWindowTextW(g.enabledNote,
                   Text(L"When disabled, Adobe passes files to other importers after restart,\n"
                        L"and Aether does not load FFmpeg.",
                        L"Если выключить, после перезапуска Adobe файлы будут переданы\n"
                        L"другим импортёрам, а FFmpeg не загрузится."));
    SetWindowTextW(g.radio[0], Text(L"Automatic (recommended)",
                                    L"Автоматически (рекомендуется)"));
    SetWindowTextW(g.note[0], Text(L"Use the CPU with 8 or more threads; otherwise use the GPU.",
                                   L"Процессором на машинах от 8 потоков, иначе видеокартой."));
    SetWindowTextW(g.radio[1], Text(L"CPU (dav1d)", L"Процессором (dav1d)"));
    SetWindowTextW(g.note[1],
                   Text(L"Decodes directly into system memory.",
                        L"Декодирует сразу в обычную память."));
    SetWindowTextW(g.radio[2], Text(L"GPU (NVIDIA / Intel / AMD)",
                                    L"Видеокартой (NVIDIA / Intel / AMD)"));
    SetWindowTextW(g.note[2],
                   Text(L"Usually slower: each frame must be copied back into system memory.\n"
                        L"It frees the CPU when the CPU is slow or busy.",
                        L"Обычно медленнее: каждый кадр нужно скопировать в обычную память.\n"
                        L"Разгружает процессор, если он слабый или занят другим."));
    SetWindowTextW(g.setHint,
                   Text(L"Changes take effect after restarting Premiere Pro.",
                        L"Изменения вступят в силу после перезапуска Premiere Pro."));
    SetWindowTextW(g.save, Text(L"Save", L"Сохранить"));

    SetWindowTextW(g.cacheHead, Text(L"Aether cache", L"Кэш Aether"));
    SetWindowTextW(g.memoryLabel, Text(L"Memory frame cache", L"Кэш кадров в памяти"));
    SetWindowTextW(g.memoryNote,
                   Text(L"This limit applies only to frames cached by Aether.\n"
                        L"Premiere's total memory use can be higher.",
                        L"Лимит относится только к кадрам, которые кэширует Aether.\n"
                        L"Общая память Premiere может быть выше."));
    SetWindowTextW(g.previewToggle,
                   Text(L"Store reduced previews on disk", L"Хранить уменьшенные превью на диске"));
    SetWindowTextW(g.previewNote,
                   Text(L"Only BGRA8 Draft/Low previews up to 2 MiB. Full-size\n"
                        L"playback and export never use this cache.",
                        L"Только BGRA8 Draft/Low до 2 MiB. Полноразмерное\n"
                        L"воспроизведение и экспорт этот кэш не используют."));
    SetWindowTextW(g.previewLimitLabel, Text(L"Disk cache limit", L"Лимит дискового кэша"));
    SetWindowTextW(g.clearPreview, Text(L"Clear preview cache", L"Очистить кэш превью"));

    SetWindowTextW(g.diagHead, Text(L"Diagnostics", L"Диагностика"));
    SetWindowTextW(g.diagNote,
                   Text(L"Checks your system, Adobe applications, and video decoding. If a file\n"
                        L"does not open, select it and Aether will check it separately.",
                        L"Проверяет систему, приложения Adobe и распаковку видео. Если какой-то\n"
                        L"файл не открывается — укажите его, и он будет проверен отдельно."));
    SetWindowTextW(g.pick, Text(L"Choose file...", L"Выбрать файл..."));
    SetWindowTextW(g.run, Text(L"Run check", L"Запустить проверку"));
    SetWindowTextW(g.copy, Text(L"Copy report", L"Скопировать отчёт"));
    SetWindowTextW(g.close, Text(L"Close", L"Закрыть"));

    SetComboText(g.memoryCombo, 0, Text(L"Off", L"Выключен"), 0);

    HWND header = g.list ? ListView_GetHeader(g.list) : nullptr;
    if (header) {
        HDITEMW item = {};
        item.mask = HDI_TEXT;
        item.pszText = const_cast<wchar_t*>(Text(L"Check", L"Проверка"));
        Header_SetItem(header, 1, &item);
        item.pszText = const_cast<wchar_t*>(Text(L"Result", L"Что вышло"));
        Header_SetItem(header, 2, &item);
    }
}

// Развернуть тему по всему окну.
//
// Стандартные элементы Win32 темнеть сами не умеют — ни кнопки, ни список.
// Собирается это из трёх разных мест, и другого пути нет:
//
//   1. Заголовок окна — DwmSetWindowAttribute. Единственная документированная
//      часть, и работает с Windows 10 1809 (до 20H1 номер атрибута был 19,
//      потом стал 20, поэтому пробуем оба).
//   2. Кнопки, поля, список — SetWindowTheme с именем "DarkMode_Explorer".
//      Имя темы не документировано, зато сам вызов — обычный публичный API,
//      и незнакомое имя Windows просто пропускает мимо. Худшее, что бывает
//      при неудаче, — светлый элемент на тёмном, а не отказ работать.
//   3. Всё остальное — наши кисти и WM_CTLCOLOR* ниже.
void ApplyTheme()
{
    g.dark = SystemPrefersDark();
    g.pal  = g.dark ? kDark : kLight;

    if (g.brWindow) DeleteObject(g.brWindow);
    if (g.brNav)    DeleteObject(g.brNav);
    if (g.brBar)    DeleteObject(g.brBar);
    g.brWindow = CreateSolidBrush(g.pal.window);
    g.brNav    = CreateSolidBrush(g.pal.nav);
    g.brBar    = CreateSolidBrush(g.pal.bar);

    const BOOL on = g.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(g.wnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &on, sizeof(on));
    DwmSetWindowAttribute(g.wnd, 19 /* он же до Windows 10 20H1 */,      &on, sizeof(on));

    // Сначала режим приложения, потом окно, и только потом темы элементов:
    // порядок важен, движок тем смотрит на состояние в момент обращения.
    LoadDarkModeApi();
    if (g_setAppMode)  g_setAppMode(g.dark ? PreferredAppMode::ForceDark
                                           : PreferredAppMode::ForceLight);
    if (g_allowForWnd) g_allowForWnd(g.wnd, on);
    if (g_flushMenus)  g_flushMenus();

    const wchar_t* theme = g.dark ? L"DarkMode_Explorer" : L"Explorer";
    HWND themed[] = { g.navSettings, g.navCache, g.navDiagnose,
                      g.languageCombo,
                      g.enabled, g.radio[0], g.radio[1], g.radio[2],
                      g.memoryCombo, g.previewToggle, g.previewLimitCombo,
                      g.clearPreview, g.save, g.close, g.pick, g.run, g.copy,
                      g.fileBox, g.list };
    for (HWND h : themed) {
        if (!h) continue;
        if (g_allowForWnd) g_allowForWnd(h, on);
        SetWindowTheme(h, theme, nullptr);
        // Элемент перечитывает тему только по этому сообщению; без него
        // смена темы на ходу оставила бы половину окна в прежних цветах.
        SendMessageW(h, WM_THEMECHANGED, 0, 0);
    }

    if (g.list) {
        ListView_SetBkColor(g.list, g.pal.window);
        ListView_SetTextBkColor(g.list, g.pal.window);
        ListView_SetTextColor(g.list, g.pal.text);

        // Шапку столбцов красит не список, а вложенный SysHeader32:
        // без отдельного обращения она остаётся светлой полосой сверху.
        HWND header = ListView_GetHeader(g.list);
        if (header) SetWindowTheme(header, g.dark ? L"DarkMode_ItemsView" : L"Explorer", nullptr);
    }

    InvalidateRect(g.wnd, nullptr, TRUE);
}

void Place(HWND h, int x, int y, int w, int hh)
{
    if (h) SetWindowPos(h, nullptr, Dp(x), Dp(y), Dp(w), Dp(hh),
                        SWP_NOZORDER | SWP_NOACTIVATE);
}

void Layout()
{
    const int contentX = kNavWidth + kMargin;
    const int contentW = kWindowW - contentX - kMargin;
    const int barTop   = kWindowH - kBarHeight;

    // Навигация
    Place(g.brand,       kMargin, 22, kNavWidth - kMargin * 2, 26);
    Place(g.version,     kMargin, 48, kNavWidth - kMargin * 2, 18);
    Place(g.languageCombo, 8, kWindowH - kMargin - 28, kNavWidth - 16, 150);
    Place(g.navSettings, 8, 126, kNavWidth - 16, 36);
    Place(g.navCache,    8, 166, kNavWidth - 16, 36);
    Place(g.navDiagnose, 8, 206, kNavWidth - 16, 36);

    // Страница настроек
    int y = 26;
    Place(g.setHead, contentX, y, contentW, 26); y += 40;
    Place(g.enabled, contentX, y, contentW, 22); y += 24;
    Place(g.enabledNote, contentX + 24, y, contentW - 24, 34); y += 46;
    const int noteH[3] = { 18, 18, 34 };
    for (int i = 0; i < 3; ++i) {
        Place(g.radio[i], contentX, y, contentW, 22); y += 24;
        Place(g.note[i], contentX + 24, y, contentW - 24, noteH[i]);
        y += noteH[i] + 14;
    }
    Place(g.setHint, contentX, y, contentW, 18);

    // Страница кэша
    y = 26;
    Place(g.cacheHead, contentX, y, contentW, 26); y += 44;
    Place(g.memoryLabel, contentX, y, 250, 22);
    Place(g.memoryCombo, contentX + 270, y - 3, 150, 180); y += 30;
    Place(g.memoryNote, contentX, y, contentW, 34); y += 54;
    Place(g.previewToggle, contentX, y, contentW, 22); y += 28;
    Place(g.previewNote, contentX + 24, y, contentW - 24, 34); y += 52;
    Place(g.previewLimitLabel, contentX, y, 250, 22);
    Place(g.previewLimitCombo, contentX + 270, y - 3, 150, 180); y += 42;
    Place(g.cacheUsage, contentX, y, contentW, 22);

    // Страница диагностики
    y = 26;
    Place(g.diagHead, contentX, y, contentW, 26); y += 34;
    Place(g.diagNote, contentX, y, contentW, 34); y += 44;
    Place(g.fileBox, contentX, y, contentW - 130, 26);
    Place(g.pick,    contentX + contentW - 122, y - 2, 122, 30);
    y += 40;
    Place(g.list, contentX, y, contentW, barTop - y - 34);
    Place(g.status, contentX, barTop - 26, contentW, 20);

    // Полоса кнопок
    const int by = barTop + (kBarHeight - kButtonH) / 2;
    Place(g.close, kWindowW - kMargin - kButtonW, by, kButtonW, kButtonH);
    Place(g.save,  kWindowW - kMargin - kButtonW * 2 - 8, by, kButtonW, kButtonH);
    Place(g.clearPreview, kNavWidth + kMargin, by, kButtonW + 50, kButtonH);
    Place(g.run,   kNavWidth + kMargin, by, kButtonW + 24, kButtonH);
    Place(g.copy,  kNavWidth + kMargin + kButtonW + 32, by, kButtonW + 34, kButtonH);
}

void ShowPage()
{
    const bool s = (g.page == Page::Settings);
    const bool c = (g.page == Page::Cache);
    const bool d = (g.page == Page::Diagnose);
    const int  showS = s ? SW_SHOW : SW_HIDE;
    const int  showC = c ? SW_SHOW : SW_HIDE;
    const int  showD = d ? SW_SHOW : SW_HIDE;

    HWND setPage[] = { g.setHead, g.enabled, g.enabledNote,
                       g.radio[0], g.radio[1], g.radio[2],
                       g.note[0], g.note[1], g.note[2], g.setHint, g.save };
    for (HWND h : setPage) if (h) ShowWindow(h, showS);

    HWND cachePage[] = { g.cacheHead, g.memoryLabel, g.memoryCombo, g.memoryNote,
                         g.previewToggle, g.previewNote, g.previewLimitLabel,
                         g.previewLimitCombo, g.cacheUsage, g.clearPreview };
    for (HWND h : cachePage) if (h) ShowWindow(h, showC);
    if (g.save) ShowWindow(g.save, (s || c) ? SW_SHOW : SW_HIDE);

    HWND diagPage[] = { g.diagHead, g.diagNote, g.fileBox, g.pick,
                        g.list, g.status, g.run, g.copy };
    for (HWND h : diagPage) if (h) ShowWindow(h, showD);

    if (g.previewLimitCombo) EnableWindow(g.previewLimitCombo, g.previewChoice);
    if (g.copy) EnableWindow(g.copy, g.haveReport && !g.running);
    if (g.run)  EnableWindow(g.run, !g.running);

    InvalidateRect(g.wnd, nullptr, TRUE);
}

// ------------------------------------------------------------- список строк

void ResetList()
{
    ListView_DeleteAllItems(g.list);

    // Группы убираем поимённо: ListView_RemoveAllGroups есть не везде
    for (int i = 0; i < 32; ++i) ListView_RemoveGroup(g.list, i);
}

void FillList()
{
    ResetList();
    ListView_EnableGroupView(g.list, TRUE);

    int group = 0;
    int row   = 0;

    for (const aether::Section& s : g.report.sections) {
        LVGROUP lg = { sizeof(LVGROUP) };
        lg.mask      = LVGF_HEADER | LVGF_GROUPID | LVGF_STATE;
        lg.pszHeader = const_cast<wchar_t*>(s.title.c_str());
        lg.iGroupId  = group;
        lg.state     = LVGS_NORMAL;
        lg.stateMask = LVGS_COLLAPSIBLE;
        ListView_InsertGroup(g.list, -1, &lg);

        for (const aether::Check& c : s.checks) {
            const wchar_t* mark = L"";
            switch (c.state) {
                case aether::State::Pass: mark = L"✓"; break;   // галочка
                case aether::State::Fail: mark = L"✕"; break;   // косой крест
                case aether::State::Warn: mark = L"!";      break;
                case aether::State::Skip: mark = L"—"; break;   // тире
                default:                  mark = L"";       break;
            }

            LVITEMW it = {};
            it.mask     = LVIF_TEXT | LVIF_GROUPID | LVIF_PARAM;
            it.iItem    = row;
            it.iGroupId = group;
            it.lParam   = (LPARAM)c.state;
            it.pszText  = const_cast<wchar_t*>(mark);
            ListView_InsertItem(g.list, &it);

            ListView_SetItemText(g.list, row, 1, const_cast<wchar_t*>(c.name.c_str()));
            ListView_SetItemText(g.list, row, 2, const_cast<wchar_t*>(c.detail.c_str()));
            ++row;
        }
        ++group;
    }

    // К началу: вставка строк уводит список к последней, и открывается он
    // прокрученным вниз — на самом интересном, но не на первом.
    if (row > 0) ListView_EnsureVisible(g.list, 0, FALSE);
}

// ------------------------------------------------------------------ работа

void Diagnose()
{
    if (g.running) return;
    g.running = true;
    g.haveReport = false;
    EnableWindow(g.run, FALSE);
    EnableWindow(g.copy, FALSE);
    SetWindowTextW(g.status, Text(L"Checking...", L"Проверяем..."));

    const std::wstring file = g.userFile;
    HWND target = g.wnd;

    // Отдельный поток обязателен: прогон занимает секунды, а окно, которое
    // на это время перестало отвечать, Windows красит в белое и подписывает
    // «не отвечает». Выглядит как поломка, хотя всё идёт по плану.
    std::thread([target, file] {
        aether::Report* r = new aether::Report(
            aether::Run(file, [target](const std::wstring& step) {
                PostMessageW(target, WM_DIAG_STEP, 0, (LPARAM)new std::wstring(step));
            }));
        PostMessageW(target, WM_DIAG_DONE, 0, (LPARAM)r);
    }).detach();
}

void CopyReport()
{
    if (!g.haveReport) return;

    const std::wstring text = g.report.PlainText();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);

    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return;

    void* p = GlobalLock(mem);
    memcpy(p, text.c_str(), bytes);
    GlobalUnlock(mem);

    if (OpenClipboard(g.wnd)) {
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, mem);
        CloseClipboard();
        SetWindowTextW(g.status,
                       Text(L"Report copied — paste it into a GitHub issue.",
                            L"Отчёт скопирован — вставьте его в issue на GitHub."));
    } else {
        GlobalFree(mem);
    }
}

void PickFile()
{
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner   = g.wnd;
    ofn.lpstrFilter = Text(L"Video and audio\0*.mp4;*.mkv;*.webm;*.mov;*.m4v;*.mka\0"
                            L"All files\0*.*\0\0",
                            L"Видео и звук\0*.mp4;*.mkv;*.webm;*.mov;*.m4v;*.mka\0"
                            L"Все файлы\0*.*\0\0");
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = Text(L"File that does not open", L"Файл, который не открывается");
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        g.userFile = path;
        SetWindowTextW(g.fileBox, path);
    }
}

void UpdateCacheUsage()
{
    const av1imp::PreviewCacheUsage usage = av1imp::PreviewCache::Instance().Usage();
    wchar_t text[160] = {};
    swprintf_s(text, Text(L"Used: %.1f MiB, files: %llu", L"Занято: %.1f MiB, файлов: %llu"),
               usage.bytes / (1024.0 * 1024.0),
               (unsigned long long)usage.files);
    SetWindowTextW(g.cacheUsage, text);
}

uint32_t ComboValue(HWND combo, uint32_t fallback)
{
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR) return fallback;
    const LRESULT data = SendMessageW(combo, CB_GETITEMDATA, index, 0);
    return data == CB_ERR ? fallback : (uint32_t)data;
}

void SaveSettings()
{
    av1imp::Settings settings;
    settings.enabled = g.enabledChoice;
    settings.decode = (g.choice == 1) ? av1imp::DecodeMode::Software
                    : (g.choice == 2) ? av1imp::DecodeMode::Hardware
                                      : av1imp::DecodeMode::Auto;
    settings.memoryCacheMB = ComboValue(g.memoryCombo, 512);
    settings.previewCache = g.previewChoice;
    settings.previewCacheMB = ComboValue(g.previewLimitCombo, 2048);

    if (av1imp::SaveSettings(settings)) {
        MessageBoxW(g.wnd,
                    Text(L"Saved.\n\nRestart Premiere Pro for the changes to take effect.",
                         L"Сохранено.\n\nПерезапустите Premiere Pro, чтобы настройка применилась."),
                    Text(L"Saved", L"Готово"), MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(g.wnd, Text(L"Could not write the settings file.",
                                L"Не удалось записать файл настроек."),
                    Text(L"Error", L"Ошибка"),
                    MB_OK | MB_ICONERROR);
    }
}

void ClearPreviewCache()
{
    av1imp::PreviewCache::Instance().Clear();
    UpdateCacheUsage();
    MessageBoxW(g.wnd,
                Text(L"Reduced preview cache cleared.\n\n"
                     L"A running Adobe application can create new files immediately.",
                     L"Кэш уменьшенных превью очищен.\n\n"
                     L"Работающее приложение Adobe может сразу создать новые файлы."),
                Text(L"Cache cleared", L"Кэш очищен"), MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------- рисование

void PaintChrome(HWND w)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(w, &ps);

    RECT client;
    GetClientRect(w, &client);

    RECT navRect = { 0, 0, Dp(kNavWidth), client.bottom };
    FillRect(dc, &navRect, g.brNav);

    RECT bar = { Dp(kNavWidth), Dp(kWindowH - kBarHeight), client.right, client.bottom };
    FillRect(dc, &bar, g.brBar);

    HBRUSH line = CreateSolidBrush(g.pal.line);
    RECT vline = { Dp(kNavWidth), 0, Dp(kNavWidth) + 1, client.bottom };
    FillRect(dc, &vline, line);
    RECT hline = { Dp(kNavWidth), Dp(kWindowH - kBarHeight), client.right,
                   Dp(kWindowH - kBarHeight) + 1 };
    FillRect(dc, &hline, line);

    // Своя рамка вокруг поля пути и списка. Штатная WS_EX_CLIENTEDGE рисуется
    // системными цветами, а они светлые всегда — в тёмной теме выходила белая
    // канавка вокруг обоих. Заодно уходит объёмный вид родом из Windows 2000.
    if (g.page == Page::Diagnose) {
        HWND framed[] = { g.fileBox, g.list };
        for (HWND h : framed) {
            if (!h || !IsWindowVisible(h)) continue;
            RECT r;
            GetWindowRect(h, &r);
            MapWindowPoints(nullptr, w, (POINT*)&r, 2);
            InflateRect(&r, 1, 1);
            FrameRect(dc, &r, line);
        }
    }

    DeleteObject(line);

    EndPaint(w, &ps);
}

// Пункт навигации рисуется сам: у обычной кнопки нет вида «выбранная
// строка списка», а вкладки под это окно не годятся — их было бы две штуки
// на всю ширину.
void DrawNavItem(DRAWITEMSTRUCT* di)
{
    const bool picked = (di->CtlID == ID_NAV_SETTINGS && g.page == Page::Settings) ||
                        (di->CtlID == ID_NAV_CACHE && g.page == Page::Cache) ||
                        (di->CtlID == ID_NAV_DIAGNOSE && g.page == Page::Diagnose);
    const bool hot = (di->itemState & ODS_FOCUS) != 0;

    HBRUSH b = picked ? CreateSolidBrush(g.pal.navPick) : nullptr;
    FillRect(di->hDC, &di->rcItem, b ? b : g.brNav);
    if (b) DeleteObject(b);

    if (picked) {
        // Полоска слева — то, чем Windows отмечает выбранный пункт с 2015 года
        RECT tab = di->rcItem;
        tab.left  += Dp(2);
        tab.right  = tab.left + Dp(3);
        tab.top   += Dp(8);
        tab.bottom -= Dp(8);
        HBRUSH a = CreateSolidBrush(g.pal.accent);
        FillRect(di->hDC, &tab, a);
        DeleteObject(a);
    }

    wchar_t text[64] = {};
    GetWindowTextW(di->hwndItem, text, 64);

    RECT r = di->rcItem;
    r.left += Dp(16);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, picked ? g.pal.accent : g.pal.text);
    SelectObject(di->hDC, g.nav);
    DrawTextW(di->hDC, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (hot) {
        RECT f = di->rcItem;
        InflateRect(&f, -Dp(2), -Dp(2));
        DrawFocusRect(di->hDC, &f);
    }
}

// Кнопка и переключатель рисуются вручную, и это не украшательство.
//
// Движок тем берёт цвет текста и рамки из темы, а не из нашего DC: SetTextColor
// он попросту не смотрит. В тёмной теме это давало чёрную подпись на чёрном
// и белую обводку у кнопок. Недокументированный тёмный режим uxtheme тут не
// помог — проверено замером яркости, подпись оставалась 32 из 255.
//
// Своя отрисовка чуть дороже в коде и полностью предсказуема: цвета берутся
// из палитры, значит обе темы устроены одинаково и ломаться порознь им негде.

void FillRounded(HDC dc, const RECT& r, COLORREF fill, COLORREF edge, int radius)
{
    HBRUSH b = CreateSolidBrush(fill);
    HPEN   pn = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ ob = SelectObject(dc, b);
    HGDIOBJ op = SelectObject(dc, pn);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(pn);
}

void DrawPushButton(DRAWITEMSTRUCT* di)
{
    const bool disabled = (di->itemState & ODS_DISABLED) != 0;
    const bool pressed  = (di->itemState & ODS_SELECTED) != 0;
    const bool hot      = (di->itemState & ODS_HOTLIGHT) != 0;

    // Главная кнопка страницы залита цветом выделения — так видно, чего от
    // человека ждут, и не приходится держать в голове, какая тут по умолчанию.
    const bool primary = (di->CtlID == ID_SAVE || di->CtlID == ID_RUN);

    COLORREF fill = primary ? g.pal.accent : g.pal.ctrl;
    if (pressed)      fill = primary ? g.pal.accent : g.pal.ctrlDown;
    else if (hot)     fill = primary ? g.pal.accent : g.pal.ctrlHot;
    if (disabled)     fill = g.pal.ctrlDown;

    COLORREF text = primary ? g.pal.onAccent : g.pal.text;
    if (disabled) text = g.pal.disabled;

    FillRect(di->hDC, &di->rcItem, g.brBar);          // подложка полосы кнопок
    FillRounded(di->hDC, di->rcItem, fill, disabled ? g.pal.ctrlEdge : g.pal.ctrlEdge, Dp(8));

    wchar_t caption[64] = {};
    GetWindowTextW(di->hwndItem, caption, 64);

    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, text);
    SelectObject(di->hDC, g.body);
    DrawTextW(di->hDC, caption, -1, &di->rcItem,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if ((di->itemState & ODS_FOCUS) && !disabled) {
        RECT f = di->rcItem;
        InflateRect(&f, -Dp(3), -Dp(3));
        DrawFocusRect(di->hDC, &f);
    }
}

void DrawRadio(DRAWITEMSTRUCT* di)
{
    const int index = di->CtlID - ID_AUTO;
    const bool on   = (g.choice == index);
    const bool hot  = (di->itemState & ODS_HOTLIGHT) != 0;

    FillRect(di->hDC, &di->rcItem, g.brWindow);

    const int size = Dp(16);
    const int top  = di->rcItem.top + ((di->rcItem.bottom - di->rcItem.top) - size) / 2;
    RECT ring = { di->rcItem.left, top, di->rcItem.left + size, top + size };

    // Кружок: снаружи обод, внутри точка. Отмеченный обводится цветом
    // выделения — иначе в тёмной теме отличить его от пустого почти нельзя.
    FillRounded(di->hDC, ring,
                on ? g.pal.accent : (hot ? g.pal.ctrlHot : g.pal.ctrl),
                on ? g.pal.accent : g.pal.ctrlEdge,
                size);

    if (on) {
        RECT dot = ring;
        InflateRect(&dot, -Dp(5), -Dp(5));
        FillRounded(di->hDC, dot, g.pal.onAccent, g.pal.onAccent, Dp(6));
    }

    wchar_t caption[128] = {};
    GetWindowTextW(di->hwndItem, caption, 128);

    RECT text = di->rcItem;
    text.left = ring.right + Dp(10);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, g.pal.text);
    SelectObject(di->hDC, g.body);
    DrawTextW(di->hDC, caption, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (di->itemState & ODS_FOCUS) {
        RECT f = di->rcItem;
        InflateRect(&f, -Dp(1), -Dp(1));
        DrawFocusRect(di->hDC, &f);
    }
}

void DrawCheck(DRAWITEMSTRUCT* di)
{
    const bool on = di->CtlID == ID_ENABLED ? g.enabledChoice : g.previewChoice;
    const bool hot = (di->itemState & ODS_HOTLIGHT) != 0;
    FillRect(di->hDC, &di->rcItem, g.brWindow);

    const int size = Dp(16);
    const int top = di->rcItem.top + ((di->rcItem.bottom - di->rcItem.top) - size) / 2;
    RECT box = { di->rcItem.left, top, di->rcItem.left + size, top + size };
    FillRounded(di->hDC, box, on ? g.pal.accent : (hot ? g.pal.ctrlHot : g.pal.ctrl),
                on ? g.pal.accent : g.pal.ctrlEdge, Dp(3));
    if (on) {
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, g.pal.onAccent);
        SelectObject(di->hDC, g.body);
        DrawTextW(di->hDC, L"✓", -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    wchar_t caption[160] = {};
    GetWindowTextW(di->hwndItem, caption, 160);
    RECT text = di->rcItem;
    text.left = box.right + Dp(10);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, g.pal.text);
    SelectObject(di->hDC, g.body);
    DrawTextW(di->hDC, caption, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (di->itemState & ODS_FOCUS) {
        RECT f = di->rcItem;
        InflateRect(&f, -Dp(1), -Dp(1));
        DrawFocusRect(di->hDC, &f);
    }
}

// Шапка столбцов рисуется вручную, и добраться до неё пришлось в обход.
//
// «Проверка» и «Что вышло» рисует не список, а вложенный SysHeader32, и своих
// извещений он шлёт не нам, а списку. До нашего окна они не доходят вовсе —
// оттого шапка и осталась чёрной по тёмному, когда всё вокруг уже побелело.
//
// Отсюда подмена оконной процедуры списка: перехватываем WM_NOTIFY от шапки
// по дороге. И рисуем целиком, а не только ставим цвет: движок тем берёт
// цвет из темы, а не из DC, — на кнопках это уже проверено дорогой ценой.
LRESULT CALLBACK ListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                          UINT_PTR id, DWORD_PTR)
{
    if (msg == WM_NOTIFY) {
        NMHDR* n = (NMHDR*)lp;
        if (n->code == NM_CUSTOMDRAW && n->hwndFrom == ListView_GetHeader(h)) {
            NMCUSTOMDRAW* cd = (NMCUSTOMDRAW*)lp;

            if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;

            if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                RECT r = cd->rc;
                FillRect(cd->hdc, &r, g.brWindow);

                // Волосяная линия под шапкой и справа от столбца: без них
                // шапка сливается со строками, и таблица теряет столбцы.
                HBRUSH line = CreateSolidBrush(g.pal.line);
                RECT bottom = { r.left, r.bottom - 1, r.right, r.bottom };
                FillRect(cd->hdc, &bottom, line);
                RECT edge = { r.right - 1, r.top + Dp(6), r.right, r.bottom - Dp(6) };
                FillRect(cd->hdc, &edge, line);
                DeleteObject(line);

                wchar_t caption[64] = {};
                HDITEMW hi = {};
                hi.mask       = HDI_TEXT;
                hi.pszText    = caption;
                hi.cchTextMax = 64;
                Header_GetItem(n->hwndFrom, (int)cd->dwItemSpec, &hi);

                r.left  += Dp(8);
                r.right -= Dp(8);
                SetBkMode(cd->hdc, TRANSPARENT);
                SetTextColor(cd->hdc, g.pal.text);
                SelectObject(cd->hdc, g.body);
                DrawTextW(cd->hdc, caption, -1, &r,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                return CDRF_SKIPDEFAULT;
            }
        }
    }

    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, ListProc, id);
    return DefSubclassProc(h, msg, wp, lp);
}

// Цвет ставится только столбцу со значком: раскрашивать всю строку — значит
// сделать отчёт пёстрым и нечитаемым, а показать надо ровно исход проверки.
LRESULT ListCustomDraw(NMLVCUSTOMDRAW* cd)
{
    switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:          return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:      return CDRF_NOTIFYSUBITEMDRAW;
        case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
            if (cd->iSubItem == 0) {
                switch ((aether::State)cd->nmcd.lItemlParam) {
                    case aether::State::Pass: cd->clrText = g.pal.good; break;
                    case aether::State::Fail: cd->clrText = g.pal.bad;  break;
                    case aether::State::Warn: cd->clrText = g.pal.warn; break;
                    default:                  cd->clrText = g.pal.note; break;
                }
            } else if (cd->iSubItem == 2) {
                cd->clrText = g.pal.note;
            } else {
                // Цвет надо ставить КАЖДОМУ столбцу, а не только цветным.
                // Оставленный без присмотра, он достаётся в наследство от
                // предыдущего: названия проверок позеленели от галочек слева.
                cd->clrText = g.pal.text;
            }
            return CDRF_NEWFONT;
    }
    return CDRF_DODEFAULT;
}

void Build(HWND w)
{
    g.wnd = w;
    g.dpi = DpiOf(w);
    g.language = LoadLanguage();
    MakeFonts();

    g.brand   = Add(L"STATIC", L"Aether", WS_VISIBLE, 0);
    g.version = Add(L"STATIC", L"", WS_VISIBLE, 0);
    g.languageCombo = Add(L"COMBOBOX", L"", WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
                          ID_LANGUAGE);
    const LRESULT english = SendMessageW(g.languageCombo, CB_ADDSTRING, 0, (LPARAM)L"English");
    SendMessageW(g.languageCombo, CB_SETITEMDATA, english, (LPARAM)UiLanguage::English);
    const LRESULT russian = SendMessageW(g.languageCombo, CB_ADDSTRING, 0, (LPARAM)L"Русский");
    SendMessageW(g.languageCombo, CB_SETITEMDATA, russian, (LPARAM)UiLanguage::Russian);
    SendMessageW(g.languageCombo, CB_SETCURSEL,
                 g.language == UiLanguage::Russian ? russian : english, 0);

    g.navSettings = Add(L"BUTTON", L"Настройки",
                        WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, ID_NAV_SETTINGS);
    g.navCache = Add(L"BUTTON", L"Кэш",
                     WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, ID_NAV_CACHE);
    g.navDiagnose = Add(L"BUTTON", L"Диагностика",
                        WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, ID_NAV_DIAGNOSE);

    // ---- настройки ----
    g.setHead  = Add(L"STATIC", L"Как распаковывать видео AV1 и VP9", 0, 0);
    g.enabled = Add(L"BUTTON", L"Aether включён",
                    BS_OWNERDRAW | WS_GROUP | WS_TABSTOP, ID_ENABLED);
    g.enabledNote = Add(L"STATIC",
                        L"Если выключить, после перезапуска Adobe файлы будут переданы\n"
                        L"другим импортёрам, а FFmpeg не загрузится.", 0, 0);
    g.radio[0] = Add(L"BUTTON", L"Автоматически (рекомендуется)",
                     BS_OWNERDRAW | WS_GROUP | WS_TABSTOP, ID_AUTO);
    g.note[0]  = Add(L"STATIC", L"Процессором на машинах от 8 потоков, иначе видеокартой.", 0, 0);
    g.radio[1] = Add(L"BUTTON", L"Процессором (dav1d)", BS_OWNERDRAW | WS_TABSTOP, ID_SOFTWARE);
    g.note[1]  = Add(L"STATIC",
                     L"По замерам быстрее: видеокарте приходится копировать каждый кадр\n"
                     L"в обычную память, а процессор сразу пишет туда.", 0, 0);
    g.radio[2] = Add(L"BUTTON", L"Видеокартой (NVIDIA / Intel / AMD)", BS_OWNERDRAW | WS_TABSTOP, ID_HARDWARE);
    g.note[2]  = Add(L"STATIC", L"Разгружает процессор. Выбирайте, если он слабый или занят другим.", 0, 0);
    g.setHint  = Add(L"STATIC", L"Изменения вступят в силу после перезапуска Premiere Pro.",
                     SS_LEFTNOWORDWRAP, 0);
    g.save     = Add(L"BUTTON", L"Сохранить", BS_OWNERDRAW | WS_TABSTOP, ID_SAVE);

    // ---- кэш ----
    g.cacheHead = Add(L"STATIC", L"Кэш Aether", 0, 0);
    g.memoryLabel = Add(L"STATIC", L"Кэш кадров в памяти", SS_LEFTNOWORDWRAP, 0);
    g.memoryCombo = Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                        ID_MEMORY_CACHE);
    g.memoryNote = Add(L"STATIC",
                       L"Лимит относится только к кадрам, которые кэширует Aether.\n"
                       L"Общая память Premiere может быть выше.", 0, 0);
    g.previewToggle = Add(L"BUTTON", L"Хранить уменьшенные превью на диске",
                          BS_OWNERDRAW | WS_GROUP | WS_TABSTOP, ID_PREVIEW_CACHE);
    g.previewNote = Add(L"STATIC",
                        L"Только BGRA8 Draft/Low до 2 MiB. Полноразмерное\n"
                        L"воспроизведение и экспорт этот кэш не используют.", 0, 0);
    g.previewLimitLabel = Add(L"STATIC", L"Лимит дискового кэша", SS_LEFTNOWORDWRAP, 0);
    g.previewLimitCombo = Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                              ID_PREVIEW_LIMIT);
    g.cacheUsage = Add(L"STATIC", L"", SS_LEFTNOWORDWRAP, 0);
    g.clearPreview = Add(L"BUTTON", L"Очистить кэш превью",
                         BS_OWNERDRAW | WS_TABSTOP, ID_CLEAR_PREVIEW);

    // ---- диагностика ----
    g.diagHead = Add(L"STATIC", L"Диагностика", 0, 0);
    g.diagNote = Add(L"STATIC",
                     L"Проверяет систему, приложения Adobe и распаковку видео. Если какой-то\n"
                     L"файл не открывается — укажите его, и он будет проверен отдельно.", 0, 0);
    g.fileBox  = Add(L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY, 0);
    g.pick     = Add(L"BUTTON", L"Выбрать файл...", BS_OWNERDRAW | WS_TABSTOP, ID_PICK);
    g.list     = Add(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                     ID_LIST);
    g.status   = Add(L"STATIC", L"", SS_LEFTNOWORDWRAP, 0);
    g.run      = Add(L"BUTTON", L"Запустить проверку", BS_OWNERDRAW | WS_TABSTOP, ID_RUN);
    g.copy     = Add(L"BUTTON", L"Скопировать отчёт", BS_OWNERDRAW | WS_TABSTOP, ID_COPY);

    g.close = Add(L"BUTTON", L"Закрыть", WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, ID_CLOSE);

    ListView_SetExtendedListViewStyle(g.list,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    SetWindowSubclass(g.list, ListProc, 1, 0);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    struct C { const wchar_t* title; int width; };
    const C cols[] = { { L"", 28 }, { L"Проверка", 210 }, { L"Что вышло", 300 } };
    for (int i = 0; i < 3; ++i) {
        col.pszText = const_cast<wchar_t*>(cols[i].title);
        col.cx      = Dp(cols[i].width);
        ListView_InsertColumn(g.list, i, &col);
    }

    ApplyFonts();
    UpdateUiText();
    Layout();

    const av1imp::Settings settings = av1imp::CurrentSettings();
    switch (settings.decode) {
        case av1imp::DecodeMode::Software: g.choice = 1; break;
        case av1imp::DecodeMode::Hardware: g.choice = 2; break;
        default:                           g.choice = 0; break;
    }
    g.enabledChoice = settings.enabled;
    g.previewChoice = settings.previewCache;

    const uint32_t memoryValues[] = { 0, 128, 256, 512, 1024, 2048, 4096 };
    for (uint32_t value : memoryValues) {
        wchar_t label[64] = {};
        if (value == 0) wcscpy_s(label, Text(L"Off", L"Выключен"));
        else swprintf_s(label, L"%u MiB", value);
        const LRESULT at = SendMessageW(g.memoryCombo, CB_ADDSTRING, 0, (LPARAM)label);
        SendMessageW(g.memoryCombo, CB_SETITEMDATA, at, value);
        if (value == settings.memoryCacheMB) SendMessageW(g.memoryCombo, CB_SETCURSEL, at, 0);
    }

    const uint32_t diskValues[] = { 256, 512, 1024, 2048, 4096, 8192, 20480 };
    for (uint32_t value : diskValues) {
        wchar_t label[64] = {};
        if (value >= 1024 && value % 1024 == 0) swprintf_s(label, L"%u GiB", value / 1024);
        else swprintf_s(label, L"%u MiB", value);
        const LRESULT at = SendMessageW(g.previewLimitCombo, CB_ADDSTRING, 0, (LPARAM)label);
        SendMessageW(g.previewLimitCombo, CB_SETITEMDATA, at, value);
        if (value == settings.previewCacheMB) SendMessageW(g.previewLimitCombo, CB_SETCURSEL, at, 0);
    }
    if (SendMessageW(g.memoryCombo, CB_GETCURSEL, 0, 0) == CB_ERR)
        SendMessageW(g.memoryCombo, CB_SETCURSEL, 3, 0);
    if (SendMessageW(g.previewLimitCombo, CB_GETCURSEL, 0, 0) == CB_ERR)
        SendMessageW(g.previewLimitCombo, CB_SETCURSEL, 3, 0);
    UpdateCacheUsage();

    ApplyTheme();
    DragAcceptFiles(w, TRUE);
    ShowPage();
}

LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_CREATE:
            Build(w);
            return 0;

        case WM_ERASEBKGND: {
            RECT r;
            GetClientRect(w, &r);
            FillRect((HDC)wp, &r, g.brWindow ? g.brWindow : GetSysColorBrush(COLOR_WINDOW));
            return 1;
        }

        case WM_PAINT:
            PaintChrome(w);
            return 0;

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
            switch (di->CtlID) {
                case ID_NAV_SETTINGS:
                case ID_NAV_CACHE:
                case ID_NAV_DIAGNOSE:
                    DrawNavItem(di);
                    return TRUE;
                case ID_ENABLED:
                case ID_PREVIEW_CACHE:
                    DrawCheck(di);
                    return TRUE;
                case ID_AUTO:
                case ID_SOFTWARE:
                case ID_HARDWARE:
                    DrawRadio(di);
                    return TRUE;
                case ID_SAVE:
                case ID_PICK:
                case ID_RUN:
                case ID_COPY:
                case ID_CLEAR_PREVIEW:
                case ID_CLOSE:
                    DrawPushButton(di);
                    return TRUE;
            }
            return FALSE;
        }

        case WM_NOTIFY: {
            NMHDR* n = (NMHDR*)lp;
            if (n->idFrom == ID_LIST && n->code == NM_CUSTOMDRAW) {
                return ListCustomDraw((NMLVCUSTOMDRAW*)lp);
            }
            return 0;
        }

        // Подписи и переключатели рисуют фон сами и берут его у нас. Без
        // этого вокруг каждого остаётся прямоугольник чужого цвета — в тёмной
        // теме белый, что видно за версту.
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            const HWND ctl = (HWND)lp;
            SetBkMode(dc, TRANSPARENT);

            const bool inNav = (ctl == g.brand || ctl == g.version);
            const bool note  = (ctl == g.note[0] || ctl == g.note[1] || ctl == g.note[2] ||
                                ctl == g.enabledNote || ctl == g.setHint ||
                                ctl == g.memoryNote || ctl == g.previewNote ||
                                ctl == g.cacheUsage ||
                                ctl == g.diagNote || ctl == g.status ||
                                ctl == g.version);
            SetTextColor(dc, note ? g.pal.note : g.pal.text);
            return (LRESULT)(inNav ? g.brNav : g.brWindow);
        }

        // Поле с путём к файлу: у него свой фон, и системный он берёт даже
        // с тёмной темой элемента.
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wp;
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, g.pal.window);
            SetTextColor(dc, g.pal.text);
            return (LRESULT)g.brWindow;
        }

        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            wchar_t path[MAX_PATH] = {};
            if (DragQueryFileW(drop, 0, path, MAX_PATH)) {
                g.userFile = path;
                SetWindowTextW(g.fileBox, path);
                g.page = Page::Diagnose;
                ShowPage();
            }
            DragFinish(drop);
            return 0;
        }

        case WM_DIAG_STEP: {
            std::wstring* step = (std::wstring*)lp;
            SetWindowTextW(g.status, (std::wstring(Text(L"Checking: ", L"Проверяем: ")) +
                                      *step).c_str());
            delete step;
            return 0;
        }

        case WM_DIAG_DONE: {
            aether::Report* r = (aether::Report*)lp;
            g.report = *r;
            delete r;
            g.haveReport = true;
            g.running    = false;
            FillList();
            EnableWindow(g.run, TRUE);
            EnableWindow(g.copy, TRUE);
            SetWindowTextW(g.status, g.report.AnyFailed()
                ? Text(L"Some checks failed — see the red marks. Copy the report and attach it to an issue.",
                       L"Есть сбои — отметки красным. Скопируйте отчёт и приложите к issue.")
                : Text(L"Everything looks good. Premiere still opens files through its own path.",
                       L"Всё в порядке. Это не обещает, что Premiere откроет файл, — он ходит своим путём."));
            return 0;
        }

        // Человек переключил тему, пока окно открыто. Windows рассылает это
        // всем окнам; нас касается только "ImmersiveColorSet".
        case WM_SETTINGCHANGE:
            if (lp && wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0) {
                ApplyTheme();
            }
            return 0;

        case WM_DPICHANGED: {
            g.dpi = HIWORD(wp);
            MakeFonts();
            ApplyFonts();
            Layout();
            const RECT* want = (const RECT*)lp;
            SetWindowPos(w, nullptr, want->left, want->top,
                         want->right - want->left, want->bottom - want->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(w, nullptr, TRUE);
            return 0;
        }

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case ID_LANGUAGE:
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        const LRESULT index = SendMessageW(g.languageCombo, CB_GETCURSEL, 0, 0);
                        const LRESULT value = SendMessageW(g.languageCombo, CB_GETITEMDATA, index, 0);
                        if (value != CB_ERR) {
                            g.language = static_cast<UiLanguage>(value);
                            SaveLanguage(g.language);
                            UpdateUiText();
                            UpdateCacheUsage();
                            ApplyTheme();
                        }
                    }
                    return 0;

                // Рисуем сами — значит и переключаем сами: BS_OWNERDRAW
                // состояния «отмечен» не ведёт вовсе.
                case ID_AUTO:
                case ID_SOFTWARE:
                case ID_HARDWARE:
                    g.choice = LOWORD(wp) - ID_AUTO;
                    for (HWND h : g.radio) if (h) InvalidateRect(h, nullptr, TRUE);
                    return 0;

                case ID_ENABLED:
                    g.enabledChoice = !g.enabledChoice;
                    InvalidateRect(g.enabled, nullptr, TRUE);
                    return 0;

                case ID_PREVIEW_CACHE:
                    g.previewChoice = !g.previewChoice;
                    InvalidateRect(g.previewToggle, nullptr, TRUE);
                    EnableWindow(g.previewLimitCombo, g.previewChoice);
                    return 0;

                case ID_NAV_SETTINGS: g.page = Page::Settings; ShowPage(); return 0;
                case ID_NAV_CACHE:
                    g.page = Page::Cache;
                    UpdateCacheUsage();
                    ShowPage();
                    return 0;
                case ID_NAV_DIAGNOSE: g.page = Page::Diagnose; ShowPage(); return 0;
                case ID_SAVE:  SaveSettings(); return 0;
                case ID_CLEAR_PREVIEW: ClearPreviewCache(); return 0;
                case ID_PICK:  PickFile();     return 0;
                case ID_RUN:   Diagnose();     return 0;
                case ID_COPY:  CopyReport();   return 0;
                case ID_CLOSE: DestroyWindow(w); return 0;
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(w);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmdLine, int show)
{
    // Ключи разбираем ДО создания окна: тему применяет WM_CREATE, а он
    // случается внутри CreateWindowExW. Разбор после него опоздал бы.
    if (cmdLine && wcsstr(cmdLine, L"--light")) g_forceTheme = 0;
    if (cmdLine && wcsstr(cmdLine, L"--dark"))  g_forceTheme = 1;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    HICON iconBig   = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                        GetSystemMetrics(SM_CXICON),
                                        GetSystemMetrics(SM_CYICON), 0);
    HICON iconSmall = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                        GetSystemMetrics(SM_CXSMICON),
                                        GetSystemMetrics(SM_CYSMICON), 0);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = Proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    // Фон закрашиваем сами: системная кисть COLOR_WINDOW всегда светлая,
    // и в тёмной теме окно моргало бы белым при каждом изменении размера.
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"AetherApp";
    wc.hIcon         = iconBig;
    wc.hIconSm       = iconSmall;
    RegisterClassExW(&wc);

    // WS_CLIPCHILDREN обязателен, и это не вкусовщина.
    //
    // Без него закраска в WM_PAINT идёт по всему окну, включая места, где
    // сидят дочерние элементы, — и затирает их. Кто успел отрисоваться позже,
    // тот и виден: кнопка пропадала в одном прогоне из трёх, а список на
    // странице диагностики выходил пустым при полном наборе строк внутри.
    // Ловится это только повторным снимком, потому что зависит от порядка.
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                        WS_CLIPCHILDREN;

    HWND w = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"Aether", style,
                             CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
                             nullptr, nullptr, inst, nullptr);
    if (!w) return 1;

    RECT r = { 0, 0, Dp(kWindowW), Dp(kWindowH) };
    AdjustWindowRectEx(&r, style, FALSE, WS_EX_ACCEPTFILES);
    const int ww = r.right - r.left, wh = r.bottom - r.top;

    RECT work = {};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        SetWindowPos(w, nullptr,
                     work.left + ((work.right - work.left) - ww) / 2,
                     work.top  + ((work.bottom - work.top) - wh) / 2,
                     ww, wh, SWP_NOZORDER);
    } else {
        SetWindowPos(w, nullptr, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER);
    }

    // Установщик зовёт нас с последней страницы: человек нажал «проверить»,
    // и попасть он должен сразу на проверку, а не на настройки.
    if (cmdLine && wcsstr(cmdLine, L"--diagnose")) {
        g.page = Page::Diagnose;
        ShowPage();
    }

    ShowWindow(w, show);
    UpdateWindow(w);

    if (g.page == Page::Diagnose) Diagnose();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(w, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
