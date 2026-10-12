#pragma once

#include <string>

namespace FamilyPreferences {
void initialize();
void save();
bool switchTo(const std::string& id);
bool restartRequested();
void restartFrontend();
void frontendReady();
} // namespace FamilyPreferences
