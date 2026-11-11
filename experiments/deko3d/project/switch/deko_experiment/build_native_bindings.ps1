param(
 [string]$Docker='<USER>/AppData/Local/Programs/DockerDesktop/resources/bin/docker.exe',
 [string]$Image='devkitpro-mesa-rust-aarch64:latest'
)
$taskWorkspace=(Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
# The command body is constant; workspace is passed as a Docker bind argument.
$taskCommand=@'
set -eu
mkdir -p /workspace/outputs/deko-native
cc=/opt/devkitpro/devkitA64/bin/aarch64-none-elf-g++
inc=/workspace/work/deko3d-sdk-reference/include
ref=/workspace/work/deko3d-sdk-reference
third=/workspace/work/rexglue-sdk/work/rexglue-sdk-horizon-reference/thirdparty
src=/workspace/work/green-lantern-360/project/switch/deko_experiment
"$cc" -std=c++20 -O2 -fPIE -D__SWITCH__ -march=armv8-a+crc+crypto -mtune=cortex-a57 -I/opt/devkitpro/libnx/include -I"$inc" -c "$src/deko_bindings.cpp" -o /workspace/outputs/deko-native/deko_bindings.o
"$cc" -std=c++20 -O2 -fPIE -D__SWITCH__ -march=armv8-a+crc+crypto -mtune=cortex-a57 -I/opt/devkitpro/libnx/include -I"$inc" -c "$src/deko_images.cpp" -o /workspace/outputs/deko-native/deko_images.o
"$cc" -std=c++20 -O2 -fPIE -D__SWITCH__ -march=armv8-a+crc+crypto -mtune=cortex-a57 -I/opt/devkitpro/libnx/include -I"$inc" -c "$src/deko_depth.cpp" -o /workspace/outputs/deko-native/deko_depth.o
"$cc" -std=c++20 -O2 -fPIE -D__SWITCH__ -march=armv8-a+crc+crypto -mtune=cortex-a57 -I/opt/devkitpro/libnx/include -I"$inc" -c "$src/deko_output.cpp" -o /workspace/outputs/deko-native/deko_output.o
"$cc" -std=c++23 -O2 -DNDEBUG -fPIE -D__SWITCH__ -D_POSIX_C_SOURCE=200809L -march=armv8-a+crc+crypto -mtune=cortex-a57 -I/opt/devkitpro/libnx/include -I"$inc" -c "$src/deko_textures.cpp" -o /workspace/outputs/deko-native/deko_textures.o
for part in conversion info util; do
 "$cc" -std=c++23 -O2 -DNDEBUG -fPIE -ffunction-sections -fdata-sections -D__SWITCH__ -D_POSIX_C_SOURCE=200809L -DSPDLOG_FMT_EXTERNAL -DSPDLOG_COMPILED_LIB -march=armv8-a+crc+crypto -mtune=cortex-a57 -I/opt/devkitpro/libnx/include -I"$inc" -I"$third/fmt/include" -I"$third/spdlog/include" -I"$third/xxhash" -c "$ref/src/graphics/pipeline/texture/$part.cpp" -o "/workspace/outputs/deko-native/texture_$part.o"
done
"$cc" -std=c++20 -O2 -fPIE -D__SWITCH__ -march=armv8-a+crc+crypto -mtune=cortex-a57 -I/opt/devkitpro/libnx/include -I"$inc" -c /workspace/work/deko3d-sdk-reference/src/ui/deko3d/nucleo.cpp -o /workspace/outputs/deko-native/nucleo.o
/opt/devkitpro/devkitA64/bin/aarch64-none-elf-ar rcs /workspace/outputs/deko-native/libgreen_lantern_deko_bindings.a /workspace/outputs/deko-native/deko_bindings.o /workspace/outputs/deko-native/deko_images.o /workspace/outputs/deko-native/deko_depth.o /workspace/outputs/deko-native/deko_output.o /workspace/outputs/deko-native/deko_textures.o /workspace/outputs/deko-native/texture_conversion.o /workspace/outputs/deko-native/texture_info.o /workspace/outputs/deko-native/texture_util.o /workspace/outputs/deko-native/nucleo.o
"$cc" -std=c++20 -O2 -fPIE -D__SWITCH__ -march=armv8-a+crc+crypto -mtune=cortex-a57 -I/opt/devkitpro/libnx/include -I"$inc" -specs=/opt/devkitpro/libnx/switch.specs "$src/link_check.cpp" /workspace/outputs/deko-native/libgreen_lantern_deko_bindings.a -L/opt/devkitpro/libnx/lib -ldeko3d -lnx -o /workspace/outputs/deko-native/link-check.elf
'@
& $Docker run --rm --mount "type=bind,source=$taskWorkspace,target=/workspace" $Image sh -c $taskCommand
exit $LASTEXITCODE
