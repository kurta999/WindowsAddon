#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
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

// Validates a remote-supplied Explorer path and returns it in normalized
// backslash form, always rooted with a leading separator.
//
// The result is concatenated onto a drive letter by the caller, so anything
// that could escape that drive or alter the meaning of the resulting argument
// is rejected outright: path traversal, UNC prefixes, embedded drive letters,
// wildcards, quotes, control characters and non-ASCII bytes. Returns
// std::nullopt when the input is not safe to act on.
[[nodiscard]] std::optional<std::string> SanitizeExplorerPath(std::string_view path);

// Longest Explorer path accepted from the network.
inline constexpr std::size_t MaxExplorerPathLength = 255;
}
