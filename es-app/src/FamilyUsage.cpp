#include "FamilyUsage.h"

#include "FamilyProfiles.h"
#include "FileData.h"
#include "Settings.h"
#include "SystemData.h"
#include "Log.h"

#include <mutex>

namespace FamilyUsage {
namespace {
struct DisplayCache {
	std::mutex mutex;
	std::string familyRootIdentity;
	std::string profileIdIdentity;
	FamilyProfiles::Profile profile;
	bool profileLoaded = false;
	std::string usageRoot;
	std::string snapshotRoot;
	fs::file_time_type modified{};
	bool exists = false;
	Snapshot snapshot;
};

DisplayCache& displayCache()
{
	static DisplayCache cache;
	return cache;
}
}

std::int64_t displaySeconds(const FileData* file, std::int64_t legacySeconds)
{
	if (!file || !FamilyProfiles::enabled()) return legacySeconds;
	try
	{
		const auto* source = const_cast<FileData*>(file)->getSourceFileData();
		const auto* system = source ? source->getSystem() : nullptr;
		if (!source || !system) return legacySeconds;
		const std::string key = gameKey(system->getName(), source->getPath(), system->getStartPath());
		const std::string familyRootIdentity = FamilyProfiles::detail::environment("RETROBAT_FAMILY_ROOT");
		const std::string profileIdIdentity = FamilyProfiles::currentId();
		auto& cache = displayCache();
		{
			std::lock_guard<std::mutex> guard(cache.mutex);
			if (!cache.profileLoaded || cache.familyRootIdentity != familyRootIdentity || cache.profileIdIdentity != profileIdIdentity)
			{
				cache.profile = FamilyProfiles::require("");
				cache.familyRootIdentity = familyRootIdentity;
				cache.profileIdIdentity = profileIdIdentity;
				cache.usageRoot = detail::rootFromProfileRoot(cache.profile.root);
				cache.profileLoaded = true;
			}
			std::error_code ec;
			const fs::path path = detail::filePath(cache.usageRoot);
			const bool exists = fs::exists(path, ec) && !ec;
			const auto modified = exists ? fs::last_write_time(path, ec) : fs::file_time_type{};
			if (ec) throw std::runtime_error("Could not inspect family usage data: " + ec.message());
			if (cache.snapshotRoot != cache.usageRoot || cache.exists != exists || cache.modified != modified)
			{
				cache.snapshot = load(cache.usageRoot);
				cache.snapshotRoot = cache.usageRoot;
				cache.exists = exists;
				cache.modified = modified;
			}
			return cache.snapshot.displaySeconds(Settings::getInstance()->getString("FamilyUsageScope"), cache.profile.id, key, legacySeconds);
		}
	}
	catch (const std::exception& error)
	{
		LOG(LogWarning) << "Could not read family playtime: " << error.what();
		return Settings::getInstance()->getString("FamilyUsageScope") == "everyone" ? legacySeconds : 0;
	}
}
} // namespace FamilyUsage
