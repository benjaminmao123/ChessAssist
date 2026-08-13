#pragma once

#include <ini/ini.h>

#include <string>
#include <string_view>

// Small shared helpers for settings.ini, which multiple independent panels (ControlsPanel,
// BoardStatePanel) each own a disjoint section of and persist separately, all to the same file.
namespace SettingsIni
{
// Reads path as an INIReader if it exists, logging via logLabel and falling back to a blank
// reader on parse failure or if the file doesn't exist yet (not an error). This is the read
// half of each owner's SaveSettings(): read-merge here, UpsertEntry() this owner's own keys,
// then SaveMerged() - so as not to erase another owner's sections.
[[nodiscard]] inih::INIReader LoadOrEmpty(const std::string& path, std::string_view logLabel);

// Writes ini to path (always overwriting), logging success/failure via logLabel - the common
// write half paired with LoadOrEmpty(), with this owner's own UpsertEntry() calls in between.
void SaveMerged(const std::string& path, const inih::INIReader& ini, std::string_view logLabel);


// InsertEntry() throws if the key already exists in the section; UpdateEntry() throws if it
// doesn't. Callers that read-merge an existing file before writing (see INIWriter::write()'s
// comment) will often hit the "already exists" case, since the key is usually one a previous
// session already wrote - this picks whichever call actually applies.
template <typename T>
void UpsertEntry(inih::INIReader& ini, const std::string& section, const std::string& name, const T& value)
{
    // Keys() throws if the *section* itself doesn't exist yet, not just if the key is absent -
    // common when writing the first key into a brand-new section. Mirrors Get(section, name,
    // default)'s own try/catch for the equivalent case.
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
