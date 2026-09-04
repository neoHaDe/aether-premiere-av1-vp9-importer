// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 neoHaDe

#include "app_diagnose.h"
#include "localization.h"

#include <iomanip>
#include <sstream>

#include "../src/AV1Decoder.h"
#include "../src/AV1Settings.h"
#include "../src/PreviewCache.h"
#include "../src/AV1Version.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>
}

namespace aether {

namespace {

std::wstring Widen(const std::string& s)
{
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string Narrow(const std::wstring& s)
{
    if (s.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                      nullptr, 0, nullptr, nullptr);
    std::string a(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &a[0], n, nullptr, nullptr);
    return a;
}

std::wstring Format(const wchar_t* fmt, ...)
{
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);
    return buf;
}

void Add(Section& s, const wchar_t* name, State st, const std::wstring& detail)
{
    Check c;
    c.name   = name;
    c.state  = st;
    c.detail = detail;
    s.checks.push_back(c);
}

// --------------------------------------------------------------------------
// Реестр: короткие помощники, потому что дальше их нужно много
// --------------------------------------------------------------------------

std::wstring RegString(HKEY root, const wchar_t* path, const wchar_t* value)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, path, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return std::wstring();
    }

    wchar_t buf[512] = {};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    const LSTATUS r = RegQueryValueExW(key, value, nullptr, &type, (LPBYTE)buf, &size);
    RegCloseKey(key);

    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return std::wstring();
    return buf;
}

DWORD RegDword(HKEY root, const wchar_t* path, const wchar_t* value)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, path, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return 0;

    DWORD data = 0, size = sizeof(data), type = 0;
    const LSTATUS r = RegQueryValueExW(key, value, nullptr, &type, (LPBYTE)&data, &size);
    RegCloseKey(key);
    return (r == ERROR_SUCCESS && type == REG_DWORD) ? data : 0;
}

// --------------------------------------------------------------------------
// Система
// --------------------------------------------------------------------------

std::wstring WindowsName()
{
    const wchar_t* cv = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

    std::wstring name  = RegString(HKEY_LOCAL_MACHINE, cv, L"ProductName");
    std::wstring build = RegString(HKEY_LOCAL_MACHINE, cv, L"CurrentBuildNumber");
    std::wstring disp  = RegString(HKEY_LOCAL_MACHINE, cv, L"DisplayVersion");

    // ProductName до сих пор пишет «Windows 10» на Windows 11: Microsoft его
    // не стала менять, чтобы не сломать чужие проверки. Отличаем по сборке.
    const int buildNum = build.empty() ? 0 : _wtoi(build.c_str());
    if (buildNum >= 22000 && name.find(L"Windows 10") != std::wstring::npos) {
        name.replace(name.find(L"Windows 10"), 10, L"Windows 11");
    }

    std::wstring out = name.empty() ? L"Windows" : name;
    if (!disp.empty())  out += L" " + disp;
    if (!build.empty()) out += std::wstring(Tr(L" (build ", L" (сборка ")) + build + L")";
    return out;
}

std::wstring CpuName()
{
    std::wstring name = RegString(
        HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"ProcessorNameString");

    // Строка из реестра приходит с пробелами по краям чаще, чем без них
    while (!name.empty() && name.back()  == L' ') name.pop_back();
    while (!name.empty() && name.front() == L' ') name.erase(name.begin());

    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    if (name.empty()) name = Tr(L"unknown", L"неизвестен");

    // Поток, а не Format(Tr(...)): строка формата не переводится никогда,
    // см. правило в tools/localization.h
    std::wostringstream out;
    out << name << Tr(L", logical processors: ", L", логических процессоров: ")
        << si.dwNumberOfProcessors;
    return out.str();
}

std::wstring MemorySize()
{
    MEMORYSTATUSEX m = { sizeof(m) };
    if (!GlobalMemoryStatusEx(&m)) return Tr(L"unknown", L"неизвестно");

    const double total = (double)m.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    const double avail = (double)m.ullAvailPhys / (1024.0 * 1024.0 * 1024.0);

    std::wostringstream out;
    out << std::fixed << std::setprecision(1)
        << total << Tr(L" GB, ", L" ГБ, ")
        << Tr(L"free ", L"свободно ") << avail << Tr(L" GB", L" ГБ");
    return out.str();
}

