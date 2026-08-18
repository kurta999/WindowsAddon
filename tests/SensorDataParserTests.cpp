#include "TestFramework.hpp"

#include "SensorDataParser.hpp"

#include <string>
#include <vector>

TEST_CASE(SensorParserExtractsSignedDecimalAndScientificValues)
{
    const std::string packet = "MEAS_DATA SCD30 -1.5 2.5e+2 nan CO .75 BME680 +4 HONEYWELL";
    const auto parsed = SensorDataParser::Parse(packet.data(), packet.size());
    const std::vector<std::string> expected{"-1.5", "2.5e+2", "0.0", ".75", "+4"};

    EXPECT_TRUE(parsed.has_value());
    EXPECT_TRUE(*parsed == expected);
}

TEST_CASE(SensorParserUsesTheSuppliedBufferLength)
{
    const std::string packet = "MEAS_DATA 10 20 ignored 999";
    const size_t valid_length = packet.find(" ignored");
    const auto parsed = SensorDataParser::Parse(packet.data(), valid_length);
    const std::vector<std::string> expected{"10", "20"};

    EXPECT_TRUE(parsed.has_value());
    EXPECT_TRUE(*parsed == expected);
}

TEST_CASE(SensorParserRejectsEmptyNullAndNonNumericInput)
{
    EXPECT_FALSE(SensorDataParser::Parse(nullptr, 0).has_value());
    EXPECT_FALSE(SensorDataParser::Parse(nullptr, 5).has_value());

    const std::string packet = "MEAS_DATA BME680 HONEYWELL";
    EXPECT_FALSE(SensorDataParser::Parse(packet.data(), packet.size()).has_value());
}
