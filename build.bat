@echo off
setlocal enabledelayedexpansion
title Perfect Dark VR - Build

:: ============================================================
::  Couleurs ANSI
:: ============================================================
for /f %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"
set "CYAN=%ESC%[96m"
set "YEL=%ESC%[93m"
set "GRN=%ESC%[92m"
set "RED=%ESC%[91m"
set "MAG=%ESC%[95m"
set "BLU=%ESC%[94m"
set "RST=%ESC%[0m"
set "BLD=%ESC%[1m"

:: ── Valeurs par defaut ──────────────────────────────────────
set "TARGET=pc"
set "REGION=ntsc"
set "ROMID=ntsc-final"
set "BUILDTYPE=RelWithDebInfo"
set "DO_INSTALL=0"
set "DO_CLEAN=0"

:: ── Chemins ─────────────────────────────────────────────────
set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"
set "ANDROID_DIR=%ROOT_DIR%\android"
set "BUILD_DIR=%ROOT_DIR%\build"

:: ============================================================
:MAIN_MENU
:: ============================================================
cls

if "%DO_INSTALL%"=="1" (set "LBL_INSTALL=OUI") else (set "LBL_INSTALL=non")
if "%DO_CLEAN%"=="1"   (set "LBL_CLEAN=OUI")   else (set "LBL_CLEAN=non")
if "%TARGET%"=="pc"    (set "LBL_TARGET=PC / PCVR  ^(Windows^)") else (set "LBL_TARGET=Quest Standalone  ^(Android^)")

echo.
echo %CYAN%%BLD%============================================================%RST%
echo %CYAN%%BLD%         Perfect Dark VR  ^|  Compilation%RST%
echo %CYAN%%BLD%============================================================%RST%
echo.
echo  %BLD%[1]%RST%  Plateforme : %MAG%!LBL_TARGET!%RST%
echo  %BLD%[2]%RST%  Region     : %MAG%!REGION!%RST%
echo  %BLD%[3]%RST%  Build      : %MAG%!BUILDTYPE!%RST%
if "%TARGET%"=="android" (
    echo  %BLD%[4]%RST%  Installer sur le Quest apres build : %MAG%!LBL_INSTALL!%RST%
) else (
    echo  %BLD%[4]%RST%  Installer sur le Quest apres build : %BLU%N/A ^(PC^)%RST%
)
echo  %BLD%[5]%RST%  Nettoyer avant de compiler ^(clean^)  : %MAG%!LBL_CLEAN!%RST%
echo.
echo  %BLD%[6]%RST%  %GRN%%BLD%LANCER LA COMPILATION%RST%
echo.
echo  %BLD%[Q]%RST%  Quitter
echo.
echo %CYAN%-------------------------------------------------------------%RST%
echo.

choice /c 123456Q /n /m "  Votre choix : "

if %ERRORLEVEL%==1 goto MENU_TARGET
if %ERRORLEVEL%==2 goto MENU_REGION
if %ERRORLEVEL%==3 goto MENU_BUILD
if %ERRORLEVEL%==4 goto TOGGLE_INSTALL
if %ERRORLEVEL%==5 goto TOGGLE_CLEAN
if %ERRORLEVEL%==6 goto RUN_BUILD
if %ERRORLEVEL%==7 goto END
goto MAIN_MENU

:: ============================================================
:MENU_TARGET
:: ============================================================
cls
echo.
echo %CYAN%%BLD%  Choisir la plateforme cible%RST%
echo.
echo  %BLD%[1]%RST%  PC / PCVR         ^(Windows, OpenXR sur SteamVR / Link^)
echo  %BLD%[2]%RST%  Quest Standalone  ^(Android arm64, sideloading^)
echo.

choice /c 12 /n /m "  Votre choix : "

if %ERRORLEVEL%==1 (
    set "TARGET=pc"
    set "BUILDTYPE=RelWithDebInfo"
)
if %ERRORLEVEL%==2 (
    set "TARGET=android"
    set "BUILDTYPE=debug"
)
goto MAIN_MENU