// Видеокарта берётся через EnumDisplayDevices, а не через WMI.
//
// WMI ради одной строчки тянет за собой COM, инициализацию, права и секунды
// ожидания — а имя и версия драйвера лежат в реестре, куда EnumDisplayDevices
// даёт прямой путь ключом DeviceKey.
std::wstring GpuName()
{
    std::wstring out;

    for (DWORD i = 0; ; ++i) {
        DISPLAY_DEVICEW dev = { sizeof(dev) };
        if (!EnumDisplayDevicesW(nullptr, i, &dev, EDD_GET_DEVICE_INTERFACE_NAME)) break;
        if (!(dev.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)) continue;

        out = dev.DeviceString;

        // DeviceKey выглядит как \Registry\Machine\System\CurrentControlSet\...
        // Приставку убираем, остальное — путь под HKLM.
        std::wstring key = dev.DeviceKey;
        const wchar_t* prefix = L"\\Registry\\Machine\\";
        if (key.rfind(prefix, 0) == 0) {
            key = key.substr(wcslen(prefix));
            const std::wstring ver = RegString(HKEY_LOCAL_MACHINE, key.c_str(), L"DriverVersion");
            if (!ver.empty()) out += std::wstring(Tr(L", driver ", L", драйвер ")) + ver;
        }
        break;
    }

    return out.empty() ? std::wstring(Tr(L"not detected", L"не определилась")) : out;
}

// --------------------------------------------------------------------------
// Adobe
// --------------------------------------------------------------------------

// Общая папка плагинов. Её адрес приложения Adobe сами пишут в реестр —
// именно оттуда её берёт и установщик, так что расхождения быть не может.
std::wstring MediaCore()
{
    const wchar_t* keys[] = {
        L"SOFTWARE\\Adobe\\Premiere Pro\\CurrentVersion",
        L"SOFTWARE\\Adobe\\After Effects\\CurrentVersion",
        L"SOFTWARE\\Adobe\\Adobe Media Encoder\\CurrentVersion",
    };
    for (const wchar_t* k : keys) {
        std::wstring p = RegString(HKEY_LOCAL_MACHINE, k, L"CommonPluginInstallPath");
        if (!p.empty()) return p;
    }
    return L"C:\\Program Files\\Adobe\\Common\\Plug-ins\\7.0\\MediaCore";
}

struct AdobeApp {
    const wchar_t* name;
    const wchar_t* exe;      // относительно папки версии
};

void CheckAdobe(Section& s)
{
    // Приложения перечисляем по папкам установки, а не по одному ключу:
    // Adobe кладёт версии рядом, и по реестру видно только текущую.
    // Путь до exe у каждого свой, и у After Effects он на папку глубже:
    // в корне лежат только ярлыки. На этом первая версия проверки и
    // споткнулась — рапортовала «не найдено» при установленном AE.
    const AdobeApp apps[] = {
        { L"Premiere Pro",   L"Adobe Premiere Pro.exe" },
        { L"After Effects",  L"Support Files\\AfterFX.exe" },
        { L"Media Encoder",  L"Adobe Media Encoder.exe" },
    };

    const std::wstring roots[] = {
        L"C:\\Program Files\\Adobe\\",
        RegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Adobe\\Premiere Pro\\CurrentVersion",
                  L"InstallPath"),
    };

    int found = 0;
    for (const AdobeApp& app : apps) {
        std::wstring versions;

        WIN32_FIND_DATAW fd = {};
        const std::wstring pattern = roots[0] + L"*";
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (fd.cFileName[0] == L'.') continue;
                if (wcsstr(fd.cFileName, app.name) == nullptr) continue;

                const std::wstring exe = roots[0] + fd.cFileName + L"\\" + app.exe;
                if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) continue;

                DWORD dummy = 0;
                const DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &dummy);
                std::wstring ver;
                if (size) {
                    std::vector<BYTE> data(size);
                    if (GetFileVersionInfoW(exe.c_str(), 0, size, data.data())) {
                        VS_FIXEDFILEINFO* fi = nullptr;
                        UINT len = 0;
                        if (VerQueryValueW(data.data(), L"\\", (LPVOID*)&fi, &len) && fi) {
                            ver = Format(L"%u.%u.%u",
                                         HIWORD(fi->dwFileVersionMS),
                                         LOWORD(fi->dwFileVersionMS),
                                         HIWORD(fi->dwFileVersionLS));
                        }
                    }
                }
                if (ver.empty()) ver = fd.cFileName;
                if (!versions.empty()) versions += L", ";
                versions += ver;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }

        if (versions.empty()) {
            Add(s, app.name, State::Skip, Tr(L"not found", L"не найдено"));
        } else {
            Add(s, app.name, State::Pass, versions);
            ++found;
        }
    }

    const std::wstring core = MediaCore();
    Add(s, Tr(L"Shared plug-ins folder", L"Общая папка плагинов"),
        GetFileAttributesW(core.c_str()) == INVALID_FILE_ATTRIBUTES ? State::Fail : State::Pass,
        core);

    if (found == 0) {
        Add(s, Tr(L"Verdict", L"Итог"), State::Warn,
            Tr(L"no Adobe application found - the plug-in has nothing to serve",
               L"ни одного приложения Adobe не найдено — плагину нечего обслуживать"));
    }
}

