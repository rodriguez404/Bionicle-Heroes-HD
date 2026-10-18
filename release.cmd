@echo off
rem Makes a GitHub release with both texture packs:
rem   release.cmd <tag> [--publish | --dry-run]          e.g.  release.cmd v1.0
rem 1. checks that the working tree is clean and HEAD is pushed (the release is tagged on HEAD),
rem 2. packs dist\BionicleHeroesHD-<tag>-x2.7z and -x4.7z (package.cmd) from build\pack\textures-x2 / -x4,
rem 3. creates the release with gh and uploads both archives. Without --publish it stays a draft:
rem    check it on GitHub and press "Publish release" there. --dry-run does everything but step 3.
rem 7z.exe and gh.exe are taken from PATH or the variables SEVENZIP / GH; local.cmd (not in git) can set them.
setlocal
set "ROOT=%~dp0"
set "TAG=%~1"
set "DRAFT=--draft"
set "DRYRUN="
if /i "%~2"=="--publish" set "DRAFT="
if /i "%~2"=="--dry-run" set "DRYRUN=1"
if "%TAG%"=="" (
  echo usage: release.cmd ^<tag^> [--publish ^| --dry-run]
  exit /b 1
)
if exist "%ROOT%local.cmd" call "%ROOT%local.cmd"
if not defined SEVENZIP for %%i in (7z.exe) do set "SEVENZIP=%%~$PATH:i"
if not defined GH for %%i in (gh.exe) do set "GH=%%~$PATH:i"
if not defined SEVENZIP (
  echo 7z.exe not found: put it on PATH or set SEVENZIP in local.cmd
  exit /b 1
)
if not defined GH (
  echo gh.exe not found: put it on PATH or set GH in local.cmd
  exit /b 1
)
set "X2=%ROOT%build\pack\textures-x2"
set "X4=%ROOT%build\pack\textures-x4"
pushd "%ROOT%" || exit /b 1

rem The release is tagged on HEAD, which must be on GitHub and match the files that get packed.
for /f %%s in ('git status --porcelain') do (
  echo Uncommitted changes: commit or stash them first.
  popd & exit /b 1
)
git fetch -q origin || (popd & exit /b 1)
for /f %%h in ('git rev-parse HEAD') do set "HEAD=%%h"
git branch -r --contains %HEAD% | findstr /r "." >nul || (
  echo HEAD %HEAD% is not pushed yet: git push first.
  popd & exit /b 1
)
"%GH%" release view "%TAG%" >nul 2>&1 && (
  echo Release %TAG% already exists.
  popd & exit /b 1
)

call "%ROOT%package.cmd" "%SEVENZIP%" "%X2%" "BionicleHeroesHD-%TAG%-x2" || (popd & exit /b 1)
call "%ROOT%package.cmd" "%SEVENZIP%" "%X4%" "BionicleHeroesHD-%TAG%-x4" || (popd & exit /b 1)

set "NOTES=%ROOT%dist\notes-%TAG%.md"
> "%NOTES%" (
  echo HD textures for Bionicle Heroes ^(PC^), upscaled with Real-ESRGAN: levels, characters, weapons, HUD and menus.
  echo.
  echo Download **one** of the two packs and extract it into the game folder ^(next to `Bionicle.exe`^):
  echo.
  echo - **x4** ^(`BionicleHeroesHD-%TAG%-x4.7z`^): 4x width and height, ~1.8 GB installed.
  echo - **x2** ^(`BionicleHeroesHD-%TAG%-x2.7z`^): 2x width and height, ~450 MB installed.
  echo.
  echo Ultimate ASI Loader ^(`dinput8.dll`^) is included. Open the archives with 7-Zip or WinRAR. See the README for details.
)

if defined DRYRUN (
  echo Dry run: would create release %TAG% on %HEAD% with dist\BionicleHeroesHD-%TAG%-x2.7z and -x4.7z, notes %NOTES%
  popd & exit /b 0
)
"%GH%" release create "%TAG%" --target %HEAD% --title "Bionicle Heroes HD %TAG%" --notes-file "%NOTES%" %DRAFT% ^
  "dist\BionicleHeroesHD-%TAG%-x2.7z" "dist\BionicleHeroesHD-%TAG%-x4.7z" || (popd & exit /b 1)
popd
if defined DRAFT (
  echo Draft release %TAG% created: review it on GitHub and press "Publish release".
) else (
  echo Release %TAG% published.
)
