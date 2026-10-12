#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
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
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

class FileData;

namespace FamilyUsage {
namespace fs = std::filesystem;

struct Snapshot {
	std::map<std::string, std::int64_t> baseline;
	std::map<std::string, std::map<std::string, std::int64_t>> profiles;

	std::int64_t personalSeconds(const std::string& profile, const std::string& game) const
	{
		auto owner = profiles.find(profile);
		if (owner == profiles.end()) return 0;
		auto value = owner->second.find(game);
		return value == owner->second.end() ? 0 : value->second;
	}

	std::int64_t everyoneSeconds(const std::string& game, std::int64_t legacySeconds) const
	{
		auto base = baseline.find(game);
		std::int64_t total = base == baseline.end() ? std::max<std::int64_t>(0, legacySeconds) : base->second;
		for (const auto& profile : profiles) {
			auto value = profile.second.find(game);
			if (value != profile.second.end()) total = value->second > std::numeric_limits<std::int64_t>::max() - total ?
				std::numeric_limits<std::int64_t>::max() : total + value->second;
		}
		return total;
	}

	std::int64_t displaySeconds(const std::string& scope, const std::string& profile, const std::string& game, std::int64_t legacySeconds) const
	{
		return scope == "everyone" ? everyoneSeconds(game, legacySeconds) : personalSeconds(profile, game);
	}
};

namespace detail {
inline fs::path filePath(const std::string& root) { return fs::u8path(root) / ".family-usage.json"; }
inline fs::path lockPath(const std::string& root) { return fs::u8path(root) / ".family-usage.lock"; }
inline std::string rootFromProfileRoot(const std::string& profileRoot)
{
	const fs::path root = fs::u8path(profileRoot).parent_path();
#if defined(__cpp_lib_char8_t)
	const auto value = root.u8string();
	return std::string(reinterpret_cast<const char*>(value.data()), value.size());
#else
	return root.u8string();
#endif
}

class Lock {
public:
	explicit Lock(const std::string& root)
	{
		std::error_code ec;
		fs::create_directories(fs::u8path(root), ec);
		if (ec) throw std::runtime_error("Could not create family usage directory: " + ec.message());
#ifdef _WIN32
		for (int attempt = 0; attempt < 1200; ++attempt) {
			handle = CreateFileW(lockPath(root).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (handle != INVALID_HANDLE_VALUE) break;
			const DWORD error = GetLastError();
			if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)
				throw std::runtime_error("Could not lock family usage data (Windows error " + std::to_string(error) + ").");
			Sleep(50);
		}
		if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Timed out waiting for family usage data lock.");
#else
		fd = ::open(lockPath(root).c_str(), O_CREAT | O_RDWR, 0600);
		if (fd < 0 || ::flock(fd, LOCK_EX) != 0) {
			if (fd >= 0) ::close(fd);
			fd = -1;
			throw std::runtime_error("Could not lock family usage data.");
		}
#endif
	}
	~Lock()
	{
#ifdef _WIN32
		if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
#else
		if (fd >= 0) { ::flock(fd, LOCK_UN); ::close(fd); }
#endif
	}
	Lock(const Lock&) = delete;
	Lock& operator=(const Lock&) = delete;
private:
#ifdef _WIN32
	HANDLE handle = INVALID_HANDLE_VALUE;
#else
	int fd = -1;
#endif
};

inline std::int64_t getInt64(const rapidjson::Value& value)
{
	if (!value.IsInt64() || value.GetInt64() < 0) throw std::runtime_error("Invalid seconds in family usage data.");
	return value.GetInt64();
}

inline Snapshot read(const std::string& root)
{
	Snapshot snapshot;
	const fs::path path = filePath(root);
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		std::error_code ec;
		if (fs::exists(path, ec) || ec) throw std::runtime_error("Could not open family usage data.");
		return snapshot;
	}
	std::error_code sizeError;
	const auto size = fs::file_size(path, sizeError);
	if (sizeError || size > 16 * 1024 * 1024) throw std::runtime_error("Family usage data exceeds 16 MiB or could not be inspected.");
	std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	if (json.size() != size) throw std::runtime_error("Family usage data changed while being read.");
	rapidjson::Document document;
	document.Parse(json.data(), json.size());
	if (document.HasParseError() || !document.IsObject() || !document.HasMember("version") || !document["version"].IsInt() || document["version"].GetInt() != 1)
		throw std::runtime_error("Family usage data is invalid JSON or has an unsupported version.");
	auto parseGames = [](const rapidjson::Value& object, std::map<std::string, std::int64_t>& out) {
		if (!object.IsObject()) throw std::runtime_error("Invalid family usage game map.");
		for (auto it = object.MemberBegin(); it != object.MemberEnd(); ++it) {
			if (!it->name.IsString()) throw std::runtime_error("Invalid family usage game key.");
			out.emplace(std::string(it->name.GetString(), it->name.GetStringLength()), getInt64(it->value));
		}
	};
	if (!document.HasMember("baseline") || !document["baseline"].IsObject() || !document.HasMember("profiles") || !document["profiles"].IsObject())
		throw std::runtime_error("Family usage data is missing required maps.");
	parseGames(document["baseline"], snapshot.baseline);
	for (auto it = document["profiles"].MemberBegin(); it != document["profiles"].MemberEnd(); ++it) {
		if (!it->name.IsString()) throw std::runtime_error("Invalid family profile usage ID.");
		auto& games = snapshot.profiles[std::string(it->name.GetString(), it->name.GetStringLength())];
		parseGames(it->value, games);
	}
	return snapshot;
}

inline void write(const std::string& root, const Snapshot& snapshot)
{
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("version"); writer.Int(1);
	writer.Key("baseline"); writer.StartObject();
	for (const auto& item : snapshot.baseline) { writer.Key(item.first.c_str()); writer.Int64(item.second); }
	writer.EndObject();
	writer.Key("profiles"); writer.StartObject();
	for (const auto& profile : snapshot.profiles) {
		writer.Key(profile.first.c_str()); writer.StartObject();
		for (const auto& item : profile.second) { writer.Key(item.first.c_str()); writer.Int64(item.second); }
		writer.EndObject();
	}
	writer.EndObject(); writer.EndObject();
	if (!writer.IsComplete() || buffer.GetSize() > 16 * 1024 * 1024) throw std::runtime_error("Could not serialize family usage data.");
	const fs::path target = filePath(root);
	const fs::path temp = target.wstring() + L".tmp";
	{
		std::ofstream output(temp, std::ios::binary | std::ios::trunc);
		if (!output) throw std::runtime_error("Could not create temporary family usage data.");
		output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
		output.flush();
		if (!output) { output.close(); std::error_code ec; fs::remove(temp, ec); throw std::runtime_error("Could not write family usage data."); }
	}
#ifdef _WIN32
	if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		DeleteFileW(temp.c_str()); throw std::runtime_error("Could not replace family usage data.");
	}
#else
	std::error_code ec;
	fs::rename(temp, target, ec);
	if (ec) { fs::remove(temp); throw std::runtime_error("Could not replace family usage data: " + ec.message()); }
#endif
}
} // namespace detail

