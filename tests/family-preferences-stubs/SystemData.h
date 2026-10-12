#pragma once
#include <map>
#include <string>
class SystemData {
public:
    static std::map<std::string, std::string> getKnownSystemNames()
    { return {{"arcade", "Arcade"}, {"ps2", "PlayStation 2"}}; }
};