// --------------------------------------------------------------------------
// Сам плагин
// --------------------------------------------------------------------------

std::wstring ExeFolder()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *(slash + 1) = 0;
    return path;
}

std::wstring FileVersionOf(const std::wstring& file)
{
    DWORD dummy = 0;
    const DWORD size = GetFileVersionInfoSizeW(file.c_str(), &dummy);
    if (!size) return std::wstring();

    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(file.c_str(), 0, size, data.data())) return std::wstring();

    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", (LPVOID*)&fi, &len) || !fi) return std::wstring();

    return Format(L"%u.%u.%u", HIWORD(fi->dwFileVersionMS),
                  LOWORD(fi->dwFileVersionMS), HIWORD(fi->dwFileVersionLS));
}

void CheckPlugin(Section& s)
{
    // Рядом с нами — если запущено из папки плагина; иначе там, куда его ставили
    const std::wstring here = ExeFolder() + L"Aether.prm";
    const std::wstring there = MediaCore() + L"\\Aether\\Aether.prm";

    std::wstring prm;
    if (GetFileAttributesW(here.c_str()) != INVALID_FILE_ATTRIBUTES)       prm = here;
    else if (GetFileAttributesW(there.c_str()) != INVALID_FILE_ATTRIBUTES) prm = there;

    if (prm.empty()) {
        Add(s, Tr(L"Plug-in file", L"Файл плагина"), State::Fail,
            Tr(L"Aether.prm found neither beside this program nor in the Adobe folder",
               L"Aether.prm не найден ни рядом с программой, ни в папке Adobe"));
        return;
    }

    const std::wstring ver = FileVersionOf(prm);
    Add(s, Tr(L"Plug-in file", L"Файл плагина"), State::Pass, prm);
    Add(s, Tr(L"Plug-in version", L"Версия плагина"),
        ver.empty() ? State::Warn : State::Info,
        ver.empty() ? std::wstring(Tr(L"no version resource - a build older than 1.2.2",
                                      L"нет ресурса версии — сборка старее 1.2.2")) : ver);

    // Версия окна настроек и версия плагина обязаны совпадать: разъехались —
    // значит установка неполная, и половина поведения не та, что ожидается.
    const std::wstring mine = AETHER_VERSION_WSTR;
    if (!ver.empty() && ver != mine) {
        Add(s, Tr(L"Versions agree", L"Совпадение версий"), State::Warn,
            std::wstring(Tr(L"plug-in ", L"плагин ")) + ver +
            Tr(L", this window ", L", это окно ") + mine +
            Tr(L" - reinstall", L" — переустановите"));
    }

    // Загрузка. Ровно то, что делает Premiere, и ровно то, что чаще всего
    // не получается: не тот разряд, нет библиотек рядом, нет прав.
    HMODULE m = LoadLibraryExW(prm.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m) {
        std::wostringstream why;
        why << Tr(L"did not load, Windows error ", L"не загрузился, ошибка Windows ")
            << GetLastError();
        Add(s, Tr(L"Loading the plug-in", L"Загрузка плагина"), State::Fail, why.str());
        return;
    }
    Add(s, Tr(L"Loading the plug-in", L"Загрузка плагина"), State::Pass,
        Tr(L"the library loads", L"библиотека загружается"));

    Add(s, Tr(L"Entry point", L"Точка входа"),
        GetProcAddress(m, "xImportEntry") ? State::Pass : State::Fail,
        GetProcAddress(m, "xImportEntry")
            ? Tr(L"xImportEntry is there", L"xImportEntry на месте")
            : Tr(L"no xImportEntry - Premiere will treat the file as somebody else's",
                 L"xImportEntry нет — Premiere сочтёт файл чужим"));
    FreeLibrary(m);
}