inline Snapshot load(const std::string& root) { return detail::read(root); }

inline void ensureBaseline(const std::string& root, const std::string& game, std::int64_t legacySeconds)
{
	detail::Lock lock(root);
	Snapshot snapshot = detail::read(root);
	if (snapshot.baseline.find(game) == snapshot.baseline.end()) {
		snapshot.baseline.emplace(game, std::max<std::int64_t>(0, legacySeconds));
		detail::write(root, snapshot);
	}
}

inline void addPlayTime(const std::string& root, const std::string& profile, const std::string& game, std::int64_t seconds)
{
	if (seconds <= 0) return;
	detail::Lock lock(root);
	Snapshot snapshot = detail::read(root);
	auto& total = snapshot.profiles[profile][game];
	if (seconds > std::numeric_limits<std::int64_t>::max() - total)
		throw std::overflow_error("Family playtime exceeds the supported range.");
	total += seconds;
	detail::write(root, snapshot);
}

inline std::string gameKey(const std::string& system, const std::string& gamePath, const std::string& startPath)
{
	auto normalize = [](std::string path) {
		std::replace(path.begin(), path.end(), '\\', '/');
		while (path.size() > 1 && path.back() == '/') path.pop_back();
#ifdef _WIN32
		std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
		return path;
	};
	std::string relative = normalize(gamePath);
	const std::string root = normalize(startPath);
	if (!root.empty() && relative.size() > root.size() && relative.compare(0, root.size(), root) == 0 && relative[root.size()] == '/')
		relative = relative.substr(root.size() + 1);
	return system + "/" + relative;
}

std::int64_t displaySeconds(const ::FileData* file, std::int64_t legacySeconds);
} // namespace FamilyUsage
