#pragma once
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace FamilyProfiles {
struct Profile { std::string id; std::string root; };
inline std::string& lastSessionId() { static std::string value; return value; }
inline bool& lastSessionWasPreferences() { static bool value = false; return value; }
inline std::string environment(const char* name)
{
#ifdef _WIN32
    char value[4096];
    const DWORD size = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    return size == 0 || size >= sizeof(value) ? std::string() : std::string(value, size);
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}
inline bool enabled() { return !environment("RETROBAT_FAMILY_ROOT").empty(); }
inline std::string currentId() { return environment("RETROBAT_PROFILE_ID"); }
inline Profile require(const std::string& id)
{
    if (id != "A" && id != "B") throw std::runtime_error("unknown test profile");
    const auto root = environment("RETROBAT_FAMILY_ROOT");
    return {id, (std::filesystem::u8path(root) / id).u8string()};
}
class Session {
public:
    explicit Session(const std::string& id = {}, bool preferences = false)
    {
        lastSessionId() = id;
        lastSessionWasPreferences() = preferences;
        std::filesystem::create_directories(std::filesystem::u8path(require(id).root));
    }
};
}
