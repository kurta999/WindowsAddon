#pragma once

#include <future>
#include <atomic>

// !\brief The two coin prices the main page shows, refreshed on a timer.
//
// Two collaborators reach it: the frame's tick, which asks whether new prices
// have arrived, and the panel's Update button. Both are handed it.
class Settings;

class CryptoPrice
{
public:
    // !\brief How often to refresh, in minutes, and zero to switch the feature
    // off. It is one value in [App], read three times on the update path, and
    // it used to come from the settings singleton on each of them.
    void SetSettings(const Settings& settings) noexcept { m_Settings = &settings; }
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

    // !\brief Takes what curl printed and stores the prices it carries.
    // Public so the response handling can be exercised without a network.
    void ApplyResponse(std::string_view response);

private:
    const Settings* m_Settings = nullptr;

    // !\brief The configured interval, or zero when nothing supplied one -
    // which reads as "switched off", the same as the setting's own zero.
    [[nodiscard]] std::uint16_t UpdateIntervalMinutes() const;

    // !\brief Get coin price from web API
    void ExecuteApiRead();

    // !\brief Timepoint for last update
    std::chrono::steady_clock::time_point last_update;

    std::atomic<float> eth_buy;
    std::atomic<float> eth_sell;
    std::atomic<float> btc_buy;
    std::atomic<float> btc_sell;
    std::atomic<bool>  is_pending{ false };

    /* Declared last on purpose. Members are destroyed in reverse order and this
       future's destructor blocks until the read finishes, so everything the
       read touches has to outlive it. */
    // !\brief Future for executing async crypto price reading
    std::future<void> m_api_future;
};