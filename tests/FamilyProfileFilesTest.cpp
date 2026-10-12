#include "FamilyProfileFiles.h"
#include "ProfileMusicPaths.h"

#include <fstream>
#include <chrono>
#include <iostream>

namespace fs = std::filesystem;

static void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

static void write(const fs::path& path, const std::string& value)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!out) throw std::runtime_error("test fixture write failed");
}

int main()
try {
    check(ProfileMusicPaths::encode("B:/family/A/music/song.ogg", "B:/family/A/music", "B:/shared-user/music", "B:/shared/music") == "@user/song.ogg", "profile music path was not portable");
    check(ProfileMusicPaths::encode("B:/shared-user/music/song.ogg", "B:/family/A/music", "B:/shared-user/music", "B:/shared/music") == "@shared/song.ogg", "shared user music path was not portable");
    check(ProfileMusicPaths::decode("@user/song.ogg", "S:/A/music", "S:/shared-user/music", "R:/music") == "S:/A/music/song.ogg", "profile music path did not follow client root");
    check(ProfileMusicPaths::decode("@shared/song.ogg", "S:/A/music", "S:/shared-user/music", "R:/music") == "S:/shared-user/music/song.ogg", "shared user music path did not follow client root");
    check(ProfileMusicPaths::decode("@shared/../secret", "S:/A/music", "S:/shared-user/music", "R:/music") == "@shared/../secret", "relative playlist path escaped shared music folder");
    const fs::path root = fs::current_path() / ("profile-files-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    const fs::path es = root / "base-es", pad = root / "base-pad", music = root / "base-music";
    const fs::path systemMusic = root / "system-music", profile = root / "profile-a";
    write(es / "es_input.cfg", "input");
    write(es / "es_last_input.cfg", "last");
    write(es / "es_padtokey.cfg", "padtokey-config");
    write(es / "themesettings" / "dark.cfg", "theme");
    write(es / "themesettings" / "ignore.txt", "ignore");
    write(pad / "keyboard.keys", "keys");
    write(pad / "ignore.cfg", "ignore");
    write(music / "favorites.m3u", "track.ogg;Track\n" + (music / "shared-track.ogg").generic_u8string() + ";Shared Track\n" +
        (systemMusic / "system-track.ogg").generic_u8string() + ";System Track\nZ:/outside/other.ogg;External Track\nplain-line\n");
    write(music / "track.ogg", "audio");

    FamilyProfileFiles::prepare(profile, es, pad, music, music, systemMusic);
    check(fs::exists(profile / "config" / "es_input.cfg"), "es_input was not seeded");
    check(fs::exists(profile / "config" / "es_last_input.cfg"), "es_last_input was not seeded");
    check(fs::exists(profile / "config" / "es_padtokey.cfg"), "es_padtokey was not seeded");
    check(fs::exists(profile / "config" / "themesettings" / "dark.cfg"), "theme config was not seeded");
    check(!fs::exists(profile / "config" / "themesettings" / "ignore.txt"), "non-cfg theme file was copied");
    check(fs::exists(profile / "config" / "padtokey" / "keyboard.keys"), "padtokey mapping was not seeded");
    check(!fs::exists(profile / "config" / "padtokey" / "ignore.cfg"), "non-keys mapping was copied");
    check(fs::exists(profile / "music" / "favorites.m3u"), "favorites playlist was not seeded");
    check(!fs::exists(profile / "music" / "track.ogg"), "music content was copied");
    {
        std::ifstream input(profile / "music" / "favorites.m3u", std::ios::binary);
        const std::string favorites((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        check(favorites.find("@shared/shared-track.ogg;Shared Track") != std::string::npos, "shared favorite was not made portable");
        check(favorites.find("@system/system-track.ogg;System Track") != std::string::npos, "system favorite was not made portable");
        check(favorites.find("Z:/outside/other.ogg;External Track") != std::string::npos, "external favorite was changed");
        check(favorites.find("plain-line\n") != std::string::npos, "non-entry line was changed");
    }

    write(profile / "music" / "favorites.m3u", (music / "shared-track.ogg").generic_u8string() + ";Existing Shared\n");
    FamilyProfileFiles::prepare(profile, es, pad, music, music, systemMusic);
    {
        std::ifstream input(profile / "music" / "favorites.m3u", std::ios::binary);
        const std::string favorites((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        check(favorites == "@shared/shared-track.ogg;Existing Shared\n", "existing profile favorite was not normalized");
    }

    fs::remove(profile / "config" / "es_input.cfg");
    fs::remove(profile / "config" / "themesettings" / "dark.cfg");
    fs::remove(profile / "music" / "favorites.m3u");
    write(profile / "config" / "other.txt", "preserve");
    write(es / "es_input.cfg", "changed-base");
    write(es / "themesettings" / "dark.cfg", "changed-base");
    FamilyProfileFiles::prepare(profile, es, pad, music, music, systemMusic);
    check(!fs::exists(profile / "config" / "es_input.cfg"), "deleted input config was resurrected");
    check(!fs::exists(profile / "config" / "themesettings" / "dark.cfg"), "deleted theme config was resurrected");
    check(!fs::exists(profile / "music" / "favorites.m3u"), "deleted favorites playlist was resurrected");
    check(fs::exists(profile / "config" / "other.txt"), "existing profile config contents were replaced");

    const fs::path badEs = root / "bad-es", badProfile = root / "profile-bad";
    write(badEs / "themesettings" / "target.cfg", "target");
    std::error_code symlinkError;
    fs::create_symlink(badEs / "themesettings" / "target.cfg", badEs / "themesettings" / "link.cfg", symlinkError);
    bool rejected = false;
    if (!symlinkError) {
        try {
    FamilyProfileFiles::prepare(badProfile, badEs, pad, music, music, systemMusic); }
        catch (const std::exception&) { rejected = true; }
        check(rejected, "symlink theme config was accepted");
        check(!fs::exists(badProfile / "config"), "failed seed promoted partial config directory");
    }

    const fs::path largeEs = root / "large-es", largeProfile = root / "profile-large";
    fs::create_directories(largeEs);
    {
        std::ofstream out(largeEs / "es_input.cfg", std::ios::binary);
        out.seekp(16 * 1024 * 1024);
        out.put('x');
    }
    rejected = false;
    try {
    FamilyProfileFiles::prepare(largeProfile, largeEs, pad, music, music, systemMusic); }
    catch (const std::exception&) { rejected = true; }
    check(rejected, "oversized config file was accepted");
    check(!fs::exists(largeProfile / "config"), "oversized seed promoted partial config directory");

    std::cout << "PASS: first-use seeding, no reseed after edits/deletions, preservation, safe symlink rejection, and size-bounded atomic promotion\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << "\n";
    return 1;
}
