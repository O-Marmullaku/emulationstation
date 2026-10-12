#pragma once

#include "ProfileMusicPaths.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace FamilyProfileFiles {
namespace fs = std::filesystem;
namespace detail {
constexpr std::uintmax_t maxFileBytes = 16u * 1024u * 1024u;
constexpr std::uintmax_t maxTotalBytes = 64u * 1024u * 1024u;

inline fs::file_status inspectPath(const fs::path& path)
{
    if (path.empty()) return fs::file_status(fs::file_type::not_found);
    std::error_code ec;
    const auto result = fs::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory)
        return fs::file_status(fs::file_type::not_found);
    if (ec) throw fs::filesystem_error("Could not inspect family profile file", path, ec);
    if (fs::is_symlink(result)) throw std::runtime_error("Family profile files cannot be symlinks or reparse points.");
#ifdef _WIN32
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Family profile files cannot be symlinks or reparse points.");
#endif
    return result;
}

inline void requireDirectory(const fs::path& path)
{
    if (path.empty()) return;
    const auto st = inspectPath(path);
    if (st.type() != fs::file_type::not_found && !fs::is_directory(st))
        throw std::runtime_error("Expected a family profile directory: " + path.string());
}

inline bool regularFile(const fs::path& path)
{
    const auto st = inspectPath(path);
    if (st.type() == fs::file_type::not_found) return false;
    if (!fs::is_regular_file(st)) throw std::runtime_error("Expected a regular family profile file: " + path.string());
    return true;
}

inline void accountFile(const fs::path& path, std::uintmax_t& total)
{
    if (!regularFile(path)) return;
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) throw fs::filesystem_error("Could not size family profile file", path, ec);
    if (size > maxFileBytes || total > maxTotalBytes - size)
        throw std::runtime_error("Family profile configuration exceeds its copy size limit.");
    total += size;
}

inline void copyFile(const fs::path& source, const fs::path& destination, std::uintmax_t& total)
{
    if (source.empty()) return;
    if (!regularFile(source)) return;
    accountFile(source, total);
    std::error_code ec;
    if (!fs::copy_file(source, destination, fs::copy_options::none, ec)) {
        if (ec) throw fs::filesystem_error("Could not seed family profile file", source, destination, ec);
        throw std::runtime_error("Could not seed family profile file: " + source.string());
    }
    if (fs::file_size(source) != fs::file_size(destination))
        throw std::runtime_error("Family profile file changed while being copied: " + source.string());
}

inline fs::path makeStage(const fs::path& parent, const char* leaf);
inline void removeStage(const fs::path& stage, const fs::path& parent, const char* leaf) noexcept;

inline std::string normalizeFavorites(const std::string& content, const fs::path& profileMusic,
    const fs::path& sharedMusic, const fs::path& systemMusic)
{
    const std::string profile = profileMusic.generic_u8string();
    const std::string shared = sharedMusic.generic_u8string();
    const std::string system = systemMusic.generic_u8string();
    std::string result;
    result.reserve(content.size());
    for (std::size_t start = 0; start < content.size();) {
        const std::size_t end = content.find('\n', start);
        const std::size_t lineEnd = end == std::string::npos ? content.size() : end;
        std::string line = content.substr(start, lineEnd - start);
        const bool carriageReturn = !line.empty() && line.back() == '\r';
        if (carriageReturn) line.pop_back();
        const auto separator = line.find(';');
        if (separator != std::string::npos) {
            const std::string path = line.substr(0, separator);
            if (fs::u8path(path).is_absolute())
                line.replace(0, separator, ProfileMusicPaths::encode(path, profile, shared, system));
        }
        result += line;
        if (carriageReturn) result += '\r';
        if (end != std::string::npos) result += '\n';
        start = end == std::string::npos ? content.size() : end + 1;
    }
    return result;
}

