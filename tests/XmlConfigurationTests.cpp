#include "TestFramework.hpp"

#include "XmlConfigurations.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
class XmlTempDirectory
{
public:
    XmlTempDirectory()
    {
        static std::atomic<unsigned> sequence{};
        path = std::filesystem::temp_directory_path() /
            ("WindowsHelperXmlTests_" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~XmlTempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

void WriteXml(const std::filesystem::path& path, std::string_view xml)
{
    std::ofstream output(path);
    output << xml;
}
}

TEST_CASE(XmlTxAndRxListsRoundTripWithEscaping)
{
    XmlTempDirectory temp;
    const std::vector<xml_config::TxEntry> tx{{0x123, {0x00, 0xAB, 0xFF}, 100, 2, 1, "O'Brien & <diagnostic>"}};
    const std::vector<xml_config::RxEntry> rx{{0x18DAF110, 3, "Antwort & Rückmeldung <ok>"}};
    EXPECT_TRUE(xml_config::SaveTx(temp.path / "tx.xml", tx));
    EXPECT_TRUE(xml_config::SaveRx(temp.path / "rx.xml", rx));
    EXPECT_EQ(*xml_config::LoadTx(temp.path / "tx.xml"), tx);
    EXPECT_EQ(*xml_config::LoadRx(temp.path / "rx.xml"), rx);
}

TEST_CASE(XmlDidsAndAlarmsRoundTrip)
{
    XmlTempDirectory temp;
    const std::vector<xml_config::DidEntry> dids{{0xF190, "string", "VIN & identity", "", "", 17}};
    const std::vector<xml_config::AlarmEntry> alarms{{"Wake <driver>", "Macro", "F12", "echo A&B", true}};
    EXPECT_TRUE(xml_config::SaveDids(temp.path / "dids.xml", dids));
    EXPECT_TRUE(xml_config::SaveAlarms(temp.path / "alarms.xml", alarms));
    EXPECT_EQ(*xml_config::LoadDids(temp.path / "dids.xml"), dids);
    EXPECT_EQ(*xml_config::LoadAlarms(temp.path / "alarms.xml"), alarms);
}

TEST_CASE(XmlFrameMappingsRoundTripDescriptionsAndComments)
{
    XmlTempDirectory temp;
    const std::vector<xml_config::FrameMapping> mappings{{0x321, "Powertrain <status>", 8, 'R', {
        {0, 1, "bool", "Ready", "Ready & enabled"},
        {8, 16, "uint16_t", "Speed", "km/h <scaled>\nsecond line"}
    }}};
    EXPECT_TRUE(xml_config::SaveMappings(temp.path / "mapping.xml", mappings));
    EXPECT_EQ(*xml_config::LoadMappings(temp.path / "mapping.xml"), mappings);
}

TEST_CASE(XmlLoadRejectsMissingRequiredFields)
{
    XmlTempDirectory temp;
    WriteXml(temp.path / "bad_tx.xml", "<CanUsbXml><Frame><ID>123</ID></Frame></CanUsbXml>");
    WriteXml(temp.path / "bad_alarm.xml", "<AlarmsXml><Alarm><Name>x</Name></Alarm></AlarmsXml>");
    EXPECT_FALSE(xml_config::LoadTx(temp.path / "bad_tx.xml").has_value());
    EXPECT_FALSE(xml_config::LoadAlarms(temp.path / "bad_alarm.xml").has_value());
}

TEST_CASE(XmlLoadRejectsInvalidAndDuplicateIds)
{
    XmlTempDirectory temp;
    WriteXml(temp.path / "bad_rx.xml",
        "<CanUsbRxXml><Frame><ID>XYZ</ID><Comment>x</Comment><LogLevel>1</LogLevel></Frame></CanUsbRxXml>");
    WriteXml(temp.path / "duplicate_did.xml",
        "<DidListXml>"
        "<DidEntry><ID>100</ID><Type>uint8_t</Type><Name>a</Name><Min>0</Min><Max>1</Max><Length>1</Length></DidEntry>"
        "<DidEntry><ID>100</ID><Type>uint8_t</Type><Name>b</Name><Min>0</Min><Max>1</Max><Length>1</Length></DidEntry>"
        "</DidListXml>");
    EXPECT_FALSE(xml_config::LoadRx(temp.path / "bad_rx.xml").has_value());
    EXPECT_FALSE(xml_config::LoadDids(temp.path / "duplicate_did.xml").has_value());
}

TEST_CASE(XmlLoadRejectsUnsupportedTypesDuplicateOffsetsAndOversizedMappings)
{
    XmlTempDirectory temp;
    WriteXml(temp.path / "bad_type.xml",
        "<CanFrameMapping><Frame><ID>123</ID><Name>x</Name><Size>8</Size><Direction>T</Direction>"
        "<Mapping offset='0' len='8' type='mystery' desc='x'>bad</Mapping></Frame></CanFrameMapping>");
    WriteXml(temp.path / "duplicate_offset.xml",
        "<CanFrameMapping><Frame><ID>123</ID><Name>x</Name><Size>8</Size><Direction>T</Direction>"
        "<Mapping offset='0' len='8' type='uint8_t' desc='x'>a</Mapping>"
        "<Mapping offset='0' len='1' type='bool' desc='x'>b</Mapping></Frame></CanFrameMapping>");
    WriteXml(temp.path / "oversized.xml",
        "<CanFrameMapping><Frame><ID>123</ID><Name>x</Name><Size>8</Size><Direction>T</Direction>"
        "<Mapping offset='56' len='16' type='uint16_t' desc='x'>too big</Mapping></Frame></CanFrameMapping>");
    EXPECT_FALSE(xml_config::LoadMappings(temp.path / "bad_type.xml").has_value());
    EXPECT_FALSE(xml_config::LoadMappings(temp.path / "duplicate_offset.xml").has_value());
    EXPECT_FALSE(xml_config::LoadMappings(temp.path / "oversized.xml").has_value());
}

TEST_CASE(XmlLoadRejectsUnsupportedDidTypesAndOversizedLengths)
{
    XmlTempDirectory temp;
    WriteXml(temp.path / "bad_did.xml",
        "<DidListXml><DidEntry><ID>100</ID><Type>float128</Type><Name>x</Name>"
        "<Min>0</Min><Max>1</Max><Length>5000</Length></DidEntry></DidListXml>");
    EXPECT_FALSE(xml_config::LoadDids(temp.path / "bad_did.xml").has_value());
}
