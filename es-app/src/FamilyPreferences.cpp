#include "FamilyPreferences.h"

#include "FamilyPreferenceStore.h"
#include "FamilyProfileFiles.h"
#include "FamilyProfiles.h"
#include "Settings.h"
#include "SystemData.h"
#include "Paths.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <set>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace FamilyPreferences {
namespace {
namespace fs = std::filesystem;
using StoreValues = FamilyPreferenceStore::Values;
using StoreValue = FamilyPreferenceStore::Value;

bool s_initialized = false;
bool s_restartRequested = false;
std::string s_activeId;
StoreValues s_baseline;
StoreValues s_applied;
std::string s_basePadtokey, s_baseMusic, s_baseFavoritesMusic;

bool allowedKey(const std::string& name)
{
    return Settings::isPersistentSetting(name);
}

bool encodeSetting(Settings* settings, const std::string& name, StoreValue& result)
{
    switch (settings->getSettingType(name)) {
    case SettingType::Bool:
        result = {"bool", settings->getBool(name) ? "true" : "false"};
        return true;
    case SettingType::Int:
        result = {"int", std::to_string(settings->getInt(name))};
        return true;
    case SettingType::Float: {
        char buffer[64];
        const float value = settings->getFloat(name);
        if (!std::isfinite(value)) return false;
        const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general,
            std::numeric_limits<float>::max_digits10);
        if (converted.ec != std::errc()) return false;
        result = {"float", std::string(buffer, converted.ptr)};
        return true;
    }
    case SettingType::String:
        result = {"string", settings->getString(name)};
        return true;
    default:
        return false;
    }
}

StoreValues captureCurrent()
{
    StoreValues values;
    Settings* settings = Settings::getInstance();
    for (const std::string& name : settings->getSettingsNames()) {
        if (!allowedKey(name)) continue;
        StoreValue value;
        if (encodeSetting(settings, name, value)) values.emplace(name, std::move(value));
    }
    return values;
}

bool applyValue(Settings* settings, const std::string& name, const StoreValue& value)
{
    auto wrongType = [&]() {
        settings->resetSetting(name);
        return false;
    };
    switch (settings->getSettingType(name)) {
    case SettingType::Bool:
        if (value.type != "bool") return wrongType();
        return settings->setBool(name, value.value == "true");
    case SettingType::Int: {
        if (value.type != "int") return wrongType();
        int parsed = 0;
        const auto converted = std::from_chars(value.value.data(), value.value.data() + value.value.size(), parsed);
        if (converted.ec == std::errc() && converted.ptr == value.value.data() + value.value.size())
            return settings->setInt(name, parsed);
        throw std::runtime_error("Integer family preference is outside the supported range: " + name);
    }
    case SettingType::Float: {
        if (value.type != "float") return wrongType();
        float parsed = 0.0f;
        const auto converted = std::from_chars(value.value.data(), value.value.data() + value.value.size(), parsed, std::chars_format::general);
        if (converted.ec == std::errc() && converted.ptr == value.value.data() + value.value.size() && std::isfinite(parsed))
            return settings->setFloat(name, parsed);
        throw std::runtime_error("Float family preference is outside the supported range: " + name);
    }
    case SettingType::String:
        if (value.type != "string") return wrongType();
        return settings->setString(name, value.value);
    default:
        if (value.type == "bool") return settings->setBool(name, value.value == "true");
        if (value.type == "string") return settings->setString(name, value.value);
        if (value.type == "int") {
            int parsed = 0;
            const auto converted = std::from_chars(value.value.data(), value.value.data() + value.value.size(), parsed);
            if (converted.ec != std::errc() || converted.ptr != value.value.data() + value.value.size())
                throw std::runtime_error("Integer family preference is outside the supported range: " + name);
            return settings->setInt(name, parsed);
        }
        if (value.type == "float") {
            float parsed = 0.0f;
            const auto converted = std::from_chars(value.value.data(), value.value.data() + value.value.size(), parsed, std::chars_format::general);
            if (converted.ec != std::errc() || converted.ptr != value.value.data() + value.value.size() || !std::isfinite(parsed))
                throw std::runtime_error("Float family preference is outside the supported range: " + name);
            return settings->setFloat(name, parsed);
        }
        throw std::runtime_error("Unknown family preference type: " + name);
    }
}