inline void normalizeFavoritesFile(const fs::path& file, const fs::path& profileMusic,
    const fs::path& sharedMusic, const fs::path& systemMusic)
{
    if (!regularFile(file)) return;
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) throw fs::filesystem_error("Could not size favorites playlist", file, ec);
    if (size > maxFileBytes) throw std::runtime_error("Favorites playlist exceeds its copy size limit.");
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Could not read favorites playlist: " + file.string());
    const std::string original((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) throw std::runtime_error("Could not read favorites playlist: " + file.string());
    input.close();
    const std::string normalized = normalizeFavorites(original, profileMusic, sharedMusic, systemMusic);
    if (normalized.size() > maxFileBytes) throw std::runtime_error("Normalized favorites playlist exceeds its size limit.");
    if (normalized == original) return;

    const fs::path parent = file.parent_path();
    const std::string filename = file.filename().string();
    fs::path stage = makeStage(parent, filename.c_str());
    try {
        const fs::path temporary = stage / file.filename();
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(normalized.data(), static_cast<std::streamsize>(normalized.size()));
        output.flush();
        if (!output) throw std::runtime_error("Could not write normalized favorites playlist: " + temporary.string());
        output.close();
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not replace favorites playlist (Windows error " + std::to_string(GetLastError()) + ").");
#else
        fs::rename(temporary, file, ec);
        if (ec) throw fs::filesystem_error("Could not replace favorites playlist", temporary, file, ec);
#endif
        removeStage(stage, parent, filename.c_str());
    } catch (...) {
        removeStage(stage, parent, filename.c_str());
        throw;
    }
}

inline bool directFilename(const fs::path& entry, const char* extension)
{
    const auto name = entry.filename();
    const auto ext = name.extension();
    return !name.empty() && name != "." && name != ".." && name.native().size() <= 255 && ext == fs::path(extension);
}

inline std::vector<fs::path> filesWithExtension(const fs::path& directory, const char* extension)
{
    std::vector<fs::path> files;
    if (directory.empty()) return files;
    requireDirectory(directory);
    if (!fs::is_directory(inspectPath(directory))) return files;
    for (const auto& entry : fs::directory_iterator(directory)) {
        const auto path = entry.path();
        if (!directFilename(path, extension)) continue;
        if (!regularFile(path)) throw std::runtime_error("Expected a regular family profile file: " + path.string());
        files.push_back(path);
    }
    return files;
}

inline fs::path makeStage(const fs::path& parent, const char* leaf)
{
    static std::atomic<std::uint64_t> sequence{0};
#ifdef _WIN32
    const auto pid = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    const auto pid = static_cast<std::uint64_t>(::getpid());
#endif
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        const auto id = sequence.fetch_add(1, std::memory_order_relaxed);
        fs::path candidate = parent / (std::string(".") + leaf + ".stage." + std::to_string(pid) + "." + std::to_string(id));
        std::error_code ec;
        if (fs::create_directory(candidate, ec)) return candidate;
        if (ec != std::errc::file_exists)
            throw fs::filesystem_error("Could not create family profile staging directory", candidate, ec);
    }
    throw std::runtime_error("Could not allocate a family profile staging directory.");
}

inline void removeStage(const fs::path& stage, const fs::path& parent, const char* leaf) noexcept
{
    try {
        if (stage.parent_path().lexically_normal() != parent.lexically_normal()) return;
        const std::string prefix = std::string(".") + leaf + ".stage.";
        if (stage.filename().string().compare(0, prefix.size(), prefix) != 0) return;
        std::error_code ec;
        if (!fs::is_symlink(fs::symlink_status(stage, ec)) && !ec) fs::remove_all(stage, ec);
    } catch (...) {}
}

