#include "CryptoPriceParser.hpp"

#include <charconv>

namespace crypto_price
{
namespace
{
constexpr std::string_view AMOUNT_FIELD = R"("amount":")";

// What cmd.exe prints when the executable is not on PATH.
constexpr std::string_view MISSING_COMMAND_MARKER = "not recognized";
}

std::vector<std::string_view> SplitResponses(std::string_view response)
{
    std::vector<std::string_view> pieces;
    std::size_t begin = 0;
    for(;;)
    {
        const std::size_t found = response.find(RESPONSE_SEPARATOR, begin);
        if(found == std::string_view::npos)
        {
            pieces.push_back(response.substr(begin));
            return pieces;
        }
        pieces.push_back(response.substr(begin, found - begin));
        begin = found + RESPONSE_SEPARATOR.size();
    }
}

std::optional<float> ExtractAmount(std::string_view object)
{
    const std::size_t found = object.find(AMOUNT_FIELD);
    if(found == std::string_view::npos)
        return std::nullopt;

    const std::string_view number = object.substr(found + AMOUNT_FIELD.size());
    float amount = 0.0f;
    const auto parsed = std::from_chars(number.data(), number.data() + number.size(), amount);
    if(parsed.ec != std::errc{})
        return std::nullopt;
    return amount;
}

bool LooksLikeMissingCurl(std::string_view text)
{
    return text.find(MISSING_COMMAND_MARKER) != std::string_view::npos;
}

std::optional<Prices> ParseCoinbaseResponse(std::string_view response)
{
    const auto pieces = SplitResponses(response);

    /* Anything before the first object is either empty or an error the shell
       printed instead of running curl. */
    if(pieces.empty() || LooksLikeMissingCurl(pieces.front()))
        return std::nullopt;

    /* Four requests, so four objects plus the text in front of the first one. */
    if(pieces.size() != 5)
        return std::nullopt;

    Prices prices;
    prices.eth_buy = ExtractAmount(pieces[1]);
    prices.eth_sell = ExtractAmount(pieces[2]);
    prices.btc_buy = ExtractAmount(pieces[3]);
    prices.btc_sell = ExtractAmount(pieces[4]);
    return prices;
}
}
