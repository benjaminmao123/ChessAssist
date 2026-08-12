#include "Settings/SettingsIni.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace
{
// Mirrors ControlsPanel/BoardStatePanel::LoadSettings()/SaveSettings(): a fresh temp path per
// test, so tests can run in parallel/any order without colliding.
std::filesystem::path TempIniPath()
{
    static int counter = 0;
    return std::filesystem::temp_directory_path() / ("settings_ini_test_" + std::to_string(++counter) + ".ini");
}

// The exact "read-merge an existing file, then upsert this owner's own keys, then write back"
// sequence ControlsPanel::SaveSettings()/BoardStatePanel::SaveSettings() each perform - the
// thing this whole file exists to cover, since a subtly wrong version of it either throws (see
// SettingsIni::UpsertEntry's comment on InsertEntry/UpdateEntry both being one-directional) or
// silently erases another owner's section (a from-scratch INIReader instead of a read-merged
// one).
void SaveSection(const std::filesystem::path& path, const std::string& section, const std::string& key, const std::string& value)
{
    inih::INIReader ini;
    if (std::filesystem::exists(path))
        ini = inih::INIReader(path.string());

    SettingsIni::UpsertEntry(ini, section, key, value);

    inih::INIWriter::write(path.string(), ini, /*overwrite=*/true);
}
}  // namespace

TEST(SettingsIniTest, UpsertInsertsWhenKeyAbsent)
{
    inih::INIReader ini;
    EXPECT_NO_THROW(SettingsIni::UpsertEntry(ini, "Section", "Key", std::string("value")));
    EXPECT_EQ(ini.Get<std::string>("Section", "Key", "MISSING"), "value");
}

TEST(SettingsIniTest, UpsertUpdatesWhenKeyAlreadyPresent)
{
    inih::INIReader ini;
    ini.InsertEntry("Section", "Key", std::string("first"));

    EXPECT_NO_THROW(SettingsIni::UpsertEntry(ini, "Section", "Key", std::string("second")));
    EXPECT_EQ(ini.Get<std::string>("Section", "Key", "MISSING"), "second");
}

TEST(SettingsIniTest, UpsertWorksForBoolAndIntToo)
{
    inih::INIReader ini;
    SettingsIni::UpsertEntry(ini, "Section", "Flag", true);
    SettingsIni::UpsertEntry(ini, "Section", "Number", 42);

    EXPECT_NO_THROW(SettingsIni::UpsertEntry(ini, "Section", "Flag", false));
    EXPECT_NO_THROW(SettingsIni::UpsertEntry(ini, "Section", "Number", 7));

    EXPECT_EQ(ini.Get<bool>("Section", "Flag", true), false);
    EXPECT_EQ(ini.Get<int>("Section", "Number", -1), 7);
}

// The scenario that actually motivated this file: two independent "owners" (ControlsPanel-like
// and BoardStatePanel-like) each read-merge-then-write the same settings.ini, repeatedly across
// simulated sessions, in alternating order. Neither should ever throw, and neither should ever
// erase the other's section - which a from-scratch (non-merged) INIReader, or plain
// InsertEntry() on a second write, would both do.
TEST(SettingsIniTest, TwoOwnersRoundTripTheSameFileAcrossSessionsWithoutClobbering)
{
    const std::filesystem::path path = TempIniPath();

    // Session 1: owner A writes first, then owner B.
    ASSERT_NO_THROW(SaveSection(path, "OwnerA", "Setting", "a1"));
    ASSERT_NO_THROW(SaveSection(path, "OwnerB", "Setting", "b1"));

    {
        const inih::INIReader check(path.string());
        EXPECT_EQ(check.Get<std::string>("OwnerA", "Setting", "MISSING"), "a1");
        EXPECT_EQ(check.Get<std::string>("OwnerB", "Setting", "MISSING"), "b1");
    }

    // Session 2: same keys already exist in the file (the InsertEntry-throws-on-duplicate
    // case), and the write order is reversed (owner B first this time).
    ASSERT_NO_THROW(SaveSection(path, "OwnerB", "Setting", "b2"));
    ASSERT_NO_THROW(SaveSection(path, "OwnerA", "Setting", "a2"));

    {
        const inih::INIReader check(path.string());
        EXPECT_EQ(check.Get<std::string>("OwnerA", "Setting", "MISSING"), "a2");
        EXPECT_EQ(check.Get<std::string>("OwnerB", "Setting", "MISSING"), "b2");
    }

    std::filesystem::remove(path);
}
