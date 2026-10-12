#include "FamilyPreferenceStore.h"

#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::cerr << "Check failed: " << #x << " at " << __LINE__ << std::endl; std::exit(1); } } while (false)
#include <chrono>
#include <iostream>

int main()
{
    namespace fps = FamilyPreferenceStore;
    const auto root = fps::fs::current_path();
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = root / ("test-data-" + std::to_string(unique));
    fps::fs::create_directory(dir);
    try {
        const auto path = dir / "settings.json";
        CHECK(fps::read(path).empty());

        fps::Values values{
            {"audio.enabled", {"bool", "true"}},
            {"input.deadzone", {"float", "0.125"}},
            {"ui.scale", {"int", "-2"}},
            {"ui.title", {"string", "Caf\xC3\xA9 \xF0\x9F\xA7\x80"}},
        };
        fps::write(path, values);
        CHECK(fps::read(path) == values);

        auto invalid = values;
        invalid["audio.enabled"].value = "yes";
        bool rejectedWrite = false;
        try { fps::write(path, invalid); } catch (const std::runtime_error&) { rejectedWrite = true; }
        CHECK(rejectedWrite);
        CHECK(fps::read(path) == values);

        const auto malformed = dir / "malformed.json";
        { std::ofstream out(malformed, std::ios::binary); out << "{\"x\":{\"type\":\"float\",\"value\":\"nan\"}}"; }
        bool rejectedRead = false;
        try { (void)fps::read(malformed); } catch (const std::runtime_error&) { rejectedRead = true; }
        CHECK(rejectedRead);

        const auto badJson = dir / "bad-json.json";
        { std::ofstream out(badJson, std::ios::binary); out << "{not-json"; }
        bool rejectedSyntax = false;
        try { (void)fps::read(badJson); } catch (const std::runtime_error&) { rejectedSyntax = true; }
        CHECK(rejectedSyntax);

        auto invalidFloat = values;
        invalidFloat["input.deadzone"].value = "inf";
        bool rejectedFloat = false;
        try { fps::write(dir / "bad-float.json", invalidFloat); } catch (const std::runtime_error&) { rejectedFloat = true; }
        CHECK(rejectedFloat);

        auto invalidInt = values;
        invalidInt["ui.scale"].value = "01";
        bool rejectedInt = false;
        try { fps::write(dir / "bad-int.json", invalidInt); } catch (const std::runtime_error&) { rejectedInt = true; }
        CHECK(rejectedInt);

        auto longName = fps::Values{{std::string(fps::MaxNameBytes + 1, 'k'), {"string", "x"}}};
        bool rejectedName = false;
        try { fps::write(dir / "long-name.json", longName); } catch (const std::runtime_error&) { rejectedName = true; }
        CHECK(rejectedName);

        auto longValue = fps::Values{{"long", {"string", std::string(fps::MaxValueBytes + 1, 'x')}}};
        bool rejectedValue = false;
        try { fps::write(dir / "long-value.json", longValue); } catch (const std::runtime_error&) { rejectedValue = true; }
        CHECK(rejectedValue);

        fps::Values tooMany;
        for (std::size_t i = 0; i <= fps::MaxEntries; ++i) tooMany.emplace("k" + std::to_string(i), fps::Value{"bool", "true"});
        bool rejectedCount = false;
        try { fps::write(dir / "too-many.json", tooMany); } catch (const std::runtime_error&) { rejectedCount = true; }
        CHECK(rejectedCount);

        const auto oversized = dir / "oversized.json";
        { std::ofstream out(oversized, std::ios::binary); out << std::string(fps::MaxFileBytes + 1, ' '); }
        bool rejectedSize = false;
        try { (void)fps::read(oversized); } catch (const std::runtime_error&) { rejectedSize = true; }
        CHECK(rejectedSize);

        std::cout << "PASS: missing file, typed UTF-8 round trip, old-file preservation, malformed/non-finite primitives, and all size/count limits\n";
        fps::fs::remove_all(dir);
        return 0;
    } catch (...) {
        fps::fs::remove_all(dir);
        throw;
    }
}
