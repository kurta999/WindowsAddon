#pragma once

// The half of the crypto price feature that is not a network call.
//
// CryptoPrice shells out to curl for four coinbase endpoints at once and gets
// back their JSON objects concatenated. Picking the four prices out of that is
// plain text handling with no dependency on the shell, the network or the
// application, so it lives here where the dependency-free test suite can reach
// it.

#include <optional>
#include <string_view>
#include <vector>

namespace crypto_price
{
// !\brief The prices one multi-request answers with, in request order.
//
// A field is empty when its object carried no usable amount; the caller keeps
// whatever it had before rather than replacing a good price with a zero.
struct Prices
{
    std::optional<float> eth_buy;
    std::optional<float> eth_sell;
    std::optional<float> btc_buy;
    std::optional<float> btc_sell;

    bool operator==(const Prices&) const = default;
};

// !\brief The marker coinbase starts every response object with.
inline constexpr std::string_view RESPONSE_SEPARATOR = R"({"data":{)";

// !\brief Splits a concatenated response on the object marker.
//
// The text before the first marker is returned as the first piece, so four
// answers produce five pieces. That is what tells a well-formed response apart
// from a shell error printed where the first object should be.
//
// The pieces point into `response`, which therefore has to outlive them.
[[nodiscard]] std::vector<std::string_view> SplitResponses(std::string_view response);

// !\brief Reads the "amount" field out of one response object.
[[nodiscard]] std::optional<float> ExtractAmount(std::string_view object);

// !\brief Whether the text is the shell complaining that curl is not installed.
[[nodiscard]] bool LooksLikeMissingCurl(std::string_view text);

// !\brief Parses a four-endpoint coinbase response.
// !\return The prices, or nothing when curl is missing or the response does
// not carry exactly four objects.
[[nodiscard]] std::optional<Prices> ParseCoinbaseResponse(std::string_view response);
}
