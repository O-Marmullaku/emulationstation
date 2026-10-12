#include "FamilyPreferences.h"
#include "FamilyPreferenceStore.h"
#include "FamilyProfiles.h"
#include "Paths.h"
#include "Settings.h"

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
static const std::string gameKey = "ps2[\"Input Audio Device.iso\"].internalresolution";
static const std::string controllerKey = "ps2[\"Normal Game.iso\"].controller";

static void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static void setEnvironment(const char* name, const std::string& value)
{
#ifdef _WIN32
    SetEnvironmentVariableA(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

int main()
try {
#ifdef _WIN32
    const std::string readyName = "Local\\RetroBatProfileTest-" + std::to_string(GetCurrentProcessId());
    HANDLE ready = CreateEventA(nullptr, TRUE, FALSE, readyName.c_str());
    check(ready != nullptr, "could not create readiness test event");
    setEnvironment("RETROBAT_PROFILE_READY", readyName);
    FamilyPreferences::frontendReady();
    check(WaitForSingleObject(ready, 0) == WAIT_OBJECT_0, "first frame did not signal the outgoing frontend");
    check(GetEnvironmentVariableA("RETROBAT_PROFILE_READY", nullptr, 0) == 0, "readiness event leaked to later launches");
    CloseHandle(ready);
#endif
    const fs::path root = fs::current_path() / ("profile-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    fs::create_directories(root / "home");
    fs::create_directories(root / "family" / "A");
    fs::create_directories(root / "family" / "B");
    fs::create_directories(root / "shared-music");
    { std::ofstream favorites(root / "shared-music" / "favorites.m3u"); favorites << "track.ogg;Track\n"; }
    Paths::getMusicPath() = (root / "shared-music").u8string();
    setEnvironment("RETROBAT_FAMILY_ROOT", (root / "family").u8string());
    setEnvironment("RETROBAT_TEST_HOME", (root / "home").u8string());
    setEnvironment("RETROBAT_PROFILE_ID", "A");

    Settings::clear();
    Settings* settings = Settings::getInstance();
    settings->addString("global.language", "en");
    settings->addString("ThemeSet", "classic");
    settings->addBool("Windowed", true);
    settings->addInt("global.volume", 5);
    settings->addBool("global.retroachievements", false);
    settings->addInt(gameKey, 2);
    settings->addBool(controllerKey, false);
    FamilyPreferences::initialize();
    check(fs::exists(root / "family" / "A" / "music" / "favorites.m3u"), "first-use profile did not seed favorites from system music when no custom user music path exists");

    settings->setString("global.language", "fr");
    settings->setString("ThemeSet", "modern");
    settings->setInt(gameKey, 3);
    settings->setBool(controllerKey, true);
    settings->setBool("global.retroachievements", true);
    settings->setBool("global.new-option", true);
    FamilyPreferences::save();
    check(FamilyProfiles::lastSessionId() == "A", "A save did not lock owner A");
    check(FamilyProfiles::lastSessionWasPreferences(), "A save did not request preference lock");

    auto storedA = FamilyPreferenceStore::read(root / "family" / "A" / "preferences.json");
    check(storedA.at("global.language").value == "fr", "A preference not saved");
    check(storedA.at(gameKey).value == "3", "per-game key with Input/Audio title was rejected");
    check(storedA.at(controllerKey).value == "true", "controller preference was not persisted");
    check(storedA.at("global.retroachievements").value == "true", "achievement preference was not persisted");
    check(storedA.find("Windowed") == storedA.end(), "transient command-line option was persisted");

    check(FamilyPreferences::switchTo("B"), "switch must request frontend restart");
    check(settings->getString("ThemeSet") == "classic", "theme leaked from A into B");
    check(FamilyProfiles::currentId() == "B", "switch did not update profile environment");
    check(settings->getString("global.language") == "en", "B did not receive baseline preference");
    check(settings->getInt(gameKey) == 2, "B did not receive per-game baseline");
    check(settings->getSettingType("global.new-option") == SettingType::Unknown, "A-only key leaked into B");
    check(FamilyProfiles::lastSessionId() == "B", "switch did not use owner B lock");

    settings->setInt("global.volume", 7);
    FamilyPreferences::save();
    auto storedB = FamilyPreferenceStore::read(root / "family" / "B" / "preferences.json");
    storedB["global.external-edit"] = {"string", "kept"};
    FamilyPreferenceStore::write(root / "family" / "B" / "preferences.json", storedB);
    settings->setString("global.language", "it");
    FamilyPreferences::save();
    storedB = FamilyPreferenceStore::read(root / "family" / "B" / "preferences.json");
    check(storedB.at("global.external-edit").value == "kept", "external profile edit was overwritten");
    check(storedB.at("global.language").value == "it", "changed B preference not saved");

    FamilyPreferences::switchTo("A");
    check(settings->getString("ThemeSet") == "modern", "A theme was not restored");
    check(settings->getString("global.language") == "fr", "switch back did not restore A value");
    check(settings->getInt(gameKey) == 3, "switch back did not restore per-game A value");
    check(settings->getBool("global.new-option"), "new key absent from initial Settings map was not restored");

    FamilyPreferenceStore::write(root / "family" / "B" / "preferences.json", {{"global.volume", {"int", "2147483648"}}});
    bool rejected = false;
    try { FamilyPreferences::switchTo("B"); }
    catch (const std::exception&) { rejected = true; }
    check(rejected, "out-of-range target preference was accepted");
    check(FamilyProfiles::currentId() == "A", "failed switch changed active profile environment");
    check(settings->getString("global.language") == "fr", "failed switch did not restore active A settings");

    {
        std::ofstream corrupt(root / "family" / "B" / "preferences.json", std::ios::binary | std::ios::trunc);
        corrupt << "{not json";
    }
    rejected = false;
    try { FamilyPreferences::switchTo("B"); }
    catch (const std::exception&) { rejected = true; }
    check(rejected, "malformed target JSON was accepted");
    check(FamilyProfiles::currentId() == "A", "malformed target changed active profile environment");
    check(settings->getString("global.language") == "fr", "malformed target did not restore active A settings");

    std::cout << "PASS: preference-owner lock selection, baseline and A/B switching, absent/new keys, per-game title filtering, exclusions, external-file merge, and malformed/range-error rollback\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << "\n";
    return 1;
}
