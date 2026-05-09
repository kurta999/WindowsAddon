#pragma once

#include "utils/CSingleton.hpp"
#include <future>
#include <atomic>

class CryptoPrice : public CSingleton < CryptoPrice >
{
    friend class CSingleton < CryptoPrice >;

public:
    CryptoPrice() = default;
    ~CryptoPrice() = default;

    // !\brief Update prices
    // !\param force [in] Force update even if timeout hasn't happened
    void UpdatePrices(bool force = false);

    float GetEthBuy()  const { return eth_buy.load(); }
    float GetEthSell() const { return eth_sell.load(); }
    float GetBtcBuy()  const { return btc_buy.load(); }
    float GetBtcSell() const { return btc_sell.load(); }

    // !\brief Returns true and clears the pending flag if new data has arrived
    bool ConsumePending() { return is_pending.exchange(false); }

private:
    // !\brief Get coin price from web API
    void ExecuteApiRead();

    // !\brief Timepoint for last update
    std::chrono::steady_clock::time_point last_update;

    // !\brief Future for executing async crypto price reading
    std::future<void> m_api_future;

    std::atomic<float> eth_buy;
    std::atomic<float> eth_sell;
    std::atomic<float> btc_buy;
    std::atomic<float> btc_sell;
    std::atomic<bool>  is_pending{ false };
};