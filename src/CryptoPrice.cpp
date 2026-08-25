#include "pch_core.hpp"
#include "CryptoPrice.hpp"
#include "Settings.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "CryptoPriceParser.hpp"
#include "utils/WindowsCommand.hpp"

namespace
{
constexpr const wchar_t* COINBASE_CURL_ARGUMENTS =
    L"/C curl --silent https://api.coinbase.com/v2/prices/ETH-USD/buy"
    L" -: https://api.coinbase.com/v2/prices/ETH-USD/sell"
    L" -: https://api.coinbase.com/v2/prices/BTC-USD/buy"
    L" -: https://api.coinbase.com/v2/prices/BTC-USD/sell";

constexpr const char* COINBASE_CURL_COMMAND =
    "curl https://api.coinbase.com/v2/prices/ETH-USD/buy"
    " -: https://api.coinbase.com/v2/prices/ETH-USD/sell"
    " -: https://api.coinbase.com/v2/prices/BTC-USD/buy"
    " -: https://api.coinbase.com/v2/prices/BTC-USD/sell";
}

void CryptoPrice::ApplyResponse(std::string_view response)
{
    /* Splitting the answer and picking the amounts out of it is plain text
       handling, so it lives in crypto_price where it can be tested without a
       shell or a network. */
    const auto prices = crypto_price::ParseCoinbaseResponse(response);
    if(!prices)
    {
        if(crypto_price::LooksLikeMissingCurl(response))
            LOG(LogLevel::Error, "curl is not found on the system, coin price requests won't work!");
        else
            LOG(LogLevel::Error, "Unexpected coin price response from curl: {}", response);
        return;
    }

    /* A field the response did not carry keeps the price we already had. */
    if(prices->eth_buy)  eth_buy  = *prices->eth_buy;
    if(prices->eth_sell) eth_sell = *prices->eth_sell;
    if(prices->btc_buy)  btc_buy  = *prices->btc_buy;
    if(prices->btc_sell) btc_sell = *prices->btc_sell;
    is_pending = true;

    LOG(LogLevel::Notification, "Coin price successfully retrieved! Buy, Sell - ETH: {}, {}, BTC: {}, {}",
        eth_buy.load(), eth_sell.load(), btc_buy.load(), btc_sell.load());
}

void CryptoPrice::ExecuteApiRead()
{
    if(UpdateIntervalMinutes() == 0)
        return;

#ifdef _WIN32
    const CStringA response = utils::ExecuteCmdWithoutWindow(COINBASE_CURL_ARGUMENTS, 5000);
    ApplyResponse(std::string_view(response.GetString(), static_cast<std::size_t>(response.GetLength())));
#else
    ApplyResponse(utils::exec(COINBASE_CURL_COMMAND));
#endif

    last_update = std::chrono::steady_clock::now();
}

std::uint16_t CryptoPrice::UpdateIntervalMinutes() const
{
    return m_Settings != nullptr ? m_Settings->crypto_price_update : 0;
}

void CryptoPrice::UpdatePrices(bool force)
{
    if(!UpdateIntervalMinutes())
    {
        last_update = std::chrono::steady_clock::now();
        return;
    }

    uint64_t dif = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - last_update).count();
    if(dif > (UpdateIntervalMinutes() * 60) || force)
    {
        if(m_api_future.valid())
            if(m_api_future.wait_for(std::chrono::nanoseconds(1)) != std::future_status::ready)
                return;
        m_api_future = std::async(std::launch::async, &CryptoPrice::ExecuteApiRead, this);
    }
}
