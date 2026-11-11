#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// A route is a list of (press time, release time) in seconds of level time.
using Holds = std::vector<std::pair<double, double>>;

struct MacroInfo {
    Holds holds;
    uint32_t levelID = 0;
    std::string levelName;
    std::string format;
};

// Loads a bot macro (.gdr2 binary, or .gdr v1 in JSON or msgpack form).
// Returns an empty string on success, otherwise an error message.
std::string loadMacro(std::filesystem::path const& path, MacroInfo& out);
