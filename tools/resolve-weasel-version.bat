@echo off

rem Shared version rule for build.bat and xbuild.bat.
if not defined VERSION_MAJOR set "VERSION_MAJOR=0"
if not defined VERSION_MINOR set "VERSION_MINOR=17"
if not defined VERSION_PATCH set "VERSION_PATCH=4"
if not defined WEASEL_VERSION set "WEASEL_VERSION=%VERSION_MAJOR%.%VERSION_MINOR%.%VERSION_PATCH%"
if not defined WEASEL_BUILD set "WEASEL_BUILD=0"

set "PRODUCT_VERSION=%WEASEL_VERSION%.%WEASEL_BUILD%"
if defined RELEASE_BUILD goto finish

git --version >nul 2>&1
if errorlevel 1 (
  >&2 echo Warning: Git is unavailable; using WEASEL_BUILD=%WEASEL_BUILD%.
  goto finish
)

rem This repository starts at build 194 with a single root commit.
rem Count commits from that root so later updates keep increasing the build number.
set "_WEASEL_BASE_REF=%WEASEL_VERSION_BASE_REF%"
if defined _WEASEL_BASE_REF goto verify_base
if "%WEASEL_VERSION%"=="0.17.4" goto count_snapshot

rem Future versions use only their exact upstream version tag.
git rev-parse --verify --quiet "refs/tags/%WEASEL_VERSION%" >nul 2>&1
if errorlevel 1 goto missing_base
set "_WEASEL_BASE_REF=refs/tags/%WEASEL_VERSION%"

:verify_base
git merge-base --is-ancestor "%_WEASEL_BASE_REF%" HEAD >nul 2>&1
if errorlevel 1 goto invalid_base
for /f %%i in ('git rev-list "%_WEASEL_BASE_REF%..HEAD" --count') do set "WEASEL_BUILD=%%i"
goto set_product_version

:count_snapshot
for /f %%i in ('git rev-list HEAD --count') do set /a WEASEL_BUILD=193+%%i >nul

:set_product_version
for /f %%i in ('git rev-parse --short HEAD') do set "PRODUCT_VERSION=%WEASEL_VERSION%.%WEASEL_BUILD%.%%i"
set "_WEASEL_BASE_REF="
goto finish

:missing_base
>&2 echo ERROR: Missing exact base tag for WEASEL_VERSION=%WEASEL_VERSION%.
set "_WEASEL_BASE_REF="
exit /b 1

:invalid_base
>&2 echo ERROR: Version baseline is not an ancestor of HEAD.
set "_WEASEL_BASE_REF="
exit /b 1

:finish
if not defined FILE_VERSION set "FILE_VERSION=%WEASEL_VERSION%.%WEASEL_BUILD%"
exit /b 0
