#include "TestFramework.hpp"

#include "TcpMessageParser.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

TEST_CASE(TcpMessageParserHandlesExactCapacityWithoutTerminator)
{
    std::array<char, tcp_message::MaxMessageSize> buffer{};
    std::fill(buffer.begin(), buffer.end(), 'x');
    constexpr std::string_view prefix = "expw/share/path";
    std::copy(prefix.begin(), prefix.end(), buffer.begin());

    const auto message = tcp_message::BoundedMessage(buffer, buffer.size());
    EXPECT_EQ(message.size(), buffer.size());
    EXPECT_EQ(buffer.back(), 'x');

    const auto parsed = tcp_message::Parse({message.data(), message.size()});
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->command, tcp_message::Command::OpenExplorer);
    EXPECT_EQ(parsed->argument.size(), buffer.size() - std::string_view("expw").size());
}

TEST_CASE(TcpMessageParserClampsImpossibleTransferCounts)
{
    std::array<char, 8> buffer{};
    EXPECT_EQ(tcp_message::BoundedMessage(buffer, 0).size(), std::size_t{0});
    EXPECT_EQ(tcp_message::BoundedMessage(buffer, 8).size(), std::size_t{8});
    EXPECT_EQ(tcp_message::BoundedMessage(buffer, 9).size(), std::size_t{8});
}

TEST_CASE(TcpMessageParserRecognizesOnlyCompletePrefixes)
{
    EXPECT_FALSE(tcp_message::Parse("exp").has_value());
    EXPECT_FALSE(tcp_message::Parse("GET /").has_value());

    const auto measurements = tcp_message::Parse("MEAS_DATA 1 2 3");
    ASSERT_TRUE(measurements.has_value());
    EXPECT_EQ(measurements->command, tcp_message::Command::Measurements);

    const auto graph = tcp_message::Parse("GET /CO2 HTTP/1.1\r\n\r\n");
    ASSERT_TRUE(graph.has_value());
    EXPECT_EQ(graph->command, tcp_message::Command::Graph);
    EXPECT_EQ(graph->argument, std::string_view("CO2.html"));
}

TEST_CASE(ExplorerPathRejectsTraversalAndUncAndDriveLetters)
{
    /* Anything that could leave the mapped share, or smuggle a second argument
       past the shell, has to be refused outright. */
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("/work/..").has_value());
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("/work/../../windows").has_value());
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("..").has_value());
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("\\\\attacker\\\\share").has_value());
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("//attacker/share").has_value());
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("/work\" C:/windows/system32").has_value());
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("/work C:/windows").has_value());
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("/work/*").has_value());
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("/work/?").has_value());
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("/work\r\nGET /CO2").has_value());
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath(
        std::string_view("/work\0payload", 13)).has_value());
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("/\xE9").has_value());
}

TEST_CASE(ExplorerPathRejectsEmptyOversizedAndSeparatorOnlyInput)
{
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("").has_value());
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("/").has_value());
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("///").has_value());
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("/.").has_value());

    const std::string oversized(tcp_message::MaxExplorerPathLength + 1, 'a');
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath(oversized).has_value());

    /* Trailing dots and spaces are stripped by Win32, so two distinct requests
       would otherwise resolve to the same directory. */
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("/work/name.").has_value());
    EXPECT_FALSE(tcp_message::SanitizeExplorerPath("/work/name ").has_value());
}

TEST_CASE(ExplorerPathNormalizesSeparatorsAndRootsTheResult)
{
    const auto simple = tcp_message::SanitizeExplorerPath("/home/user/project");
    ASSERT_TRUE(simple.has_value());
    EXPECT_EQ(*simple, std::string("\\home\\user\\project"));

    /* Drive-relative input becomes rooted, so "Z:" + result is unambiguous. */
    const auto relative = tcp_message::SanitizeExplorerPath("home/user");
    ASSERT_TRUE(relative.has_value());
    EXPECT_EQ(*relative, std::string("\\home\\user"));

    const auto redundant = tcp_message::SanitizeExplorerPath("//home///user//");
    EXPECT_FALSE(redundant.has_value());

    const auto dotted = tcp_message::SanitizeExplorerPath("/home/./user");
    ASSERT_TRUE(dotted.has_value());
    EXPECT_EQ(*dotted, std::string("\\home\\user"));

    /* The sender appends a newline; it must not defeat validation. */
    const auto trailing = tcp_message::SanitizeExplorerPath("/home/user\r\n");
    ASSERT_TRUE(trailing.has_value());
    EXPECT_EQ(*trailing, std::string("\\home\\user"));
}
