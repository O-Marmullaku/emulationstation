#include "../es-app/src/FamilyUsage.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::cerr << "Check failed: " << #x << " at " << __LINE__ << std::endl; std::exit(1); } } while (false)

template<class F> static void mustThrow(F action)
{
	bool threw = false;
	try { action(); } catch (const std::overflow_error&) { threw = true; }
	CHECK(threw);
}

int main()
{
	namespace fu = FamilyUsage;
	const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto root = fu::fs::temp_directory_path() / ("family-usage-test-" + std::to_string(nonce));
	fu::fs::create_directories(root);
	try
	{
		const auto rootUtf8 = root.u8string();
		const std::string familyRoot(rootUtf8.begin(), rootUtf8.end());
		const std::string game = fu::gameKey("ps2", "R:/roms/PS2/God of War.iso", "R:/roms/PS2");
		CHECK(game == "ps2/god of war.iso");

		// Existing gamelist time remains unassigned Everyone history.
		CHECK(fu::load(familyRoot).everyoneSeconds(game, 900) == 900);
		CHECK(fu::load(familyRoot).personalSeconds("alice", game) == 0);
		fu::ensureBaseline(familyRoot, game, 900);
		fu::ensureBaseline(familyRoot, game, 1200); // never replace the captured baseline
		fu::addPlayTime(familyRoot, "alice", game, 60);
		fu::addPlayTime(familyRoot, "bibi", game, 30);
		auto snapshot = fu::load(familyRoot);
		CHECK(snapshot.baseline.at(game) == 900);
		CHECK(snapshot.personalSeconds("alice", game) == 60);
		CHECK(snapshot.personalSeconds("bibi", game) == 30);
		CHECK(snapshot.everyoneSeconds(game, 9999) == 990);
		CHECK(snapshot.displaySeconds("personal", "alice", game, 9999) == 60);
		CHECK(snapshot.displaySeconds("everyone", "alice", game, 9999) == 990);
		fu::ensureBaseline(familyRoot, "ps2/overflow.iso", std::numeric_limits<std::int64_t>::max() - 1);
		fu::addPlayTime(familyRoot, "alice", "ps2/overflow.iso", 2);
		CHECK(fu::load(familyRoot).everyoneSeconds("ps2/overflow.iso", 0) == std::numeric_limits<std::int64_t>::max());
		fu::addPlayTime(familyRoot, "alice", "ps2/record-overflow.iso", std::numeric_limits<std::int64_t>::max());
		mustThrow([&] { fu::addPlayTime(familyRoot, "alice", "ps2/record-overflow.iso", 1); });

		// Concurrent clients serialize updates through the shared lock file.
		std::vector<std::thread> workers;
		for (int i = 0; i < 4; ++i)
			workers.emplace_back([&, i] {
				for (int j = 0; j < 25; ++j) fu::addPlayTime(familyRoot, "alice", "ps2/concurrent.iso", 1);
			});
		for (auto& worker : workers) worker.join();
		CHECK(fu::load(familyRoot).personalSeconds("alice", "ps2/concurrent.iso") == 100);
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << std::endl;
		fu::fs::remove_all(root);
		return 1;
	}
	fu::fs::remove_all(root);
	return 0;
}
