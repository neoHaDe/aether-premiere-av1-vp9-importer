@echo off
REM Builds AV1Importer.prm.
REM
REM Comments here are English on purpose. cmd.exe walks a batch file by byte
REM offset and re-seeks after every line; when the file is UTF-8 but the console
REM code page is not, those offsets drift and a later command gets cut in half
REM ("vcxproj" arriving as "xproj"). ASCII-only keeps the file immune to that.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
msbuild src\AV1Importer.vcxproj /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
if errorlevel 1 exit /b 1

REM FFmpeg DLLs belong next to the .prm. The plug-in loads them by full path
REM from its own folder and will not search PATH: that search is how a DLL
REM next to Premiere or in CWD would execute inside Adobe.
if not exist build\Release mkdir build\Release
for %%D in (avutil-60.dll swresample-6.dll swscale-9.dll avcodec-62.dll avformat-62.dll) do (
    copy /y "ffmpeg\bin\%%D" "build\Release\" >nul
    if errorlevel 1 exit /b 1
)

REM cl does not create the intermediate directory itself - see build-test.bat
if not exist build\obj mkdir build\obj

REM The icon, so the settings window is not a blank rectangle in the taskbar
rc /nologo /i src /fo build\obj\aether.res installer\Aether.rc
if errorlevel 1 exit /b 1

REM AetherDiagnose has no window, so it gets no icon and no manifest -
REM only the version, so Properties on the file is not blank.
rc /nologo /i src /fo build\obj\diagnose.res installer\AetherDiagnose.rc
if errorlevel 1 exit /b 1

REM Aether.exe - the plug-in window: settings and diagnostics.
REM /MT, not /MD: the plug-in must not depend on msvcp140.dll, and the window
REM is kept consistent with it so a user without the redistributable can run it.
cl /nologo /utf-8 /std:c++17 /EHsc /O2 /MT /DUNICODE /D_UNICODE ^
   /D__STDC_CONSTANT_MACROS /D__STDC_LIMIT_MACROS ^
   /I"ffmpeg\include" ^
   tools\aether_app.cpp tools\app_diagnose.cpp tools\localization.cpp ^
   src\AV1Settings.cpp src\PreviewCache.cpp src\AV1Log.cpp src\AV1Decoder.cpp build\obj\aether.res ^
   /Fe:build\Release\Aether.exe "/Fo:build\obj\\" ^
   /link /SUBSYSTEM:WINDOWS /LIBPATH:"ffmpeg\lib" ^
   avcodec.lib avformat.lib avutil.lib swscale.lib swresample.lib ^
   shell32.lib ole32.lib user32.lib gdi32.lib comdlg32.lib advapi32.lib version.lib bcrypt.lib

REM AetherDiagnose.exe - the diagnostic engine with no window of its own.
REM Ships with the plug-in: the CEP panel inside Premiere is HTML and cannot
REM decode video at all, so it runs this and reads the JSON back. One engine
REM for the panel and the window means the two cannot drift apart.
cl /nologo /utf-8 /std:c++17 /EHsc /O2 /MT ^
   /D__STDC_CONSTANT_MACROS /D__STDC_LIMIT_MACROS ^
   /I"ffmpeg\include" ^
   tools\diagnose_app.cpp tools\app_diagnose.cpp tools\localization.cpp ^
   src\AV1Settings.cpp src\PreviewCache.cpp src\AV1Log.cpp src\AV1Decoder.cpp build\obj\diagnose.res ^
   /Fe:build\Release\AetherDiagnose.exe "/Fo:build\obj\\" ^
   /link /LIBPATH:"ffmpeg\lib" ^
   avcodec.lib avformat.lib avutil.lib swscale.lib swresample.lib ^
   shell32.lib ole32.lib user32.lib advapi32.lib version.lib bcrypt.lib
