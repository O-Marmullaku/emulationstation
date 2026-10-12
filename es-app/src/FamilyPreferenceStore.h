#pragma once

#include <atomic>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace FamilyPreferenceStore {
namespace fs = std::filesystem;

struct Value {
    std::string type;
    std::string value;
    bool operator==(const Value& other) const { return type == other.type && value == other.value; }
    bool operator!=(const Value& other) const { return !(*this == other); }
};

using Values = std::map<std::string, Value>;

constexpr std::size_t MaxFileBytes = 1024 * 1024;
constexpr std::size_t MaxEntries = 4096;
constexpr std::size_t MaxNameBytes = 512;
constexpr std::size_t MaxValueBytes = 64 * 1024;

namespace detail {
inline bool validUtf8(const std::string& s)
{
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c <= 0x7f) { ++i; continue; }
        std::size_t n;
        std::uint32_t cp;
        if (c >= 0xc2 && c <= 0xdf) { n = 2; cp = c & 0x1f; }
        else if (c >= 0xe0 && c <= 0xef) { n = 3; cp = c & 0x0f; }
        else if (c >= 0xf0 && c <= 0xf4) { n = 4; cp = c & 0x07; }
        else return false;
        if (i + n > s.size()) return false;
        for (std::size_t j = 1; j < n; ++j) {
            const unsigned char d = static_cast<unsigned char>(s[i + j]);
            if ((d & 0xc0) != 0x80) return false;
            if (j == 1 && ((c == 0xe0 && d < 0xa0) || (c == 0xed && d >= 0xa0) ||
                           (c == 0xf0 && d < 0x90) || (c == 0xf4 && d >= 0x90))) return false;
            cp = (cp << 6) | (d & 0x3f);
        }
        if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        i += n;
    }
    return true;
}

inline bool validInteger(const std::string& s)
{
    if (s.empty()) return false;
    std::size_t digit = (s[0] == '-') ? 1 : 0;
    if (digit == s.size()) return false;
    if (s[digit] == '0') {
        if (digit + 1 != s.size()) return false;
    } else {
        if (s[digit] < '1' || s[digit] > '9') return false;
        for (std::size_t i = digit + 1; i < s.size(); ++i)
            if (s[i] < '0' || s[i] > '9') return false;
    }
    std::int64_t value = 0;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), value, 10);
    return result.ec == std::errc() && result.ptr == s.data() + s.size();
}

inline bool validFloat(const std::string& s)
{
    if (s.empty()) return false;
    double value = 0.0;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), value, std::chars_format::general);
    return result.ec == std::errc() && result.ptr == s.data() + s.size() && std::isfinite(value);
}

inline void validate(const std::string& name, const Value& value)
{
    if (name.empty() || name.size() > MaxNameBytes || !validUtf8(name))
        throw std::runtime_error("Family preference name is invalid or exceeds 512 bytes.");
    if (value.value.size() > MaxValueBytes || !validUtf8(value.value))
        throw std::runtime_error("Family preference value is invalid or exceeds 64 KiB.");
    if (value.type == "bool") {
        if (value.value != "true" && value.value != "false") throw std::runtime_error("Invalid boolean family preference.");
    } else if (value.type == "int") {
        if (!validInteger(value.value)) throw std::runtime_error("Invalid integer family preference.");
    } else if (value.type == "float") {
        if (!validFloat(value.value)) throw std::runtime_error("Invalid float family preference.");
    } else if (value.type != "string") {
        throw std::runtime_error("Unknown family preference type.");
    }
}

inline void rejectReparseFileTarget(const fs::path& path)
{
    std::error_code ec;
    const fs::file_status status = fs::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory) return;
    if (ec) throw fs::filesystem_error("Could not inspect family preference file", path, ec);
    if (fs::is_symlink(status)) throw std::runtime_error("Family preference file cannot be a symlink or reparse point.");
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Family preference file cannot be a symlink or reparse point.");
#endif
}

inline std::string serialize(const Values& values)
{
    if (values.size() > MaxEntries) throw std::runtime_error("Too many family preferences.");
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    for (const auto& entry : values) {
        validate(entry.first, entry.second);
        writer.Key(entry.first.data(), static_cast<rapidjson::SizeType>(entry.first.size()));
        writer.StartObject();
        writer.Key("type");
        writer.String(entry.second.type.data(), static_cast<rapidjson::SizeType>(entry.second.type.size()));
        writer.Key("value");
        writer.String(entry.second.value.data(), static_cast<rapidjson::SizeType>(entry.second.value.size()));
        writer.EndObject();
        if (buffer.GetSize() > MaxFileBytes) throw std::runtime_error("Family preferences exceed 1 MiB.");
    }
    writer.EndObject();
    if (!writer.IsComplete()) throw std::runtime_error("Could not serialize family preferences.");
    if (buffer.GetSize() > MaxFileBytes) throw std::runtime_error("Family preferences exceed 1 MiB.");
    return std::string(buffer.GetString(), buffer.GetSize());
}

