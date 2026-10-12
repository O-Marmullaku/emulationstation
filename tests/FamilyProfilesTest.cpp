#include "FamilyProfiles.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace fs = std::filesystem;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "CHECK failed at line " << __LINE__ << ": " << #condition << std::endl; std::exit(1); } } while (false)

static std::string utf8(const fs::path& path)
{
#if defined(__cpp_lib_char8_t)
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
#else
    return path.u8string();
#endif
}

static void setEnv(const char* name, const std::string& value)
{
#ifdef _WIN32
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
    CHECK(count > 0);
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, &wide[0], count) == count);
    const int nameCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, nullptr, 0);
    CHECK(nameCount > 0);
    std::wstring wideName(static_cast<std::size_t>(nameCount), L'\0');
    CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, &wideName[0], nameCount) == nameCount);
    CHECK(SetEnvironmentVariableW(wideName.c_str(), value.empty() ? nullptr : wide.c_str()));
#else
    if (value.empty()) unsetenv(name);
    else CHECK(setenv(name, value.c_str(), 1) == 0);
#endif
}

static void write(const fs::path& path, const std::string& text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    CHECK(stream.good());
    stream << text;
    CHECK(stream.good());
}

template<class F> static void mustThrow(F action)
{
    bool threw = false;
    try { action(); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
}

int main()
{
    try {
        using namespace FamilyProfiles;
        setEnv("RETROBAT_FAMILY_ROOT", "");
        setEnv("RETROBAT_PROFILE_ID", "");
        CHECK(!enabled());
        CHECK(load().empty());
        { Session disabled; }

        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path root = fs::current_path() / fs::u8path(u8"Family Profiles test \u00E9-") / std::to_string(nonce);
        fs::create_directories(root);
        const fs::path registry = root / "profiles.json";
        write(registry, R"([{"DisplayName":"Osman","ClientId":"OSMAN","AccentColor":"#5588CC","ComputerName":"OSMAN"},{"DisplayName":"New profile","ClientId":"profile-0123456789abcdef0123456789abcdef","AccentColor":"#A1b2C3"}])");

        setEnv("RETROBAT_FAMILY_ROOT", utf8(root));
        setEnv("RETROBAT_PROFILE_ID", "OSMAN");
        CHECK(enabled());
        const auto profiles = load();
        CHECK(profiles.size() == 2);
        CHECK(profiles[0].id == "OSMAN");
        CHECK(profiles[0].name == "Osman");
        CHECK(profiles[0].color == 0x5588CCFFu);
        CHECK(fs::u8path(profiles[0].root) == root / "OSMAN");
        CHECK(require("").id == "OSMAN");
        CHECK(require("PROFILE-0123456789ABCDEF0123456789ABCDEF").id == "profile-0123456789abcdef0123456789abcdef");
        mustThrow([] { require("../bad"); });
        mustThrow([] { require("missing"); });
        setEnv("RETROBAT_PROFILE_ID", "../bad");
        mustThrow([] { currentId(); });
        setEnv("RETROBAT_PROFILE_ID", "OSMAN");

        {
            Session held("OSMAN");
            mustThrow([] { Session second("OSMAN"); });
        }
        { Session released("OSMAN"); }

        write(registry, R"([{"DisplayName":"a","ClientId":"OSMAN","AccentColor":"#000000"},{"DisplayName":"b","ClientId":"osman","AccentColor":"#FFFFFF"}])");
        mustThrow([] { load(); });
        write(registry, "not json");
        mustThrow([] { load(); });
        write(registry, R"([{"DisplayName":"bad id","ClientId":"../escape","AccentColor":"#000000"}])");
        mustThrow([] { load(); });
        write(registry, R"([{"DisplayName":"bad color","ClientId":"OK","AccentColor":"red"}])");
        mustThrow([] { load(); });
        write(registry, R"([{"DisplayName":"12345678901234567890123456789012345678901","ClientId":"OK","AccentColor":"#000000"}])");
        mustThrow([] { load(); });
        write(registry, R"([{"DisplayName":"bad\nname","ClientId":"OK","AccentColor":"#000000"}])");
        mustThrow([] { load(); });
        write(registry, std::string(1024 * 1024 + 1, ' '));
        mustThrow([] { load(); });

        setEnv("RETROBAT_FAMILY_ROOT", "relative-root");
        mustThrow([] { load(); });
        setEnv("RETROBAT_FAMILY_ROOT", "");
        setEnv("RETROBAT_PROFILE_ID", "");
        fs::remove_all(root);
        std::cout << "PASS: family profile environment, UTF-8 root, registry parsing, ID/name validation, color conversion, profile resolution, fail-closed errors, size limit, and exclusive session lock.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "UNEXPECTED EXCEPTION: " << ex.what() << std::endl;
        return 2;
    }
}