:: ============================================================
:MENU_REGION
:: ============================================================
cls
echo.
echo %CYAN%%BLD%  Choisir la region du ROM%RST%
echo.
echo  %BLD%[1]%RST%  NTSC  ^(version americaine^)
echo  %BLD%[2]%RST%  PAL   ^(version europeenne^)
echo  %BLD%[3]%RST%  JPN   ^(version japonaise^)
echo.

choice /c 123 /n /m "  Votre choix : "

if %ERRORLEVEL%==1 (
    set "REGION=ntsc"
    set "ROMID=ntsc-final"
)
if %ERRORLEVEL%==2 (
    set "REGION=pal"
    set "ROMID=pal-final"
)
if %ERRORLEVEL%==3 (
    set "REGION=jpn"
    set "ROMID=jpn-final"
)
goto MAIN_MENU

:: ============================================================
:MENU_BUILD
:: ============================================================
cls
echo.
echo %CYAN%%BLD%  Choisir le type de build%RST%
echo.

if "%TARGET%"=="pc" goto MENU_BUILD_PC
goto MENU_BUILD_ANDROID

:MENU_BUILD_PC
echo  %BLD%[1]%RST%  RelWithDebInfo  ^(recommande : optimise + symboles debug^)
echo  %BLD%[2]%RST%  Release         ^(optimise maximum^)
echo  %BLD%[3]%RST%  Debug           ^(non optimise, logs complets^)
echo.
choice /c 123 /n /m "  Votre choix : "
if %ERRORLEVEL%==1 set "BUILDTYPE=RelWithDebInfo"
if %ERRORLEVEL%==2 set "BUILDTYPE=Release"
if %ERRORLEVEL%==3 set "BUILDTYPE=Debug"
goto MAIN_MENU

:MENU_BUILD_ANDROID
echo  %BLD%[1]%RST%  Debug    ^(plus rapide, logs actifs^)
echo  %BLD%[2]%RST%  Release  ^(optimise, pour distribution^)
echo.
choice /c 12 /n /m "  Votre choix : "
if %ERRORLEVEL%==1 set "BUILDTYPE=debug"
if %ERRORLEVEL%==2 set "BUILDTYPE=release"
goto MAIN_MENU

:: ============================================================
:TOGGLE_INSTALL
:: ============================================================
if "%TARGET%"=="pc" goto MAIN_MENU
if "%DO_INSTALL%"=="0" (set "DO_INSTALL=1") else (set "DO_INSTALL=0")
goto MAIN_MENU

:: ============================================================
:TOGGLE_CLEAN
:: ============================================================
if "%DO_CLEAN%"=="0" (set "DO_CLEAN=1") else (set "DO_CLEAN=0")
goto MAIN_MENU

:: ============================================================
:RUN_BUILD
:: ============================================================
cls

if "%DO_INSTALL%"=="1" (set "LBL_INSTALL=OUI") else (set "LBL_INSTALL=non")
if "%DO_CLEAN%"=="1"   (set "LBL_CLEAN=OUI")   else (set "LBL_CLEAN=non")
if "%TARGET%"=="pc"    (set "LBL_TGT=PC / PCVR") else (set "LBL_TGT=Quest Standalone")

echo.
echo %CYAN%%BLD%============================================================%RST%
echo %CYAN%%BLD%  Configuration selectionnee%RST%
echo %CYAN%%BLD%============================================================%RST%
echo.
echo  Plateforme : %MAG%%LBL_TGT%%RST%
echo  Region     : %MAG%%REGION%%RST%
echo  ROMID      : %MAG%%ROMID%%RST%
echo  Build      : %MAG%%BUILDTYPE%%RST%
echo  Install    : %MAG%%LBL_INSTALL%%RST%
echo  Clean      : %MAG%%LBL_CLEAN%%RST%
echo.

if "%TARGET%"=="pc"      goto BUILD_PC
if "%TARGET%"=="android" goto BUILD_ANDROID
goto MAIN_MENU

:: ╔══════════════════════════════════════════════════════════╗
:: ║                    BUILD PC / PCVR                       ║
:: ╚══════════════════════════════════════════════════════════╝
:BUILD_PC

