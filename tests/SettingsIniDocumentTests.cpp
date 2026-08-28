#include "TestFramework.hpp"

#include "SettingsIniDocument.hpp"

#include <filesystem>
#include <string>

TEST_CASE(SettingsIniDocumentExcludesComplexEditors)
{
    const std::string input =
        "[Macro_Config]\r\nUseAdvancedKeyBinding = 1\r\n"
        "[Keys_Global]\r\nF1 = KEY_TYPE[test]\r\n"
        "[CmdExecutor]\r\nCommand = dir\r\n"
        "[BackupSettings]\r\nBackupFileFormat = format\r\n"
        "[Backup_1]\r\nFrom = source\r\n"
        "[App]\r\nMinimizeOnExit = 0\r\n";

    SettingsIniDocument document;
    std::string error;
    ASSERT_TRUE(document.Parse(input, error));
    ASSERT_EQ(document.Entries().size(), std::size_t{1});
    EXPECT_EQ(document.Entries()[0].section, std::string("App"));
    EXPECT_EQ(document.Entries()[0].key, std::string("MinimizeOnExit"));
}

TEST_CASE(SettingsIniDocumentPreservesFormattingCommentsAndExcludedSections)
{
    const std::string input =
        "# heading\n"
        "[Keys_Global]\nF1 = KEY_TYPE[unchanged]\n\n"
        "[Sensors]\nEnable = 1 # Toggle TCP server\nMeasurementForward = \n\n"
        "[Backup_1]\nFrom = C:\\source\n";

    SettingsIniDocument document;
    std::string error;
    ASSERT_TRUE(document.Parse(input, error));
    ASSERT_EQ(document.Entries().size(), std::size_t{2});

    document.Entries()[0].value = "0";
    document.Entries()[1].value = "192.168.0.2";

    EXPECT_EQ(document.Render(),
        std::string(
            "# heading\n"
            "[Keys_Global]\nF1 = KEY_TYPE[unchanged]\n\n"
            "[Sensors]\nEnable = 0 # Toggle TCP server\nMeasurementForward = 192.168.0.2\n\n"
            "[Backup_1]\nFrom = C:\\source\n"));
}

TEST_CASE(SettingsIniDocumentAcceptsEmptyValuesAndInlineSemicolonComments)
{
    const std::string input =
        "[COM_Backend]\r\nRemoteTcpIp =   ; optional destination\r\n";

    SettingsIniDocument document;
    std::string error;
    ASSERT_TRUE(document.Parse(input, error));
    ASSERT_EQ(document.Entries().size(), std::size_t{1});
    EXPECT_TRUE(document.Entries()[0].value.empty());
    EXPECT_EQ(document.Entries()[0].comment, std::string("optional destination"));

    document.Entries()[0].value = "host.example";
    EXPECT_EQ(document.Render(),
        std::string("[COM_Backend]\r\nRemoteTcpIp =   host.example; optional destination\r\n"));
}
TEST_CASE(SettingsIniDocumentSurvivesAFileRoundTrip)
{
    /* The temp-write-then-atomic-replace lived inside the settings dialog,
       where a wxDialog owned the file-durability rule and nothing could test
       it. It is the document's job now. */
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "windowshelper-inidoc-test.ini";
    const std::filesystem::path temp = path.string() + ".tmp";
    std::filesystem::remove(path);

    SettingsIniDocument original;
    std::string error;
    EXPECT_TRUE(original.Parse("[Section]\n# a comment\nKey = 1\n", error));
    EXPECT_TRUE(original.SaveToFileAtomically(path, error));

    SettingsIniDocument reloaded;
    EXPECT_TRUE(reloaded.LoadFromFile(path, error));
    EXPECT_TRUE(reloaded.Render() == original.Render());

    /* The temporary must not survive a successful replace. */
    EXPECT_TRUE(!std::filesystem::exists(temp));
    std::filesystem::remove(path);
}

TEST_CASE(SettingsIniDocumentReportsAnUnreadableFile)
{
    SettingsIniDocument document;
    std::string error;
    EXPECT_TRUE(!document.LoadFromFile("does-not-exist-anywhere.ini", error));
    EXPECT_TRUE(!error.empty());
}

