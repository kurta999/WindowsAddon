#include "pch_core.hpp"
#include "CorsairHid.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "utils/InterruptibleSleep.hpp"
#include "SettingsReader.hpp"
#include "SettingsWriter.hpp"
#include <ostream>
#include "interface/IKeySink.hpp"

using namespace std::chrono_literals;

namespace {
    constexpr int HID_READ_TIMEOUT      = 100;
    constexpr int READ_DATA_BUFFER_SIZE = 64;
    constexpr int MIN_READ_DATA_SIZE    = 20;
}

CorsairHid::~CorsairHid()
{
    DestroyWorkingThread();
}

bool CorsairHid::Init()
{
#ifdef USE_HIDAPI
    if(m_IsEnabled)
    {
        LOG(LogLevel::Notification, "CorsairHid::Init");
        if(hid_inited)
            DestroyWorkingThread();

        m_worker = utils::StartNamedWorker("CorsairHid", std::bind_front(&CorsairHid::ThreadFunc, this));
    }
    else
    {
    	LOG(LogLevel::Notification, "CorsairHid::Init - disabled");
    }
#endif
    return true;
}

void CorsairHid::SetEnabled(bool enable)
{
    m_IsEnabled = enable;
}

bool CorsairHid::IsEnabled() const
{
    return m_IsEnabled;
}

void CorsairHid::SetDebouncingInterval(const uint16_t interval)
{
    m_DebouncingInterval = interval;
}

uint16_t CorsairHid::GetDebouncingInterval() const
{
    return m_DebouncingInterval;
}

CorsairDeviceType CorsairHid::GetDeviceType() const
{
    return m_DeviceType;
}

const std::string& CorsairHid::GetDeviceName() const
{
    return m_DeviceName;
}

bool CorsairHid::IsOk() const
{
    return m_IsOk.load();
}

bool CorsairHid::ExecuteInitSequence()
{
#ifdef USE_HIDAPI
    m_IsOk = false;
    LOG(LogLevel::Notification, "CorsairHid::ExecuteInitSequence");
    int ret = hid_init();  /* Initialize the hidapi library */
    if(ret)
    {
        LOG(LogLevel::Critical, "hid_init failed");
        return false;
    }

    hid_inited = true;
    const char* hid_path = nullptr;

    hid_device_info* device_info = hid_enumerate(0, 0);  /* Enumerate over all HID devices */
    while(device_info != nullptr)
    {
        LOG(LogLevel::Normal, L"HID Device: \"{}\", VID: 0x{:X}, PID: 0x{:X}, UsagePage: 0x{:X}, Usage: 0x{:X}",
            device_info->product_string, device_info->vendor_id, device_info->product_id, device_info->usage_page, device_info->usage);
            
        if(device_info->vendor_id == 0x1B1C && device_info->product_id == 0x1B11 && device_info->usage_page == 0xFFC0 && device_info->usage == 2)  /* K95 RGB (older) with 18 macro keys */
        {
            m_DeviceType = CorsairDeviceType::K95_18GKEY;
            m_DeviceName = "Corsair K95";
            hid_path = device_info->path;
            LOG(LogLevel::Normal, "Corsair K95 18 macro key found");
            break;
        }

        if(std::wstring(device_info->product_string).find(L"Corsair Gaming K95") != std::wstring::npos)  /* Corsair Gaming K95 RGB PLATINUM Keyboard */
        {
            m_DeviceType = CorsairDeviceType::K95_PLATINUM;
            m_DeviceName = "Corsair K95 RGB PLATINUM";
            hid_path = device_info->path;
            LOG(LogLevel::Normal, "Corsair K95 Platinum found");
            break;
        }
        device_info = device_info->next;
    }

    if(hid_path != nullptr)
    {
        hid_handle = hid_open_path(hid_path);  /* Never call this function from main thread, it can block occasionally! */
        if(!hid_handle)
        {
            LOG(LogLevel::Critical, L"hid_open failed: {}", hid_error(nullptr));
            m_DeviceName = "Corsair device isn't found";
            return false;
        }
    }
    else
    {
        LOG(LogLevel::Error, "Unable to find Corsair K95");
        m_DeviceName = "Corsair device isn't found";
        return false;
    }

    m_IsOk = true;
#endif
    return true;
}

void CorsairHid::DestroyWorkingThread()
{
#ifdef USE_HIDAPI
    if(m_worker)
    {
        m_worker->request_stop();
        m_cv.notify_all();
        m_worker.reset();
    }

    if(hid_handle)
        hid_close(hid_handle);
    hid_handle = nullptr;

    if(hid_inited)
    {
        hid_exit();
        hid_inited = false;
    }
#endif
}

void CorsairHid::ThreadFunc(std::stop_token token)
{
#ifdef USE_HIDAPI
    if(!ExecuteInitSequence())
    {
        m_IsOk = false;
        return;
    }

    LOG(LogLevel::Notification, "ThreadFunc");
    bool read_error_reported = false;
    while(!token.stop_requested())
    {
        if(hid_handle)
        {
            uint8_t recv_data[READ_DATA_BUFFER_SIZE];
            int read_bytes = hid_read_timeout(hid_handle, recv_data, sizeof(recv_data), HID_READ_TIMEOUT);
            if(read_bytes < 0)
            {
                m_IsOk = false;
                if(!read_error_reported)
                {
#if defined(HID_API_VERSION) && defined(HID_API_MAKE_VERSION) && HID_API_VERSION >= HID_API_MAKE_VERSION(0, 15, 0)
                    LOG(LogLevel::Error, L"HID read error: {}", hid_read_error(hid_handle));
#else
                    LOG(LogLevel::Error, L"HID read error: {}", hid_error(hid_handle));
#endif
                    read_error_reported = true;
                }

                utils::InterruptibleSleep(m_cv, m_Mutex, token, 1000ms);  /* Back-off after error */
            }
            else
            {
                if(read_error_reported)
                    LOG(LogLevel::Notification, "Corsair HID communication recovered");

                read_error_reported = false;
                m_IsOk = true;

                if(read_bytes > MIN_READ_DATA_SIZE)
                {
                    uint32_t gkey_code = 0;
                    std::memcpy(&gkey_code, recv_data + 16, sizeof(gkey_code));
                    auto it = corsair_GKeys.find(gkey_code);
                    if(it != corsair_GKeys.end())
                        HandleKeypress(it->second);
                }
            }
                
        }

        utils::InterruptibleSleep(m_cv, m_Mutex, token, 1ms);
    }
#endif
}

void CorsairHid::HandleKeypress(const std::string& key)
{
    const auto time_now = std::chrono::steady_clock::now();
    const uint64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(time_now - last_keypress).count();
    if(elapsed > m_DebouncingInterval)
    {
        last_keypress = time_now;
        if(m_KeySink != nullptr)
            m_KeySink->OnKeyPressed(key);
    }
    else
    {
        LOG(LogLevel::Normal, "Bouncing detected, keypress has been skipped. Elapsed time (ms): {}", elapsed);
    }
}

void CorsairHid::LoadSettings(SettingsReader& reader)
{
    SetEnabled(utils::stob(reader.Required("CorsairHid", "Enable")));
    SetDebouncingInterval(utils::stoi<uint16_t>(reader.Required("CorsairHid", "DebouncingInterval")));
}

void CorsairHid::SaveSettings(std::ostream& out) const
{
    SettingsWriter(out, "CorsairHid")
        .Key("Enable", IsEnabled())
        .Key("DebouncingInterval", GetDebouncingInterval())
        .Blank();
}
