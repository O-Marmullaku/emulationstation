#pragma once
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

enum class SettingType { Unknown, String, Int, Float, Bool };
class Settings {
public:
    static bool isPersistentSetting(const std::string& name) { return name != "Windowed"; }
    inline static std::function<void()> fileSaved;
    static Settings* getInstance() { static Settings instance; return &instance; }
    static void clear() { getInstance()->reset(); }
    void reset()
    {
        strings.clear(); bools.clear(); ints.clear(); floats.clear();
        defaultStrings.clear(); defaultBools.clear(); defaultInts.clear(); defaultFloats.clear();
    }
    void addString(const std::string& k, const std::string& v) { strings[k] = v; defaultStrings[k] = v; }
    void addBool(const std::string& k, bool v) { bools[k] = v; defaultBools[k] = v; }
    void addInt(const std::string& k, int v) { ints[k] = v; defaultInts[k] = v; }
    void addFloat(const std::string& k, float v) { floats[k] = v; defaultFloats[k] = v; }
    SettingType getSettingType(const std::string& k)
    {
        if (strings.count(k)) return SettingType::String;
        if (bools.count(k)) return SettingType::Bool;
        if (ints.count(k)) return SettingType::Int;
        if (floats.count(k)) return SettingType::Float;
        return SettingType::Unknown;
    }
    std::vector<std::string> getSettingsNames()
    {
        std::set<std::string> all;
        for (const auto& x : strings) all.insert(x.first);
        for (const auto& x : bools) all.insert(x.first);
        for (const auto& x : ints) all.insert(x.first);
        for (const auto& x : floats) all.insert(x.first);
        return {all.begin(), all.end()};
    }
    std::string getString(const std::string& k) { return strings.at(k); }
    bool getBool(const std::string& k) { return bools.at(k); }
    int getInt(const std::string& k) { return ints.at(k); }
    float getFloat(const std::string& k) { return floats.at(k); }
    bool setString(const std::string& k, const std::string& v) { auto it = strings.find(k); if (it != strings.end() && it->second == v) return false; strings[k] = v; return true; }
    bool setBool(const std::string& k, bool v) { if (bools.count(k) && bools[k] == v) return false; bools[k] = v; return true; }
    bool setInt(const std::string& k, int v) { if (ints.count(k) && ints[k] == v) return false; ints[k] = v; return true; }
    bool setFloat(const std::string& k, float v) { if (floats.count(k) && floats[k] == v) return false; floats[k] = v; return true; }
    void resetSetting(const std::string& k)
    {
        strings.erase(k); bools.erase(k); ints.erase(k); floats.erase(k);
        if (defaultStrings.count(k)) strings[k] = defaultStrings[k];
        if (defaultBools.count(k)) bools[k] = defaultBools[k];
        if (defaultInts.count(k)) ints[k] = defaultInts[k];
        if (defaultFloats.count(k)) floats[k] = defaultFloats[k];
    }
    bool saveFile() { if (fileSaved) fileSaved(); return true; }
    std::map<std::string, std::string> strings, defaultStrings;
    std::map<std::string, bool> bools, defaultBools;
    std::map<std::string, int> ints, defaultInts;
    std::map<std::string, float> floats, defaultFloats;
};
