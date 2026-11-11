@echo off
rem =========================================================================
rem Script d'execution automatisee des tests unitaires et de non-regression
rem pour Evolution-WindowsPort (Windows 11 / MSVC).
rem
rem Ce script configure l'environnement MSVC, compile chaque suite de tests
rem et execute l'ensemble des verifications (helpers, contrats, reglages, ABI).
rem Code de retour : 0 si tous les tests reussissent, 1 en cas d'echec.
rem =========================================================================

setlocal enabledelayedexpansion

call "%~dp0env.bat" || (
    echo [ERREUR] Impossible d'initialiser l'environnement MSVC via env.bat.
    exit /b 1
)

set "TEST_BIN_DIR=%BUILD_DIR%\tests"
if not exist "%TEST_BIN_DIR%" mkdir "%TEST_BIN_DIR%"

echo.
echo ======================================================
echo   COMPILATION DES SUITES DE TESTS UNITAIRES (MSVC)
echo ======================================================

rem 1. Compilation des tests unitaires Helpers WinUI (C++17)
echo [1/3] Compilation de test_helpers.cpp ...
cl.exe /nologo /std:c++17 /EHsc /utf-8 /W4 /O2 ^
    "%EVO_WORKSPACE%\tests\unit\test_helpers.cpp" ^
    /Fe:"%TEST_BIN_DIR%\test_helpers.exe" ^
    /Fo:"%TEST_BIN_DIR%\test_helpers.obj" || (
        echo [ERREUR] Echec de la compilation de test_helpers.cpp
        exit /b 1
    )

rem 2. Compilation des tests unitaires Contrats evo-core (C)
echo [2/3] Compilation de test_evo_core_contracts.c ...
cl.exe /nologo /utf-8 /W4 /O2 ^
    "%EVO_WORKSPACE%\tests\unit\test_evo_core_contracts.c" ^
    /Fe:"%TEST_BIN_DIR%\test_evo_core_contracts.exe" ^
    /Fo:"%TEST_BIN_DIR%\test_evo_core_contracts.obj" || (
        echo [ERREUR] Echec de la compilation de test_evo_core_contracts.c
        exit /b 1
    )

rem 3. Compilation des tests unitaires Preferences Settings (C++17)
echo [3/3] Compilation de test_settings.cpp ...
cl.exe /nologo /std:c++17 /EHsc /utf-8 /W4 /O2 ^
    "%EVO_WORKSPACE%\tests\unit\test_settings.cpp" ^
    /Fe:"%TEST_BIN_DIR%\test_settings.exe" ^
    /Fo:"%TEST_BIN_DIR%\test_settings.obj" || (
        echo [ERREUR] Echec de la compilation de test_settings.cpp
        exit /b 1
    )

rem 4. Compilation des tests unitaires Categories unifiees (C++17)
echo [4/4] Compilation de test_categories.cpp ...
cl.exe /nologo /std:c++17 /EHsc /utf-8 /W4 /O2 ^
    "%EVO_WORKSPACE%\tests\unit\test_categories.cpp" ^
    /Fe:"%TEST_BIN_DIR%\test_categories.exe" ^
    /Fo:"%TEST_BIN_DIR%\test_categories.obj" || (
        echo [ERREUR] Echec de la compilation de test_categories.cpp
        exit /b 1
    )

rem Compilation des tests des strategies de groupe (C++17 ; Policy.h)
echo [policy] Compilation de test_policy.cpp ...
cl.exe /nologo /std:c++17 /EHsc /utf-8 /W4 /O2 ^
    "%EVO_WORKSPACE%\tests\unit\test_policy.cpp" ^
    /Fe:"%TEST_BIN_DIR%\test_policy.exe" ^
    /Fo:"%TEST_BIN_DIR%\test_policy.obj" advapi32.lib || (
        echo [ERREUR] Echec de la compilation de test_policy.cpp
        exit /b 1
    )

rem Compilation des tests des regles de messagerie (C++17 ; Rules.h et evo-core-rules-util.h)
echo [regles] Compilation de test_rules.cpp ...
cl.exe /nologo /std:c++17 /EHsc /utf-8 /W4 /O2 ^
    "%EVO_WORKSPACE%\tests\unit\test_rules.cpp" ^
    /Fe:"%TEST_BIN_DIR%\test_rules.exe" ^
    /Fo:"%TEST_BIN_DIR%\test_rules.obj" user32.lib || (
        echo [ERREUR] Echec de la compilation de test_rules.cpp
        exit /b 1
    )

rem Compilation des tests du classificateur bayesien anti-spam (C)
echo [spam] Compilation de test_spam.c ...
cl.exe /nologo /utf-8 /W4 /O2 ^
    "%EVO_WORKSPACE%\tests\unit\test_spam.c" ^
    /Fe:"%TEST_BIN_DIR%\test_spam.exe" ^
    /Fo:"%TEST_BIN_DIR%\test_spam.obj" || (
        echo [ERREUR] Echec de la compilation de test_spam.c
        exit /b 1
    )

