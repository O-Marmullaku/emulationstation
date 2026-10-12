#pragma once
#include <cstdlib>
#include <string>
#ifdef _WIN32
#include <Windows.h>
#endif
class Paths {
public:
    static std::string& getUserKeyboardMappingsPath() { static std::string value; return value; }
    static std::string& getUserMusicPath() { static std::string value; return value; }
    static std::string& getMusicPath() { static std::string value; return value; }
    static std::string& getProfileConfigPath() { static std::string value; return value; }
    static std::string getUserEmulationStationPath()
    {
#ifdef _WIN32
        char value[4096];
        const DWORD size = GetEnvironmentVariableA("RETROBAT_TEST_HOME", value, static_cast<DWORD>(sizeof(value)));
        return size == 0 || size >= sizeof(value) ? std::string() : std::string(value, size);
#else
        const char* value = std::getenv("RETROBAT_TEST_HOME");
        return value ? value : "";
#endif
    }
};