void CheckFfmpeg(Section& s)
{
    Add(s, L"libavcodec", State::Info,
        Format(L"%u.%u.%u", AV_VERSION_MAJOR(avcodec_version()),
               AV_VERSION_MINOR(avcodec_version()), AV_VERSION_MICRO(avcodec_version())));
    Add(s, L"libavformat", State::Info,
        Format(L"%u.%u.%u", AV_VERSION_MAJOR(avformat_version()),
               AV_VERSION_MINOR(avformat_version()), AV_VERSION_MICRO(avformat_version())));

    // Наличие декодеров спрашиваем у самого ffmpeg, а не пишем «есть» в отчёт:
    // сборку могли подменить, и тогда правда важнее нашей уверенности.
    struct Want { const char* codec; const wchar_t* label; };
    const Want wanted[] = {
        { "libdav1d",  Tr(L"dav1d (AV1, processor)",  L"dav1d (AV1, процессор)") },
        { "libvpx-vp9",Tr(L"libvpx (VP9, processor)", L"libvpx (VP9, процессор)") },
        { "av1_cuvid", L"av1_cuvid (AV1, NVIDIA)" },
        { "vp9_cuvid", L"vp9_cuvid (VP9, NVIDIA)" },
        { "av1_qsv",   L"av1_qsv (AV1, Intel)" },
    };
    for (const Want& w : wanted) {
        const AVCodec* c = avcodec_find_decoder_by_name(w.codec);
        Add(s, w.label, c ? State::Pass : State::Skip,
            c ? Tr(L"present", L"есть")
              : Tr(L"absent from this FFmpeg build", L"нет в этой сборке ffmpeg"));
    }

    const wchar_t* mode = Tr(L"automatic", L"автоматически");
    switch (av1imp::CurrentMode()) {
        case av1imp::DecodeMode::Software:
            mode = Tr(L"processor (set by hand)", L"процессором (задано вручную)"); break;
        case av1imp::DecodeMode::Hardware:
            mode = Tr(L"graphics card (set by hand)", L"видеокартой (задано вручную)"); break;
        default: break;
    }
    Add(s, Tr(L"Decoder choice", L"Выбор декодера"), State::Info, mode);
}

// --------------------------------------------------------------------------
// Распаковка
// --------------------------------------------------------------------------

