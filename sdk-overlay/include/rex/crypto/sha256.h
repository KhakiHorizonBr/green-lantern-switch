#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace rex::crypto {

std::string sha256(std::string_view data);
std::string sha256_file(const std::filesystem::path& path);

}  // namespace rex::crypto