set "CMAKE_EXE=cmake"
cmake --version >nul 2>&1
if %ERRORLEVEL% neq 0 (
    set "CMAKE_EXE=C:\msys64\mingw64\bin\cmake.exe"
    if not exist "!CMAKE_EXE!" (
        echo %RED%[ERREUR] cmake introuvable.%RST%
        echo %RED%         Installez MSYS2 et relancez.%RST%
        goto BUILD_FAILED
    )
)

set "MAKE_EXE=C:\msys64\usr\bin\make.exe"
if not exist "%MAKE_EXE%" (
    echo %RED%[ERREUR] make introuvable : %MAKE_EXE%%RST%
    echo %RED%         Assurez-vous que MSYS2 est installe correctement.%RST%
    goto BUILD_FAILED
)

if "%DO_CLEAN%"=="1" (
    echo %YEL%[...] Suppression du dossier build...%RST%
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    echo %GRN%[OK] Nettoyage termine.%RST%
    echo.
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo %YEL%[...] Configuration CMake...%RST%
echo.

"%CMAKE_EXE%" -S "%ROOT_DIR%" -B "%BUILD_DIR%" ^
    -G "Unix Makefiles" ^
    -DCMAKE_MAKE_PROGRAM="%MAKE_EXE%" ^
    -DCMAKE_BUILD_TYPE=%BUILDTYPE% ^
    -DROMID=%ROMID%

if %ERRORLEVEL% neq 0 (
    echo.
    echo %RED%[ERREUR] La configuration CMake a echoue.%RST%
    goto BUILD_FAILED
)

echo.
echo %GRN%[OK] Configuration terminee.%RST%
echo.
echo %YEL%[...] Compilation en cours...%RST%
echo.

"%CMAKE_EXE%" --build "%BUILD_DIR%" --config %BUILDTYPE% -j%NUMBER_OF_PROCESSORS%

if %ERRORLEVEL% neq 0 (
    echo.
    echo %RED%[ERREUR] La compilation a echoue.%RST%
    goto BUILD_FAILED
)

set "BIN_SUFFIX="
if "%REGION%"=="pal" set "BIN_SUFFIX=.pal"
if "%REGION%"=="jpn" set "BIN_SUFFIX=.jpn"
set "EXE_PATH=%BUILD_DIR%\pd%BIN_SUFFIX%.x86_64.exe"

if not exist "%EXE_PATH%" (
    for %%f in ("%BUILD_DIR%\pd*.exe") do set "EXE_PATH=%%f"
)

echo.
echo %GRN%%BLD%[OK] Build PC reussi !%RST%
echo.
echo  EXE : %GRN%%EXE_PATH%%RST%
echo.
goto BUILD_SUCCESS

:: ╔══════════════════════════════════════════════════════════╗
:: ║                 BUILD ANDROID / QUEST                    ║
:: ╚══════════════════════════════════════════════════════════╝
:BUILD_ANDROID

set "GRADLEW=%ANDROID_DIR%\gradlew.bat"

if not exist "%GRADLEW%" (
    echo %RED%[ERREUR] gradlew.bat introuvable : %GRADLEW%%RST%
    goto BUILD_FAILED
)

java -version >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo %RED%[ERREUR] Java introuvable. Installez Android Studio ou le JDK 17+.%RST%
    goto BUILD_FAILED
)

if "%BUILDTYPE%"=="debug"   set "GRADLE_TASK=app:assembleDebug"
if "%BUILDTYPE%"=="release" set "GRADLE_TASK=app:assembleRelease"

if "%DO_CLEAN%"=="1" (
    echo %YEL%[...] Nettoyage Gradle...%RST%
    echo.
    call "%GRADLEW%" -p "%ANDROID_DIR%" -PROMID=%ROMID% clean
    if !ERRORLEVEL! neq 0 (
        echo %RED%[ERREUR] Le nettoyage a echoue.%RST%
        goto BUILD_FAILED
    )
    echo %GRN%[OK] Nettoyage termine.%RST%
    echo.
)

echo %YEL%[...] Compilation : %GRADLE_TASK% ^(ROMID=%ROMID%^)...%RST%
echo.