// Одна и та же проверка для вшитых клипов и для файла человека: открыть,
// прочитать описание, взять кадр из начала и кадр из середины, взять звук.
//
// Середина важнее начала: первый кадр отдаётся почти всегда, потому что
// он ключевой. Ломается обычно перемотка.
void CheckOneFile(Section& s, const std::wstring& path, const wchar_t* label,
                  bool verbose)
{
    av1imp::Decoder dec;
    if (!dec.Open(Narrow(path), av1imp::PreferHardware(), true)) {
        Add(s, label, State::Fail,
            std::wstring(Tr(L"did not open: ", L"не открылся: ")) + Widen(dec.LastError()));
        return;
    }

    const av1imp::MediaInfo& mi = dec.Info();

    if (verbose) {
        std::wostringstream inside;
        inside << Widen(mi.codecName) << L" " << mi.width << L"x" << mi.height
               << Tr(L", ", L", ") << mi.bitDepth << Tr(L"-bit, ", L" бит, ")
               << std::fixed << std::setprecision(3) << mi.fps
               << Tr(L" fps, ", L" кадр/с, ")
               << std::setprecision(1) << mi.durationSec << Tr(L" s", L" с");
        Add(s, Tr(L"Inside", L"Внутри"), State::Info, inside.str());
        Add(s, Tr(L"Decoder", L"Декодер"), State::Info, Widen(mi.decoderName) +
            (mi.hardwareDecode ? Tr(L" (graphics card)", L" (видеокарта)")
                               : Tr(L" (processor)",    L" (процессор)")));
        Add(s, Tr(L"Colour", L"Цвет"), State::Info, Widen(mi.ColourSummary()));
        if (mi.IsHdr()) {
            Add(s, L"HDR", State::Info,
                Tr(L"the curve is declared to the host, Premiere does the tone mapping - we do not",
                   L"кривая объявляется хосту, сводит Premiere — мы не тон-маппим"));
        }
        if (mi.bitDepth > 8 && (mi.colourTransfer == 0 || mi.colourTransfer == 2) &&
            (mi.colourPrimaries == 9 || mi.colourMatrix == 9 || mi.colourMatrix == 10)) {
            Add(s, Tr(L"Transfer curve", L"Кривая переноса"), State::Warn,
                Tr(L"10-bit BT.2020 with no PQ/HLG in the tags - the host may take it for SDR",
                   L"10-bit BT.2020 без PQ/HLG в тегах — хост может принять это за SDR"));
        }
        if (mi.bitDepth == 10 && !av1imp::Yuv10Enabled()) {
            Add(s, Tr(L"10-bit planes", L"10-bit плоскости"), State::Warn,
                Tr(L"yuv10 is off: the frame goes through BGRA16, tens of times slower than P010",
                   L"yuv10 выключен: кадр пойдёт через BGRA16, это в десятки раз медленнее P010"));
        }
        // Дорожку надо открыть: до этого число каналов и частота пустые,
        // и отчёт показывал бы «каналов 0, 0 Гц» у совершенно исправного файла.
        if (mi.audioStreamCount > 0 && dec.OpenAudio(0)) {
            const av1imp::MediaInfo audio = dec.Info();
            std::wostringstream sound;
            sound << Tr(L"tracks ", L"дорожек ")   << audio.audioStreamCount
                  << Tr(L", channels ", L", каналов ") << audio.audioChannels
                  << L", " << audio.audioSampleRate << Tr(L" Hz, ", L" Гц, ")
                  << Tr(L"samples ", L"отсчётов ") << audio.audioSampleCount;
            Add(s, Tr(L"Audio", L"Звук"), State::Info, sound.str());
        } else if (mi.audioStreamCount > 0) {
            std::wostringstream sound;
            sound << Tr(L"tracks ", L"дорожек ") << mi.audioStreamCount
                  << Tr(L", but the first one did not open",
                        L", но первая не открылась");
            Add(s, Tr(L"Audio", L"Звук"), State::Fail, sound.str());
        } else {
            Add(s, Tr(L"Audio", L"Звук"), State::Skip, Tr(L"no tracks", L"дорожек нет"));
        }
    }

    if (!mi.hasVideo) {
        Add(s, label, State::Pass,
            Tr(L"audio-only file, opened", L"файл только со звуком, открыт"));
        return;
    }

    const int stride = mi.width * 4;
    std::vector<uint8_t> buf((size_t)stride * mi.height);

    if (!dec.GetFrameBGRA(0, buf.data(), stride, mi.width, mi.height)) {
        Add(s, label, State::Fail,
            Tr(L"the first frame did not arrive", L"первый кадр не отдался"));
        return;
    }

    const int64_t middle = mi.frameCount > 2 ? mi.frameCount / 2 : 0;
    if (middle && !dec.GetFrameBGRA(middle, buf.data(), stride, mi.width, mi.height)) {
        std::wostringstream why;
        why << Tr(L"frame ", L"кадр ") << middle
            << Tr(L" did not arrive - seeking is broken",
                  L" не отдался — сломана перемотка");
        Add(s, label, State::Fail, why.str());
        return;
    }

    // Ещё раз первый: проверяем возврат назад, он идёт другим путём, чем
    // движение вперёд, и ломается отдельно.
    if (middle && !dec.GetFrameBGRA(0, buf.data(), stride, mi.width, mi.height)) {
        Add(s, label, State::Fail,
            Tr(L"going back to the start did not work", L"возврат к началу не сработал"));
        return;
    }

    if (mi.bitDepth == 10 && dec.CanDeliverP010()) {
        const int strideY = mi.width * (int)sizeof(uint16_t);
        const int chromaH = (mi.height + 1) / 2;
        std::vector<uint8_t> p010((size_t)strideY * mi.height + (size_t)strideY * chromaH);
        if (!dec.GetFrameP010(0, p010.data(), strideY,
                              p010.data() + (size_t)strideY * mi.height, strideY,
                              mi.width, mi.height)) {
            Add(s, label, State::Fail,
                Tr(L"the first frame arrived but the 10-bit planes did not",
                   L"первый кадр есть, а 10-bit плоскости не отдались"));
            return;
        }
    }

    std::wostringstream done;
    done << mi.width << L"x" << mi.height << L", " << mi.bitDepth
         << Tr(L"-bit, frames 0 and ", L" бит, кадры 0 и ") << middle
         << (mi.IsHdr() ? L", HDR" : L"");
    Add(s, label, State::Pass, done.str());
}

