@echo off
rem Builds the mod and packs release archives with a texture pack:
rem   package.cmd <7z.exe> <textures folder> <name>     e.g.
rem   package.cmd "C:\Program Files\7-Zip\7z.exe" build\pack\textures-x4 BionicleHeroesHD-x4
rem -> dist\<name>.7z, extracted straight into the game folder (next to Bionicle.exe). It contains the
rem mod, its ini and README, Ultimate ASI Loader (dinput8.dll) and BionicleHeroesHD\textures.
setlocal
set "ROOT=%~dp0"
set "SEVENZIP=%~1"
set "TEXTURES=%~f2"
set "NAME=%~3"
set "UAL=%ROOT%third_party\ultimate-asi-loader\dinput8.dll"

if "%NAME%"=="" (
  echo usage: package.cmd ^<7z.exe^> ^<textures folder^> ^<archive name^>
  exit /b 1
)
if not exist "%UAL%" (
  echo Missing %UAL%: the x86 dinput8.dll from https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases
  exit /b 1
)
if not exist "%TEXTURES%\*.dds" (
  echo No .dds files in %TEXTURES%
  exit /b 1
)

call "%ROOT%build.cmd" || exit /b 1

set "STAGE=%ROOT%dist\stage-%NAME%"
set "OUT=%ROOT%dist\%NAME%.7z"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\BionicleHeroesHD-licenses" || exit /b 1
mkdir "%STAGE%\BionicleHeroesHD\textures" || exit /b 1

copy /y "%ROOT%build\Release\BionicleHeroesHD.asi" "%STAGE%\" >nul || exit /b 1
copy /y "%ROOT%BionicleHeroesHD.ini" "%STAGE%\" >nul || exit /b 1
copy /y "%UAL%" "%STAGE%\dinput8.dll" >nul || exit /b 1
copy /y "%ROOT%README.md" "%STAGE%\BionicleHeroesHD-README.md" >nul || exit /b 1
copy /y "%ROOT%third_party\ultimate-asi-loader\LICENSE" "%STAGE%\BionicleHeroesHD-licenses\Ultimate-ASI-Loader.txt" >nul || exit /b 1
copy /y "%ROOT%third_party\minhook\LICENSE.txt" "%STAGE%\BionicleHeroesHD-licenses\MinHook.txt" >nul || exit /b 1
robocopy "%TEXTURES%" "%STAGE%\BionicleHeroesHD\textures" *.dds /njh /njs /nfl /ndl /np >nul
if errorlevel 8 exit /b 1

if exist "%OUT%" del "%OUT%"
pushd "%STAGE%" || exit /b 1
"%SEVENZIP%" a -t7z -mx=9 -m0=lzma2 -md=256m -ms=on -mmt=on "%OUT%" * >nul || (popd & exit /b 1)
popd
rmdir /s /q "%STAGE%"
echo Created %OUT%
