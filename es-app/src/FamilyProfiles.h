#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>
#include <set>
#include <stdexcept>
#include <system_error>
#include <string>
#include <vector>

#include <rapidjson/document.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace FamilyProfiles {
namespace detail {
namespace fs = std::filesystem;

inline std::string utf8FromWide(const std::wstring& value)
{
#ifdef _WIN32
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("Could not convert a Windows path to UTF-8.");
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), &result[0], size, nullptr, nullptr) != size)
        throw std::runtime_error("Could not convert a Windows path to UTF-8.");
    return result;
#else
    return std::string(value.begin(), value.end());
#endif
}

inline std::string environment(const char* name)
{
#ifdef _WIN32
    const int wideNameSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, nullptr, 0);
    if (wideNameSize <= 0) throw std::runtime_error("Invalid environment variable name.");
    std::wstring wideName(static_cast<std::size_t>(wideNameSize), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, &wideName[0], wideNameSize) != wideNameSize)
        throw std::runtime_error("Invalid environment variable name.");
    const DWORD needed = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (needed == 0) return {};
    std::wstring value(static_cast<std::size_t>(needed), L'\0');
    const DWORD copied = GetEnvironmentVariableW(wideName.c_str(), &value[0], needed);
    if (copied == 0 || copied >= needed) throw std::runtime_error(std::string("Could not read ") + name + ".");
    value.resize(copied);
    return utf8FromWide(value);
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

inline fs::path familyRootPath()
{
    const std::string root = environment("RETROBAT_FAMILY_ROOT");
    if (root.empty()) throw std::runtime_error("Family profile mode is enabled but RETROBAT_FAMILY_ROOT is empty.");
#ifdef _WIN32
    const fs::path path = fs::u8path(root);
#else
    const fs::path path = fs::u8path(root);
#endif
    if (!path.is_absolute()) throw std::runtime_error("RETROBAT_FAMILY_ROOT must be an absolute path.");
    return path.lexically_normal();
}

inline std::string pathUtf8(const fs::path& path)
{
#if defined(__cpp_lib_char8_t)
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
#else
    return path.u8string();
#endif
}

inline void validateId(const std::string& id)
{
    if (id.empty() || id.size() > 40 || !std::isalnum(static_cast<unsigned char>(id[0])))
        throw std::runtime_error("Invalid family profile ID.");
    for (unsigned char ch : id) {
        if (!(std::isalnum(ch) && ch < 128) && ch != '_' && ch != '-')
            throw std::runtime_error("Invalid family profile ID.");
    }
}

inline std::string foldId(const std::string& id)
{
    std::string folded = id;
    std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return folded;
}

inline void validateName(const std::string& name)
{
    if (name.empty()) throw std::runtime_error("Family profile name cannot be empty.");
    std::size_t codeUnits = 0;
    for (std::size_t i = 0; i < name.size();) {
        const unsigned char first = static_cast<unsigned char>(name[i]);
        std::uint32_t codepoint = 0;
        std::size_t count = 0;
        if (first <= 0x7F) { codepoint = first; count = 1; }
        else if (first >= 0xC2 && first <= 0xDF) { codepoint = first & 0x1F; count = 2; }
        else if (first >= 0xE0 && first <= 0xEF) { codepoint = first & 0x0F; count = 3; }
        else if (first >= 0xF0 && first <= 0xF4) { codepoint = first & 0x07; count = 4; }
        else throw std::runtime_error("Family profile name is not valid UTF-8.");
        if (i + count > name.size()) throw std::runtime_error("Family profile name is not valid UTF-8.");
        for (std::size_t j = 1; j < count; ++j) {
            const unsigned char continuation = static_cast<unsigned char>(name[i + j]);
            if ((continuation & 0xC0) != 0x80) throw std::runtime_error("Family profile name is not valid UTF-8.");
            if (j == 1 && ((first == 0xE0 && continuation < 0xA0) || (first == 0xED && continuation >= 0xA0) ||
                (first == 0xF0 && continuation < 0x90) || (first == 0xF4 && continuation > 0x8F)))
                throw std::runtime_error("Family profile name is not valid UTF-8.");
            codepoint = (codepoint << 6) | (continuation & 0x3F);
        }
        if (codepoint <= 0x1F || (codepoint >= 0x7F && codepoint <= 0x9F))
            throw std::runtime_error("Family profile names cannot contain control characters.");
        codeUnits += codepoint > 0xFFFF ? 2 : 1;
        if (codeUnits > 40) throw std::runtime_error("Family profile name must be at most 40 characters.");
        i += count;
    }
}

inline std::string stringField(const rapidjson::Value& value, const char* key)
{
    if (!value.IsObject() || !value.HasMember(key) || !value[key].IsString())
        throw std::runtime_error(std::string("Invalid family profile registry field: ") + key);
    return std::string(value[key].GetString(), value[key].GetStringLength());
}

