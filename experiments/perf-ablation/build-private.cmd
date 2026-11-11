@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b %errorlevel%
"<USER>\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" "%~dp0build-private.py"
exit /b %errorlevel%
