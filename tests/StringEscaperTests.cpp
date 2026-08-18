#include "TestFramework.hpp"

#include "StringToCEscaper.hpp"

#include <string>

TEST_CASE(StringEscaperEscapesCControlCharactersAndPrintfPercent)
{
    std::string input = "\"100%\"\r\nC:\\tmp";
    StringEscaper{}.EscapeString(input, true, true);

    EXPECT_EQ(input, std::string("\\\"100%%\\\"\\\r\nC:\\\\tmp"));
}

TEST_CASE(StringEscaperCanLeavePercentAndLineEndingsUnchanged)
{
    std::string input = "value=%\r\n";
    StringEscaper{}.EscapeString(input, false, false);

    EXPECT_EQ(input, std::string("value=%\r\n"));
}