inline unsigned colorField(const std::string& color)
{
    if (color.size() != 7 || color[0] != '#') throw std::runtime_error("Invalid family profile accent color.");
    std::uint32_t rgb = 0;
    for (std::size_t i = 1; i < color.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(color[i]);
        unsigned digit;
        if (ch >= '0' && ch <= '9') digit = ch - '0';
        else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
        else throw std::runtime_error("Invalid family profile accent color.");
        rgb = (rgb << 4) | digit;
    }
    return static_cast<unsigned>((rgb << 8) | 0xFFu);
}
} // namespace detail

struct Profile {
    std::string id;
    std::string name;
    std::string root;
    unsigned color = 0;
};

inline bool enabled()
{
    return !detail::environment("RETROBAT_FAMILY_ROOT").empty();
}

inline std::string currentId()
{
    const std::string id = detail::environment("RETROBAT_PROFILE_ID");
    if (!id.empty()) detail::validateId(id);
    return id;
}

inline std::vector<Profile> load()
{
    if (!enabled()) return {};
    const auto familyRoot = detail::familyRootPath();
    const auto registryPath = familyRoot / "profiles.json";
    std::ifstream input(registryPath, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open family profile registry: " + detail::pathUtf8(registryPath));
constexpr std::size_t maxRegistryBytes = 1024 * 1024;
    std::string json;
    char chunk[8192];
    while (input) {
        input.read(chunk, sizeof(chunk));
        const auto count = static_cast<std::size_t>(input.gcount());
        if (json.size() + count > maxRegistryBytes) throw std::runtime_error("Family profile registry exceeds 1 MiB.");
        json.append(chunk, count);
    }
    if (input.bad()) throw std::runtime_error("Could not read family profile registry: " + detail::pathUtf8(registryPath));
    if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xEF && static_cast<unsigned char>(json[1]) == 0xBB && static_cast<unsigned char>(json[2]) == 0xBF)
        json.erase(0, 3);
    rapidjson::Document document;
    document.Parse(json.data(), json.size());
    if (document.HasParseError() || !document.IsArray()) throw std::runtime_error("Family profile registry is invalid JSON; expected an array.");

    std::vector<Profile> profiles;
    std::set<std::string> ids;
    profiles.reserve(document.Size());
    for (const auto& item : document.GetArray()) {
        Profile profile;
        profile.id = detail::stringField(item, "ClientId");
        detail::validateId(profile.id);
        if (!ids.insert(detail::foldId(profile.id)).second) throw std::runtime_error("Duplicate family profile ID: " + profile.id);
        profile.name = detail::stringField(item, "DisplayName");
        detail::validateName(profile.name);
        profile.color = detail::colorField(detail::stringField(item, "AccentColor"));
        profile.root = detail::pathUtf8(familyRoot / detail::fs::u8path(profile.id));
        profiles.push_back(std::move(profile));
    }
    return profiles;
}

inline Profile require(const std::string& requestedId)
{
    if (!enabled()) throw std::runtime_error("Family profile mode is disabled.");
    const std::string id = requestedId.empty() ? currentId() : requestedId;
    detail::validateId(id);
    const auto profiles = load();
    const auto found = std::find_if(profiles.begin(), profiles.end(), [&id](const Profile& p) { return detail::foldId(p.id) == detail::foldId(id); });
    if (found == profiles.end()) throw std::runtime_error("Family profile is not present in profiles.json: " + id);
    return *found;
}

class Session {
public:
    explicit Session(const std::string& requestedId = std::string(), bool preferences = false)
    {
        if (!enabled()) return;
        const Profile profile = require(requestedId);
        const auto profilePath = detail::fs::u8path(profile.root);
        std::error_code directoryError;
        detail::fs::create_directories(profilePath, directoryError);
        if (directoryError) throw std::runtime_error("Could not create the family profile save folder: " + directoryError.message());
        const auto lockPath = profilePath / (preferences ? ".retrobat-preferences.lock" : ".retrobat-session.lock");
#ifdef _WIN32
        handle_ = CreateFileW(lockPath.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            handle_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("Could not acquire the exclusive family profile lock (Windows error " + std::to_string(error) + ").");
        }
#else
        fd_ = ::open(lockPath.string().c_str(), O_CREAT | O_RDWR, 0600);
        if (fd_ < 0 || ::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
            throw std::runtime_error("Could not acquire the exclusive family profile lock.");
        }
#endif
    }

    ~Session() { release(); }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&& other) noexcept
    {
#ifdef _WIN32
        handle_ = other.handle_;
        other.handle_ = INVALID_HANDLE_VALUE;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
    }
    Session& operator=(Session&& other) noexcept
    {
        if (this != &other) {
            release();
#ifdef _WIN32
            handle_ = other.handle_;
            other.handle_ = INVALID_HANDLE_VALUE;
#else
            fd_ = other.fd_;
            other.fd_ = -1;
#endif
        }
        return *this;
    }

private:
    void release() noexcept
    {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) { CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
#else
        if (fd_ >= 0) { ::flock(fd_, LOCK_UN); ::close(fd_); fd_ = -1; }
#endif
    }
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};
} // namespace FamilyProfiles