inline bool prepareDirectory(const fs::path& profileRoot, const fs::path& sourceEs,
    const fs::path& sourcePad, const fs::path& sourceMusic, const fs::path& sharedMusic,
    const fs::path& systemMusic, const char* leaf)
{
    const fs::path destination = profileRoot / leaf;
    requireDirectory(profileRoot);
    if (!fs::is_directory(inspectPath(profileRoot))) fs::create_directories(profileRoot);
    const auto existing = inspectPath(destination);
    if (existing.type() != fs::file_type::not_found) {
        if (!fs::is_directory(existing)) throw std::runtime_error("Expected a family profile directory: " + destination.string());
        return false;
    }

    fs::path stage = makeStage(profileRoot, leaf);
    std::uintmax_t total = 0;
    try {
        if (std::string(leaf) == "config") {
            const char* fixed[] = {"es_input.cfg", "es_last_input.cfg", "es_padtokey.cfg"};
            for (const char* name : fixed) copyFile(sourceEs / name, stage / name, total);
            const fs::path themeSource = sourceEs / "themesettings";
            for (const auto& file : filesWithExtension(themeSource, ".cfg")) {
                if (file.filename() == "." || file.filename() == "..") throw std::runtime_error("Invalid theme settings filename.");
                const fs::path targetDir = stage / "themesettings";
                fs::create_directories(targetDir);
                copyFile(file, targetDir / file.filename(), total);
            }
            for (const auto& file : filesWithExtension(sourcePad, ".keys")) {
                const fs::path targetDir = stage / "padtokey";
                fs::create_directories(targetDir);
                copyFile(file, targetDir / file.filename(), total);
            }
        } else {
            copyFile(sourceMusic.empty() ? fs::path() : sourceMusic / "favorites.m3u", stage / "favorites.m3u", total);
            normalizeFavoritesFile(stage / "favorites.m3u", profileRoot / "music", sharedMusic, systemMusic);
        }

        std::error_code ec;
        fs::rename(stage, destination, ec);
        if (ec) {
            const auto raced = inspectPath(destination);
            if (raced.type() != fs::file_type::not_found && fs::is_directory(raced)) {
                removeStage(stage, profileRoot, leaf);
                return false;
            }
            throw fs::filesystem_error("Could not promote family profile directory", stage, destination, ec);
        }
        return true;
    } catch (...) {
        removeStage(stage, profileRoot, leaf);
        throw;
    }
}

inline void validateExistingConfig(const fs::path& config)
{
    std::uintmax_t total = 0;
    for (const char* name : {"es_input.cfg", "es_last_input.cfg", "es_padtokey.cfg"}) accountFile(config / name, total);
    for (const auto& pair : {std::pair<fs::path, const char*>{config / "themesettings", ".cfg"},
                             std::pair<fs::path, const char*>{config / "padtokey", ".keys"}})
        for (const auto& file : filesWithExtension(pair.first, pair.second)) accountFile(file, total);
}
} // namespace detail

// Call while holding the profile's exclusive session lock; seeding is deliberately one-shot.
inline void prepare(const fs::path& profileRoot, const fs::path& baseEsHome,
    const fs::path& basePadtokey, const fs::path& baseFavoritesMusic,
    const fs::path& sharedMusic, const fs::path& systemMusic)
{
    if (baseEsHome.empty()) throw std::runtime_error("The base EmulationStation config path is empty.");
    detail::requireDirectory(baseEsHome);
    detail::requireDirectory(basePadtokey);
    detail::requireDirectory(baseFavoritesMusic);
    detail::requireDirectory(sharedMusic);
    detail::requireDirectory(systemMusic);
    detail::requireDirectory(profileRoot);
    if (!fs::is_directory(detail::inspectPath(profileRoot))) fs::create_directories(profileRoot);

    const fs::path config = profileRoot / "config";
    const auto configStatus = detail::inspectPath(config);
    if (configStatus.type() == fs::file_type::not_found) {
        detail::prepareDirectory(profileRoot, baseEsHome, basePadtokey, baseFavoritesMusic, sharedMusic, systemMusic, "config");
    } else {
        if (!fs::is_directory(configStatus)) throw std::runtime_error("Expected a family profile config directory: " + config.string());
        detail::validateExistingConfig(config);
    }

    const fs::path music = profileRoot / "music";
    const auto musicStatus = detail::inspectPath(music);
    if (musicStatus.type() == fs::file_type::not_found) {
        detail::prepareDirectory(profileRoot, baseEsHome, basePadtokey, baseFavoritesMusic, sharedMusic, systemMusic, "music");
    } else {
        if (!fs::is_directory(musicStatus)) throw std::runtime_error("Expected a family profile music directory: " + music.string());
        std::uintmax_t total = 0;
        detail::accountFile(music / "favorites.m3u", total);
        detail::normalizeFavoritesFile(music / "favorites.m3u", music, sharedMusic, systemMusic);
    }
}
} // namespace FamilyProfileFiles
