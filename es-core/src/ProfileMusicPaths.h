#pragma once
#include <filesystem>
#include <string>
#include <utility>

namespace ProfileMusicPaths {
inline bool safeRelative(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute()) return false;
    for (const auto& part : path) if (part == "..") return false;
    return true;
}
inline std::string encode(const std::string& path, const std::string& user, const std::string& shared, const std::string& system)
{
	for (const auto& item : {std::make_pair("@user/", user), std::make_pair("@shared/", shared), std::make_pair("@system/", system)}) {
        if (item.second.empty()) continue;
        auto local = std::filesystem::u8path(path).lexically_relative(std::filesystem::u8path(item.second));
        if (safeRelative(local)) return item.first + local.generic_u8string();
    }
    return path;
}
inline std::string decode(const std::string& path, const std::string& user, const std::string& shared, const std::string& system)
{
	for (const auto& item : {std::make_pair("@user/", user), std::make_pair("@shared/", shared), std::make_pair("@system/", system)}) {
        const std::string prefix = item.first;
        if (path.compare(0, prefix.size(), prefix) != 0 || item.second.empty()) continue;
        auto local = std::filesystem::u8path(path.substr(prefix.size()));
        if (safeRelative(local)) return (std::filesystem::u8path(item.second) / local).generic_u8string();
    }
    return path;
}
}