call "%GRADLEW%" -p "%ANDROID_DIR%" -PROMID=%ROMID% %GRADLE_TASK%

if %ERRORLEVEL% neq 0 (
    echo.
    echo %RED%[ERREUR] La compilation a echoue.%RST%
    goto BUILD_FAILED
)

set "APK_DIR=%ANDROID_DIR%\app\build\outputs\apk\%BUILDTYPE%"
set "APK_PATH=%APK_DIR%\pd-vr-%REGION%-%BUILDTYPE%.apk"

if not exist "%APK_PATH%" (
    for %%f in ("%APK_DIR%\*.apk") do set "APK_PATH=%%f"
)

echo.
echo %GRN%%BLD%[OK] Build Android reussi !%RST%
echo.
echo  APK : %GRN%%APK_PATH%%RST%
echo.

if "%DO_INSTALL%"=="0" goto BUILD_SUCCESS

:: ── ADB ─────────────────────────────────────────────────────
echo %CYAN%-------------------------------------------------------------%RST%
echo %YEL%[...] Recherche de ADB...%RST%

set "ADB_EXE=adb"
adb version >nul 2>&1
if %ERRORLEVEL% neq 0 (
    set "ADB_EXE="
    set "LOCAL_PROPS=%ANDROID_DIR%\local.properties"
    if exist "!LOCAL_PROPS!" (
        for /f "usebackq tokens=1,* delims==" %%a in ("!LOCAL_PROPS!") do (
            if "%%a"=="sdk.dir" set "SDK_PATH=%%b"
        )
        set "SDK_PATH=!SDK_PATH:\\\\=\!"
        set "SDK_PATH=!SDK_PATH:\\=\!"
        set "ADB_EXE=!SDK_PATH!\platform-tools\adb.exe"
    )
    if not exist "!ADB_EXE!" (
        echo %RED%[ERREUR] adb introuvable. Ajoutez platform-tools au PATH.%RST%
        goto BUILD_FAILED
    )
)

echo %YEL%[...] Peripheriques detectes :%RST%
"!ADB_EXE!" devices
echo.

"!ADB_EXE!" devices 2>nul | findstr /r /c:"	device" >nul
if %ERRORLEVEL% neq 0 (
    echo %RED%[ERREUR] Aucun Quest detecte en ADB.%RST%
    echo %RED%  - Branchez le casque en USB%RST%
    echo %RED%  - Activez le mode developpeur sur le Quest%RST%
    echo %RED%  - Autorisez le debogage USB dans le casque%RST%
    goto BUILD_FAILED
)

echo %YEL%[...] Installation...%RST%
"!ADB_EXE!" install -r "!APK_PATH!"
if %ERRORLEVEL% neq 0 (
    echo %RED%[ERREUR] L'installation ADB a echoue.%RST%
    goto BUILD_FAILED
)
echo %GRN%[OK] APK installe !%RST%

set "PKG_SUFFIX="
if "%REGION%"=="pal" set "PKG_SUFFIX=.pal"
if "%REGION%"=="jpn" set "PKG_SUFFIX=.jpn"
set "PACKAGE=com.perfectdark.port!PKG_SUFFIX!"
echo %YEL%[...] Lancement de !PACKAGE!...%RST%
"!ADB_EXE!" shell monkey -p "!PACKAGE!" -c android.intent.category.LAUNCHER 1 >nul
echo %GRN%[OK] Application lancee sur le Quest.%RST%

:: ============================================================
:BUILD_SUCCESS
:: ============================================================
echo.
echo %CYAN%%BLD%============================================================%RST%
echo %GRN%%BLD%  Tout est termine avec succes !%RST%
echo %CYAN%%BLD%============================================================%RST%
echo.
pause
goto MAIN_MENU

:: ============================================================
:BUILD_FAILED
:: ============================================================
echo.
echo %RED%%BLD%============================================================%RST%
echo %RED%%BLD%  Build echoue. Voir les erreurs ci-dessus.%RST%
echo %RED%%BLD%============================================================%RST%
echo.
pause
goto MAIN_MENU

:END
exit /b 0
