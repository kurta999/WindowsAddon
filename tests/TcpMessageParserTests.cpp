#include "TestFramework.hpp"

#include "TcpMessageParser.hpp"

#include <algorithm>
#include <array>
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
