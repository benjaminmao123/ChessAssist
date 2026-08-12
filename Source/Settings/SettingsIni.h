#pragma once

#include <ini/ini.h>

#include <string>
#include <string_view>

// Small shared helpers for settings.ini, which multiple independent panels (ControlsPanel,
// BoardStatePanel) each own a disjoint section of and persist separately, all to the same file.
namespace SettingsIni
{
// Reads path as an INIReader if it exists, logging (via logLabel, e.g. "BoardStatePanel::
// SaveSettings") and falling back to a blank reader on any parse failure or if the file simply
// doesn't exist yet (a fresh install/deleted file, not an error). The common read half of every
// settings.ini owner's SaveSettings(): read-merge the existing file first (via this), then
// UpsertEntry() this owner's own keys onto it, then SaveMerged() - so as not to erase another
// owner's sections when INIWriter::write() later writes out only what's in the INIReader object
// it's given.
[[nodiscard]] inih::INIReader LoadOrEmpty(const std::string& path, std::string_view logLabel);

// Writes ini to path (always overwriting), logging success/failure via logLabel - the common
// write half paired with LoadOrEmpty(), with this owner's own UpsertEntry() calls in between.
void SaveMerged(const std::string& path, const inih::INIReader& ini, std::string_view logLabel);


// inih::INIReader::InsertEntry() throws if the key already exists in the section, and
// UpdateEntry() throws if it doesn't - callers that read-merge an existing file before writing
// (so as not to erase another owner's sections in the same file - see INIWriter::write()'s own
// comment: it always writes out exactly and only what's in the INIReader object it's given)
// will very often hit exactly the "already exists" case, since the key they're about to set is
// usually the same one a previous session already wrote. This picks whichever of the two
// actually applies instead of ever risking the throwing one.
template <typename T>
void UpsertEntry(inih::INIReader& ini, const std::string& section, const std::string& name, const T& value)
{
    // Keys() itself throws if the *section* doesn't exist at all yet (not just if the key is
    // absent within an existing section) - exactly the case on the very first key ever written
    // into a brand-new section, which is common (e.g. this whole section not existing yet in a
    // freshly read-merged file). Mirrors Get(section, name, default)'s own try/catch around
    // the equivalent "not found" case, rather than only handling the key-missing half.
    bool exists = false;
    try
    {
        exists = ini.Keys(section).count(name) > 0;
    }
    catch (const std::runtime_error&)
    {
        exists = false;
    }

    if (exists)
        ini.UpdateEntry(section, name, value);
    else
        ini.InsertEntry(section, name, value);
}
}  // namespace SettingsIni