// Вшитые клипы лежат рядом с программой, в подпапке. Установщик кладёт их
// туда же, чем они и хороши: ничего не распаковывается во временную папку,
// нечего чистить и не на что ругаться антивирусу.
void CheckBundled(Section& s, const std::function<void(const std::wstring&)>& onStep)
{
    struct Sample { const wchar_t* file; const wchar_t* label; };
    const Sample samples[] = {
        { L"av1.mp4",     Tr(L"AV1, 8-bit",  L"AV1, 8 бит") },
        { L"av1_10.mp4",  Tr(L"AV1, 10-bit", L"AV1, 10 бит") },
        { L"vp9.webm",    Tr(L"VP9 in WebM", L"VP9 в WebM") },
        { L"vfr.mkv",     Tr(L"variable frame rate, MKV", L"плавающая частота, MKV") },
        { L"opus.mka",    Tr(L"Opus, audio only", L"Opus, только звук") },
    };

    const std::wstring dir = ExeFolder() + L"samples\\";
    int missing = 0;

    for (const Sample& sm : samples) {
        const std::wstring path = dir + sm.file;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            Add(s, sm.label, State::Skip,
                Tr(L"sample file not installed", L"пробный файл не установлен"));
            ++missing;
            continue;
        }
        onStep(sm.label);
        CheckOneFile(s, path, sm.label, false);
    }

    if (missing == (int)(sizeof(samples) / sizeof(samples[0]))) {
        Add(s, Tr(L"Sample files", L"Пробные файлы"), State::Warn,
            Tr(L"the samples folder beside the program is empty - reinstall the plug-in",
               L"папка samples рядом с программой пуста — переустановите плагин"));
    }
}

} // namespace

// --------------------------------------------------------------------------

std::wstring Scrub(const std::wstring& text)
{
    std::wstring out = text;

    // Домашняя папка целиком: она содержит имя пользователя, а иногда и
    // фамилию. Меняем на переменную — путь остаётся понятным, имя уходит.
    wchar_t profile[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        const std::wstring from = profile;
        size_t at = 0;
        while ((at = out.find(from, at)) != std::wstring::npos) {
            out.replace(at, from.size(), L"%USERPROFILE%");
            at += 13;
        }
    }

    // Имя пользователя может встретиться и отдельно от пути
    wchar_t user[256] = {};
    DWORD userLen = 256;
    if (GetUserNameW(user, &userLen) && userLen > 3) {
        const std::wstring from = user;
        size_t at = 0;
        while ((at = out.find(from, at)) != std::wstring::npos) {
            out.replace(at, from.size(), L"<user>");
            at += 6;
        }
    }

    // Имя машины: в отчёте бесполезно, а в публичном месте лишнее
    wchar_t host[256] = {};
    DWORD hostLen = 256;
    if (GetComputerNameW(host, &hostLen) && hostLen > 3) {
        const std::wstring from = host;
        size_t at = 0;
        while ((at = out.find(from, at)) != std::wstring::npos) {
            out.replace(at, from.size(), L"<machine>");
            at += 9;
        }
    }

    return out;
}

