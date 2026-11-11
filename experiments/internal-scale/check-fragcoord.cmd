@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0"
"<WORKSPACE>\work\glslang-host-graphics\StandAlone\Release\glslangValidator.exe" -V test.frag -o test.spv
if errorlevel 1 exit /b %errorlevel%
"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe" /nologo /std:c++20 /EHsc check-fragcoord.cpp /Fecheck-fragcoord.exe
if errorlevel 1 exit /b %errorlevel%
check-fragcoord.exe
exit /b %errorlevel%
