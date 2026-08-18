#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace tcp_message
{
inline constexpr std::size_t MaxMessageSize = 1024;

enum class Command
{
    Measurements,
    OpenExplorer,
    Graph
};

struct ParsedMessage
{
    Command command;
    std::string_view argument;

    bool operator==(const ParsedMessage&) const = default;
};

// Returns only bytes that actually fit in the receive buffer. The result is
// deliberately not NUL-terminated: every downstream parser must honor length.
[[nodiscard]] std::span<char> BoundedMessage(std::span<char> buffer,
                                             std::size_t transferred_bytes) noexcept;

// Classifies one complete TCP request without reading past message.size().
[[nodiscard]] std::optional<ParsedMessage> Parse(std::string_view message) noexcept;
}
