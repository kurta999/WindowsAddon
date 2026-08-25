#pragma once

#include <charconv>
#include <string>

#include <format>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <thread>
#include <boost/algorithm/hex.hpp>

#include "Logger.hpp"
#include "utils/NumberParsing.hpp"

constexpr uint32_t DEFAULT_TXTCTRL_BACKGROUND = 0x00F0F0F0;

// !\brief Default text colour of a data entry, as an 0x00BBGGRR value.
// Loaders used to spell this as wxBLACK->GetRGB(), which bound XML and Modbus
// parsing to wxWidgets for the sake of a constant.
constexpr uint32_t DEFAULT_TXTCTRL_FOREGROUND = 0x00000000;

/* Braced: unbraced, the trailing else of an if/else bound to this macro's if
   rather than the caller's, and the release ran on the wrong branch. */
#define SAFE_RELEASE(name) \
	do { \
		if(name) \
		{ \
			(name)->Release(); \
			(name) = nullptr; \
		} \
	} while(0)

/* input for red: 0x00FF0000, excepted input for wxColor 0x0000FF */
#define RGB_TO_WXCOLOR(color) \
    wxColour(boost::endian::endian_reverse(color << 8))

#define WXCOLOR_TO_RGB(color) \
    boost::endian::endian_reverse(color << 8)

template <typename T, typename... Ts>
concept is_any = std::disjunction_v<std::is_same<T, Ts>...>;

namespace utils
{
    void SetThreadName(std::thread& thread, const char* threadName);
    void SetThreadName(std::jthread& thread, const char* threadName);

    // !\brief Start a worker thread already carrying the name a debugger,
    // profiler and crash dump will show for it.
    //
    // Every worker in the tree was started as a make_unique<jthread> followed by
    // a SetThreadName on the next line, half of them behind an `if(worker)` that
    // could not be false. Naming is easy to forget when it is a separate step.
    template<class Fn>
    [[nodiscard]] std::unique_ptr<std::jthread> StartNamedWorker(const char* name, Fn&& fn)
    {
        auto worker = std::make_unique<std::jthread>(std::forward<Fn>(fn));
        SetThreadName(*worker, name);
        return worker;
    }

    inline std::string extract_string(std::string& str, size_t start, size_t start_end, size_t len)
    {
        return str.substr(start + len, start_end - start - len);
    }

    std::string GetDataUnit(size_t input);
    std::string SecondsToHms(int total_seconds);
    int GetVirtualKeyFromString(const std::string& key);
    std::string GetKeyStringFromVirtualKey(int key_code);

    // !\brief Retreives selected items from file explorer
    // !\return Vector of selected items
    std::vector<std::wstring> GetSelectedItemsFromFileExplorer();

    // !\brief Get current directory path from file explorer
    // !\return Current directory path from file explorer
    std::wstring GetDestinationPathFromFileExplorer();

    std::string exec(const char* cmd);
    void ConvertHexBufferToString(const std::vector<uint8_t>& in, std::string& out);
    void ConvertHexBufferToString(const std::vector<uint16_t>& in, std::string& out);
    void ConvertHexBufferToString(const char* in, size_t len, std::string& out);

    // Decodes a hex string into `out`.
    // Returns the number of bytes written, or std::nullopt when the input is
    // not valid hex or does not fit. On failure `out` is left untouched, so
    // callers must not derive a length from the input string instead.
    template<typename T, std::size_t length>
    [[nodiscard]] std::optional<std::size_t> ConvertHexStringToBuffer(const std::string& in, std::span<T, length> out)
    {
        std::string hash;
        try
        {
            hash = boost::algorithm::unhex(in);
        }
        catch(const std::exception& e)
        {
            LOG(LogLevel::Error, "Invalid hex input \"{}\": {}", in, e.what());
            return std::nullopt;
        }

        if(hash.size() > out.size_bytes())
        {
            LOG(LogLevel::Error, "Output buffer is too small! required: {}, available: {}",
                hash.size(), out.size_bytes());
            return std::nullopt;
        }

        std::copy(hash.begin(), hash.end(), out.data());
        return hash.size();
    }

    uint32_t ColorStringToInt(const std::string& in);
    const std::string ColorIntToString(uint32_t in);

    template<const unsigned num, const char separator>
    void separate(std::string& input)
    {
        for(auto it = input.begin(); (num + 1) <= std::distance(it, input.end()); ++it)
        {
            std::advance(it, num);
            it = input.insert(it, separator);
        }
    }

    template<typename T> T random_mt(T min_val, T max_val)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<T> distr(min_val, max_val);
        return distr(gen);
    }

    std::string decode64(const std::string& val);
    std::string encode64(const std::string& val);

    uint32_t GetTickCount();

    bool SendTcpBlocking(const std::string& ip, uint16_t port, const char* data, size_t len, int timeout_ms = 300, bool skip_log_msg = false);
    std::pair<int, int> ConvertToDecimalHoursAndMinutes(const std::string& timeInput);

    constexpr unsigned long RGB_TO_INT(int r, int g, int b)
    {
        return ((r & 0xff) << 16) + ((g & 0xff) << 8) + (b & 0xff);
    }
    
    const std::string GetCurrentWifiSSID();
}