void applyValues(const StoreValues& values)
{
    Settings* settings = Settings::getInstance();
    std::set<std::string> names;
    for (const auto& name : settings->getSettingsNames()) names.insert(name);
    for (const auto& entry : values) names.insert(entry.first);
    for (const std::string& name : names) {
        if (!allowedKey(name)) continue;
        const auto found = values.find(name);
        if (found == values.end()) settings->resetSetting(name);
        else (void)applyValue(settings, name, found->second);
    }
}

fs::path getBaselinePath()
{
    return fs::u8path(Paths::getUserEmulationStationPath()) / ".family-default-preferences.json";
}

fs::path getProfilePreferencesPath(const std::string& id)
{
    return fs::u8path(FamilyProfiles::require(id).root) / "preferences.json";
}

StoreValues loadBaseline(bool createIfMissing, const StoreValues& current)
{
    const fs::path path = getBaselinePath();
    std::error_code ec;
    bool exists = fs::exists(path, ec);
    if (ec) throw fs::filesystem_error("Could not inspect family preference baseline", path, ec);
    if (!exists && createIfMissing) {
        FamilyPreferenceStore::write(path, current);
    }
    return FamilyPreferenceStore::read(path);
}

StoreValues mergeBaselineAndProfile(const StoreValues& baseline, const StoreValues& profile)
{
    StoreValues result;
    for (const auto& entry : baseline)
        if (allowedKey(entry.first)) result.emplace(entry.first, entry.second);
    for (const auto& entry : profile)
        if (allowedKey(entry.first)) result[entry.first] = entry.second;
    return result;
}

void setProfileIdEnvironment(const std::string& id)
{
#ifdef _WIN32
    if (!SetEnvironmentVariableA("RETROBAT_PROFILE_ID", id.c_str()))
        throw std::runtime_error("Could not switch the active family profile environment.");
#else
    if (::setenv("RETROBAT_PROFILE_ID", id.c_str(), 1) != 0)
        throw std::runtime_error("Could not switch the active family profile environment.");
#endif
}

void applyProfile(const std::string& id, bool establishBaseline)
{
    FamilyProfiles::Session lock(id, true);
    FamilyProfileFiles::prepare(fs::u8path(FamilyProfiles::require(id).root), fs::u8path(Paths::getUserEmulationStationPath()),
        fs::u8path(s_basePadtokey), fs::u8path(s_baseFavoritesMusic));
    const StoreValues current = captureCurrent();
    StoreValues baseline = loadBaseline(establishBaseline, current);
    const StoreValues profile = FamilyPreferenceStore::read(getProfilePreferencesPath(id));
    const StoreValues effective = mergeBaselineAndProfile(baseline, profile);
    applyValues(effective);
    s_baseline = std::move(baseline);
    s_applied = captureCurrent();
}
} // namespace

void initialize()
{
    if (!FamilyProfiles::enabled()) return;
    s_basePadtokey = Paths::getUserKeyboardMappingsPath();
    s_baseMusic = Paths::getUserMusicPath();
    s_baseFavoritesMusic = s_baseMusic.empty() ? Paths::getMusicPath() : s_baseMusic;
    s_initialized = false;
    const std::string id = FamilyProfiles::currentId();
    if (id.empty()) throw std::runtime_error("Family profile mode requires RETROBAT_PROFILE_ID.");
    const auto previous = captureCurrent();
    const StoreValues previousBaseline = s_baseline;
    const StoreValues previousApplied = s_applied;
    const std::string previousId = s_activeId;
    try {
        applyProfile(id, true);
        s_activeId = FamilyProfiles::require(id).id;
        const auto root = FamilyProfiles::require(id).root;
        Paths::getProfileConfigPath() = root + "/config";
        Paths::getUserKeyboardMappingsPath() = root + "/config/padtokey";
        Paths::getUserMusicPath() = root + "/music";
        s_initialized = true;
    } catch (...) {
        try { applyValues(previous); } catch (...) {}
        s_baseline = previousBaseline;
        s_applied = previousApplied;
        s_activeId = previousId;
        throw;
    }
}