rem Compilation des tests de l'index plein texte local (C)
echo [index] Compilation de test_fulltext_index.c ...
cl.exe /nologo /utf-8 /W4 /O2 ^
    "%EVO_WORKSPACE%\tests\unit\test_fulltext_index.c" ^
    /Fe:"%TEST_BIN_DIR%\test_fulltext_index.exe" ^
    /Fo:"%TEST_BIN_DIR%\test_fulltext_index.obj" || (
        echo [ERREUR] Echec de la compilation de test_fulltext_index.c
        exit /b 1
    )

rem Compilation des tests de la file hors ligne et des conflits (C)
echo [hors-ligne] Compilation de test_offline_queue.c ...
cl.exe /nologo /utf-8 /W4 /O2 ^
    "%EVO_WORKSPACE%\tests\unit\test_offline_queue.c" ^
    /Fe:"%TEST_BIN_DIR%\test_offline_queue.exe" ^
    /Fo:"%TEST_BIN_DIR%\test_offline_queue.obj" || (
        echo [ERREUR] Echec de la compilation de test_offline_queue.c
        exit /b 1
    )

rem Compilation des tests d'export / import du courrier et des contacts (C)
echo [transfert] Compilation de test_transfer.c ...
cl.exe /nologo /utf-8 /W4 /O2 ^
    "%EVO_WORKSPACE%\tests\unit\test_transfer.c" ^
    /Fe:"%TEST_BIN_DIR%\test_transfer.exe" ^
    /Fo:"%TEST_BIN_DIR%\test_transfer.obj" || (
        echo [ERREUR] Echec de la compilation de test_transfer.c
        exit /b 1
    )

echo Compilation achevee avec succes.
echo.

rem =========================================================================
rem EXECUTION DES TESTS UNITAIRES ET DE NON-REGRESSION
rem =========================================================================

set "FAILED_SUITES=0"

rem Execution de la suite 1 : Helpers WinUI
"%TEST_BIN_DIR%\test_helpers.exe"
if !errorlevel! neq 0 set /a FAILED_SUITES+=1

rem Execution de la suite 2 : Contrats evo-core
"%TEST_BIN_DIR%\test_evo_core_contracts.exe"
if !errorlevel! neq 0 set /a FAILED_SUITES+=1

rem Execution de la suite 3 : Preferences Settings
"%TEST_BIN_DIR%\test_settings.exe"
if !errorlevel! neq 0 set /a FAILED_SUITES+=1

rem Execution de la suite 4 : Categories unifiees
"%TEST_BIN_DIR%\test_categories.exe"
if !errorlevel! neq 0 set /a FAILED_SUITES+=1

rem Execution de la suite : strategies de groupe
"%TEST_BIN_DIR%\test_policy.exe"
if !errorlevel! neq 0 set /a FAILED_SUITES+=1

rem Execution de la suite : regles de messagerie
"%TEST_BIN_DIR%\test_rules.exe"
if !errorlevel! neq 0 set /a FAILED_SUITES+=1

rem Execution de la suite : classificateur bayesien anti-spam
"%TEST_BIN_DIR%\test_spam.exe"
if !errorlevel! neq 0 set /a FAILED_SUITES+=1

rem Execution de la suite : index plein texte local
"%TEST_BIN_DIR%\test_fulltext_index.exe"
if !errorlevel! neq 0 set /a FAILED_SUITES+=1

rem Execution de la suite : file hors ligne et conflits
"%TEST_BIN_DIR%\test_offline_queue.exe"
if !errorlevel! neq 0 set /a FAILED_SUITES+=1

rem Execution de la suite : export / import du courrier et des contacts
"%TEST_BIN_DIR%\test_transfer.exe"
if !errorlevel! neq 0 set /a FAILED_SUITES+=1

rem Execution du test d'assemblage du XAML de la fenetre principale
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%EVO_WORKSPACE%\tests\xaml\test_xaml_assembly.ps1"
if !errorlevel! neq 0 set /a FAILED_SUITES+=1

rem Catalogues de traduction : chaque texte source present dans les 7 langues
python "%EVO_WORKSPACE%\tools\i18n-sync.py" check
if !errorlevel! neq 0 set /a FAILED_SUITES+=1

rem Execution du test d'export ABI DLL
if exist "%BUILD_DIR%\evo-core.dll" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%EVO_WORKSPACE%\tests\abi\test_abi_exports.ps1"
    if !errorlevel! neq 0 set /a FAILED_SUITES+=1
) else (
    echo [AVERTISSEMENT] build\evo-core.dll introuvable : test ABI ignore.
)

echo.
echo ======================================================
echo   SYNTHESE GENERALE DE LA CAMPAGNE DE TESTS
echo ======================================================
if !FAILED_SUITES! equ 0 (
    echo   TOUTES LES SUITES DE TESTS SONT VALIDEES AVEC SUCCES !
    echo ======================================================
    exit /b 0
) else (
    echo   ECHEC : !FAILED_SUITES! suites de tests ont echoue.
    echo ======================================================
    exit /b 1
)