inline fs::path makeTempPath(const fs::path& target, std::uint64_t attempt)
{
    static std::atomic<std::uint64_t> sequence{0};
#ifdef _WIN32
    const auto pid = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    const auto pid = static_cast<std::uint64_t>(::getpid());
#endif
    fs::path temp = target;
    temp += ".tmp." + std::to_string(pid) + "." + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed) + attempt);
    return temp;
}

inline void writeTempAndReplace(const fs::path& target, const std::string& bytes)
{
    fs::path temp;
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    for (std::uint64_t i = 0; i < 128; ++i) {
        temp = makeTempPath(target, i);
        file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Could not create family preference temp file");
    }
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not allocate a unique family preference temp file.");
    bool committed = false;
    try {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, std::numeric_limits<DWORD>::max()));
            DWORD written = 0;
            if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) || written == 0)
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Could not write family preference temp file");
            offset += written;
        }
        if (!FlushFileBuffers(file)) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Could not flush family preference temp file");
        if (!CloseHandle(file)) { file = INVALID_HANDLE_VALUE; throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Could not close family preference temp file"); }
        file = INVALID_HANDLE_VALUE;
        if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Could not replace family preference file");
        committed = true;
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        if (!committed) DeleteFileW(temp.c_str());
        throw;
    }
#else
    int fd = -1;
    for (std::uint64_t i = 0; i < 128; ++i) {
        temp = makeTempPath(target, i);
        fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL
#ifdef O_CLOEXEC
                    | O_CLOEXEC
#endif
                    , S_IRUSR | S_IWUSR);
        if (fd >= 0) break;
        if (errno != EEXIST) throw std::system_error(errno, std::generic_category(), "Could not create family preference temp file");
    }
    if (fd < 0) throw std::runtime_error("Could not allocate a unique family preference temp file.");
    bool committed = false;
    try {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const ssize_t written = ::write(fd, bytes.data() + offset, bytes.size() - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) throw std::system_error(errno, std::generic_category(), "Could not write family preference temp file");
            offset += static_cast<std::size_t>(written);
        }
        if (::fsync(fd) != 0) throw std::system_error(errno, std::generic_category(), "Could not flush family preference temp file");
        if (::close(fd) != 0) { fd = -1; throw std::system_error(errno, std::generic_category(), "Could not close family preference temp file"); }
        fd = -1;
        if (::rename(temp.c_str(), target.c_str()) != 0) throw std::system_error(errno, std::generic_category(), "Could not replace family preference file");
        committed = true;
    } catch (...) {
        if (fd >= 0) ::close(fd);
        if (!committed) ::unlink(temp.c_str());
        throw;
    }
#endif
}
} // namespace detail

inline Values read(const fs::path& path)
{
    detail::rejectReparseFileTarget(path);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::error_code ec;
        const auto status = fs::symlink_status(path, ec);
        if ((!ec && status.type() == fs::file_type::not_found) || ec == std::errc::no_such_file_or_directory)
            return {};
        throw fs::filesystem_error("Could not open family preference file", path, std::make_error_code(std::errc::io_error));
    }
    std::string json;
    char buffer[8192];
    while (input) {
        input.read(buffer, sizeof(buffer));
        const auto count = static_cast<std::size_t>(input.gcount());
        if (json.size() + count > MaxFileBytes) throw std::runtime_error("Family preferences exceed 1 MiB.");
        json.append(buffer, count);
    }
    if (input.bad()) throw fs::filesystem_error("Could not read family preference file", path, std::make_error_code(std::errc::io_error));

    rapidjson::Document document;
    document.Parse(json.data(), json.size());
    if (document.HasParseError() || !document.IsObject()) throw std::runtime_error("Family preference file is invalid JSON; expected an object.");
    if (document.MemberCount() > MaxEntries) throw std::runtime_error("Too many family preferences.");
    Values result;
    for (auto it = document.MemberBegin(); it != document.MemberEnd(); ++it) {
        if (!it->name.IsString() || !it->value.IsObject() || it->value.MemberCount() != 2 ||
            !it->value.HasMember("type") || !it->value["type"].IsString() ||
            !it->value.HasMember("value") || !it->value["value"].IsString())
            throw std::runtime_error("Invalid family preference entry.");
        const std::string name(it->name.GetString(), it->name.GetStringLength());
        const auto& type = it->value["type"];
        const auto& value = it->value["value"];
        Value parsed{std::string(type.GetString(), type.GetStringLength()), std::string(value.GetString(), value.GetStringLength())};
        detail::validate(name, parsed);
        if (!result.emplace(name, std::move(parsed)).second) throw std::runtime_error("Duplicate family preference name.");
    }
    return result;
}

inline void write(const fs::path& path, const Values& values)
{
    const std::string json = detail::serialize(values);
    detail::rejectReparseFileTarget(path);
    detail::writeTempAndReplace(path, json);
}
} // namespace FamilyPreferenceStore