void save()
{
    if (!FamilyProfiles::enabled()) return;
    if (!s_initialized) throw std::runtime_error("Family preferences are not initialized; refusing to save the active profile.");
    FamilyProfiles::Session lock(s_activeId, true);
    const fs::path profilePath = getProfilePreferencesPath(s_activeId);
    StoreValues stored = FamilyPreferenceStore::read(profilePath);
    for (auto it = stored.begin(); it != stored.end();) {
        if (!allowedKey(it->first)) it = stored.erase(it);
        else ++it;
    }
    const StoreValues current = captureCurrent();
    bool changed = false;
    for (const auto& entry : current) {
        const auto applied = s_applied.find(entry.first);
        if (applied != s_applied.end() && applied->second == entry.second) continue;
        // Keep values explicit across machines with different local baselines.
        stored[entry.first] = entry.second;
        changed = true;
    }
    for (const auto& entry : s_applied) {
        if (current.find(entry.first) != current.end()) continue;
        const auto baseline = s_baseline.find(entry.first);
        if (baseline != s_baseline.end()) stored[entry.first] = baseline->second;
        else stored.erase(entry.first);
        changed = true;
    }
    if (changed) FamilyPreferenceStore::write(profilePath, stored);
    s_applied = current;
}

bool switchTo(const std::string& requestedId)
{
    if (!FamilyProfiles::enabled()) return false;
    if (!s_initialized) initialize();
    const auto profile = FamilyProfiles::require(requestedId);
    if (profile.id == s_activeId) return false;

    save();
    const StoreValues previous = captureCurrent();
    const StoreValues previousBaseline = s_baseline;
    const StoreValues previousApplied = s_applied;
    const std::string previousId = s_activeId;
    try {
        applyProfile(profile.id, false);
        setProfileIdEnvironment(profile.id);
        s_activeId = profile.id;
        s_initialized = true;
    } catch (...) {
        try { setProfileIdEnvironment(previousId); } catch (...) {}
        try {
            applyValues(previous);
            s_baseline = previousBaseline;
            s_applied = previousApplied;
            s_activeId = previousId;
        } catch (...) {
            s_initialized = false;
        }
        throw;
    }
    Settings::getInstance()->saveFile();
    s_restartRequested = true;
    return true;
}

bool restartRequested() { return s_restartRequested; }

void restartFrontend()
{
    if (!s_restartRequested) return;
#ifdef _WIN32
    const std::wstring eventName = L"Local\\RetroBatProfileReady-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
    if (!ready) throw std::runtime_error("Could not prepare the profile transition.");
    if (!SetEnvironmentVariableW(L"RETROBAT_PROFILE_READY", eventName.c_str())) {
        CloseHandle(ready);
        throw std::runtime_error("Could not prepare the profile transition environment.");
    }
    std::wstring command = GetCommandLineW();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process);
    const DWORD startError = GetLastError();
    SetEnvironmentVariableW(L"RETROBAT_PROFILE_READY", nullptr);
    if (!started) {
        CloseHandle(ready);
        throw std::runtime_error("Could not restart RetroBat after switching profile (Windows error " + std::to_string(startError) + ").");
    }
    CloseHandle(process.hThread);
    const HANDLE handles[] = {ready, process.hProcess};
    const ULONGLONG deadline = GetTickCount64() + 30000;
    DWORD result = WAIT_TIMEOUT;
    while (GetTickCount64() < deadline) {
        result = MsgWaitForMultipleObjects(2, handles, FALSE, 100, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0 || result == WAIT_OBJECT_0 + 1 || result == WAIT_FAILED) break;
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    CloseHandle(ready);
    CloseHandle(process.hProcess);
    if (result == WAIT_OBJECT_0 + 1)
        throw std::runtime_error("The new profile could not start. Check the EmulationStation log and reopen RetroBat.");
#else
    throw std::runtime_error("Family profile restart is currently supported on Windows.");
#endif
}
void frontendReady()
{
#ifdef _WIN32
    static bool notified = false;
    if (notified) return;
    notified = true;
    wchar_t name[256];
    const DWORD length = GetEnvironmentVariableW(L"RETROBAT_PROFILE_READY", name, 256);
    SetEnvironmentVariableW(L"RETROBAT_PROFILE_READY", nullptr);
    if (!length || length >= 256) return;
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    if (ready) { SetEvent(ready); CloseHandle(ready); }
#endif
}
} // namespace FamilyPreferences
