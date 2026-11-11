@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
cd /d "<WORKSPACE>"
"<WORKSPACE>\work\rexglue-stable-package\win-amd64\bin\rexglue.exe" codegen "<WORKSPACE>\work\green-lantern-360\project\green_lantern_manifest.toml" --ignore-stamp --log-level=trace --log-file "<WORKSPACE>\work\green-lantern-360\hint-82295960-codegen.log"