bool Report::AnyFailed() const
{
    for (const Section& s : sections) {
        for (const Check& c : s.checks) {
            if (c.state == State::Fail) return true;
        }
    }
    return false;
}

namespace {

// Экранирование по JSON. Своё, потому что тащить библиотеку ради одной
// функции несоразмерно, а строк тут ровно два вида: имена и подробности.
std::wstring JsonEscape(const std::wstring& in)
{
    std::wstring out;
    out.reserve(in.size() + 8);
    for (wchar_t c : in) {
        switch (c) {
            case L'"':  out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n";  break;
            case L'\r': out += L"\\r";  break;
            case L'\t': out += L"\\t";  break;
            default:
                if (c < 0x20) {
                    wchar_t buf[8];
                    swprintf_s(buf, L"\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

const wchar_t* StateName(State s)
{
    switch (s) {
        case State::Pass: return L"pass";
        case State::Fail: return L"fail";
        case State::Warn: return L"warn";
        case State::Skip: return L"skip";
        default:          return L"info";
    }
}

} // namespace

std::wstring Report::Json() const
{
    std::wstring out = L"{\"version\":\"";
    out += AETHER_VERSION_WSTR;
    out += L"\",\"failed\":";
    out += AnyFailed() ? L"true" : L"false";
    out += L",\"sections\":[";

    bool firstSection = true;
    for (const Section& s : sections) {
        if (!firstSection) out += L",";
        firstSection = false;

        out += L"{\"title\":\"" + JsonEscape(s.title) + L"\",\"checks\":[";
        bool firstCheck = true;
        for (const Check& c : s.checks) {
            if (!firstCheck) out += L",";
            firstCheck = false;
            out += L"{\"name\":\"" + JsonEscape(c.name) +
                   L"\",\"state\":\"" + StateName(c.state) +
                   L"\",\"detail\":\"" + JsonEscape(c.detail) + L"\"}";
        }
        out += L"]}";
    }

    out += L"]}";
    return Scrub(out);
}

std::wstring Report::PlainText() const
{
    const wchar_t* mark[] = {
        L"OK  ",
        Tr(L"FAIL", L"СБОЙ"),
        L"!   ",
        L"    ",
        L"-   ",
    };

    std::wstring out = Tr(L"Aether diagnostic report\n",
                          L"Отчёт диагностики Aether\n");
    out += Tr(L"Window version: ", L"Версия окна: ");
    out += AETHER_VERSION_WSTR;
    out += L"\n\n";

    for (const Section& s : sections) {
        out += s.title + L"\n";
        for (const Check& c : s.checks) {
            out += L"  ";
            out += mark[(int)c.state];
            out += L"  " + c.name;
            if (!c.detail.empty()) out += L": " + c.detail;
            out += L"\n";
        }
        out += L"\n";
    }

    out += Tr(L"The plug-in core was checked on this machine. That does not prove\n"
              L"Premiere will open the file: it reaches the plug-in its own way.\n",
              L"Проверено ядро плагина на этой машине. Это не доказывает, что\n"
              L"Premiere откроет файл: он ходит к плагину своим путём.\n");

    return Scrub(out);
}

Report Run(const std::wstring& userFile,
           const std::function<void(const std::wstring&)>& onStep)
{
    Report r;

    onStep(Tr(L"system", L"система"));
    {
        Section s;
        s.title = Tr(L"System", L"Система");
        Add(s, L"Windows",     State::Info, WindowsName());
        Add(s, Tr(L"Processor",     L"Процессор"),  State::Info, CpuName());
        Add(s, Tr(L"Memory",        L"Память"),     State::Info, MemorySize());
        Add(s, Tr(L"Graphics card", L"Видеокарта"), State::Info, GpuName());
        r.sections.push_back(s);
    }

    onStep(Tr(L"Adobe applications", L"приложения Adobe"));
    {
        Section s;
        s.title = L"Adobe";
        CheckAdobe(s);
        r.sections.push_back(s);
    }

    onStep(Tr(L"plug-in", L"плагин"));
    {
        Section s;
        s.title = L"Aether";
        CheckPlugin(s);
        CheckFfmpeg(s);
        r.sections.push_back(s);
    }

    onStep(Tr(L"cache", L"кэш"));
    {
        Section s;
        s.title = Tr(L"Cache", L"Кэш");
        const av1imp::Settings settings = av1imp::CurrentSettings();
        const av1imp::PreviewCacheUsage usage =
            av1imp::PreviewCache::Instance().Usage();
        // Потоками, а не swprintf: строка формата не переводится (см. правило
        // в tools/localization.h). Заодно исчезли буферы на стеке.
        std::wostringstream memory;
        memory << settings.memoryCacheMB << L" MiB"
               << (settings.memoryCacheMB == 0 ? Tr(L" (off)", L" (выключен)") : L"");
        Add(s, Tr(L"Aether RAM cache", L"RAM-кэш Aether"), State::Info, memory.str());

        std::wostringstream disk;
        disk << (settings.previewCache ? Tr(L"on", L"включён") : Tr(L"off", L"выключен"))
             << Tr(L", limit ", L", лимит ") << settings.previewCacheMB << L" MiB"
             << Tr(L", used ", L", занято ")
             << std::fixed << std::setprecision(1)
             << (usage.bytes / (1024.0 * 1024.0)) << L" MiB"
             << Tr(L", files ", L", файлов ") << (unsigned long long)usage.files;
        Add(s, Tr(L"Reduced preview cache", L"Кэш уменьшенных превью"),
            State::Info, disk.str());
        Add(s, Tr(L"Folder", L"Каталог"), State::Info, usage.directory);
        const av1imp::PreviewCacheProcessStats process =
            av1imp::PreviewCache::Instance().ProcessStats();
        wchar_t activity[128] = {};
        // Строка формата здесь НЕ локализуется и не будет: это машинные
        // счётчики, одинаковые на всех языках.
        swprintf_s(activity, L"hit %llu, miss %llu, queued %llu, dropped %llu",
                   (unsigned long long)process.hits,
                   (unsigned long long)process.misses,
                   (unsigned long long)process.writesQueued,
                   (unsigned long long)process.writesDropped);
        Add(s, Tr(L"This process", L"Текущий процесс"), State::Info, activity);
        if (!process.lastWarning.empty()) {
            const int need = MultiByteToWideChar(CP_UTF8, 0,
                process.lastWarning.c_str(), -1, nullptr, 0);
            std::wstring warning(need > 1 ? (size_t)need : 1, L'\0');
            if (need > 1) {
                MultiByteToWideChar(CP_UTF8, 0, process.lastWarning.c_str(), -1,
                                    &warning[0], need);
                warning.pop_back();
            } else warning.clear();
            Add(s, Tr(L"Last warning", L"Последнее предупреждение"), State::Warn, warning);
        }
        r.sections.push_back(s);
    }

    {
        Section s;
        s.title = Tr(L"Decoding", L"Распаковка");
        CheckBundled(s, onStep);
        r.sections.push_back(s);
    }

    if (!userFile.empty()) {
        onStep(Tr(L"your file", L"ваш файл"));
        Section s;
        s.title = Tr(L"Your file", L"Ваш файл");
        Add(s, Tr(L"Path", L"Путь"), State::Info, userFile);
        CheckOneFile(s, userFile, Tr(L"Check", L"Проверка"), true);
        r.sections.push_back(s);
    }

    onStep(Tr(L"done", L"готово"));
    return r;
}

} // namespace aether
