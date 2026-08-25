#include "pch_core.hpp"
#include "SerialPortBase.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "utils/InterruptibleSleep.hpp"

using namespace std::chrono_literals;

constexpr auto SERIAL_EXCEPTION_DELAY = 1000ms;

SerialPortBase::~SerialPortBase()
{
    DeInitInternal();
}

void SerialPortBase::InitInternal(const std::string& serial_name, std::chrono::milliseconds main_timeout, std::chrono::milliseconds exception_timeout,
    SerialRecvFunction recv_function, SerialSendFunction send_function, uint32_t baudrate, bool auto_open)
{
    m_SerialName = serial_name;
    m_IsAutoOpen = auto_open;
    m_MainTimeout = main_timeout;
    m_ExceptionTimeout = exception_timeout;
    m_RecvFunction = recv_function;
    m_SendFunction = send_function;
    m_Baudrate = baudrate;

    if(!m_worker)
    {
        m_worker = utils::StartNamedWorker(m_SerialName.c_str(),
            std::bind_front(&SerialPortBase::WorkerThread, this));
    }
}

/* A second InitInternal overload taking (ip, port, auto_open, ...) was here.
   Nothing called it - TCP settings reach a port through SetTcp, SetTcpIp and
   SetTcpPort - and it carried a bug that could therefore never fire: it did
   not set m_SerialName, so the worker it started would have been named by
   whatever the string happened to hold. */

void SerialPortBase::DeInitInternal()
{
    DestroyWorkerThread();
}

void SerialPortBase::SetEnabled(bool enable)
{
    is_enabled = enable;
}

bool SerialPortBase::IsEnabled() const
{
    return is_enabled;
}

void SerialPortBase::SetTcp(bool is_tcp_)
{
    is_tcp = is_tcp_;
}

bool SerialPortBase::IsTcp() const
{
    return is_tcp;
}

void SerialPortBase::SetTcpIp(const std::string& ip)
{
    m_TcpIp = ip;
}

const std::string& SerialPortBase::GetTcpIp() const
{
    return m_TcpIp;
}

void SerialPortBase::SetTcpPort(uint16_t port)
{
    m_TcpPort = port;
}

uint16_t SerialPortBase::GetTcpPort() const
{
    return m_TcpPort;
}

SerialPortConnectionState SerialPortBase::GetTransportConnectionStateLocked() const
{
    if(!m_serial)
        return SerialPortConnectionState::Disconnected;
    if(m_serial->isOpen())
        return SerialPortConnectionState::Connected;
    if(m_serial->errorStatus())
        return SerialPortConnectionState::Error;
    return SerialPortConnectionState::Disconnected;
}

void SerialPortBase::PublishTransportConnectionStateLocked()
{
    m_connectionStatus.Store(GetTransportConnectionStateLocked());
}

SerialPortConnectionState SerialPortBase::GetConnectionState() const
{
    return ProbeSerialPortConnectionStatusNonBlocking(m_serialMutex, m_connectionStatus,
        [this]() { return GetTransportConnectionStateLocked(); });
}

bool SerialPortBase::IsOpen()
{
    return GetConnectionState() == SerialPortConnectionState::Connected;
}

void SerialPortBase::Open()
{
    std::scoped_lock lock(m_serialMutex);
    if(!m_serialConstructed)
    {
        PublishTransportConnectionStateLocked();
        DBG("[SERIAL] Open skipped name=%s constructed=0 tcp=%d\n", m_SerialName.c_str(), is_tcp ? 1 : 0);
        return;  /* Temporary quick solution to avoid multiple thread related problems */
    }

    m_connectionStatus.Store(SerialPortConnectionState::Connecting);
    try
    {
        DBG("[SERIAL] Open begin name=%s tcp=%d ip=%s port=%u com=%u\n", m_SerialName.c_str(), is_tcp ? 1 : 0,
            m_TcpIp.c_str(), static_cast<unsigned>(m_TcpPort), static_cast<unsigned>(com_port));
        if (!is_tcp)
        {
#ifdef _WIN32
            m_serial->open("\\\\.\\COM" + std::to_string(com_port), m_Baudrate);
#else
            m_serial->open("/dev/ttyUSB" + std::to_string(com_port), m_Baudrate);
#endif
        }
        else
        {
            m_serial->open(m_TcpIp, m_TcpPort);
        }
        PublishTransportConnectionStateLocked();
        DBG("[SERIAL] Open end name=%s isOpen=%d err=%d\n", m_SerialName.c_str(), m_serial && m_serial->isOpen() ? 1 : 0,
            m_serial && m_serial->errorStatus() ? 1 : 0);
    }
    catch(const std::exception& e)
    {
        m_is_ok = false;
        m_connectionStatus.Store(SerialPortConnectionState::Error);
        DBG("[SERIAL] Open exception name=%s what=%s\n", m_SerialName.c_str(), e.what());
        LOG(LogLevel::Error, "Failed to open {}: {}", m_SerialName, e.what());
    }
    catch(...)
    {
        m_is_ok = false;
        m_connectionStatus.Store(SerialPortConnectionState::Error);
        DBG("[SERIAL] Open unknown exception name=%s\n", m_SerialName.c_str());
        LOG(LogLevel::Error, "Failed to open {}: unknown transport error", m_SerialName);
    }
}

void SerialPortBase::Close()
{
    try
    {
        std::scoped_lock lock(m_serialMutex);
        DBG("[SERIAL] Close begin name=%s hasSerial=%d open=%d\n", m_SerialName.c_str(), m_serial ? 1 : 0,
            m_serial && m_serial->isOpen() ? 1 : 0);
        if(m_serial)
            m_serial->close();
        PublishTransportConnectionStateLocked();
        DBG("[SERIAL] Close end name=%s hasSerial=%d open=%d\n", m_SerialName.c_str(), m_serial ? 1 : 0,
            m_serial && m_serial->isOpen() ? 1 : 0);
    }
    catch(const std::exception& e)
    {
        m_connectionStatus.Store(SerialPortConnectionState::Error);
        DBG("[SERIAL] Close exception name=%s what=%s\n", m_SerialName.c_str(), e.what());
		LOG(LogLevel::Error, "Exception {} serial close {}", m_SerialName, e.what());
	}
}

void SerialPortBase::SetComPort(uint16_t port)
{
    com_port = port;
}

uint16_t SerialPortBase::GetComPort() const
{
    return com_port;
}

void SerialPortBase::SetBaudrate(uint32_t baudrate)
{
    m_Baudrate = baudrate;
}

uint32_t SerialPortBase::GetBaudrate() const
{
    return m_Baudrate;
}

bool SerialPortBase::IsOk() const
{
    return m_is_ok;
}

bool SerialPortBase::IsErrorPresent() const
{
    return GetConnectionState() == SerialPortConnectionState::Error;
}

void SerialPortBase::NotifiyMainThread()
{
    is_notification_pending = true;
    m_cv.notify_all();
}

void SerialPortBase::DestroyWorkerThread()
{
    if(m_worker)
    {
        DBG("[SERIAL] DestroyWorkerThread requesting stop name=%s\n", m_SerialName.c_str());
        m_worker->request_stop();
        m_cv.notify_all();
        m_worker->join();
        m_worker.reset();
        is_notification_pending = false;
        DBG("[SERIAL] DestroyWorkerThread joined name=%s\n", m_SerialName.c_str());
    }
}

bool SerialPortBase::IsInstanceInited()
{
    std::scoped_lock lock(m_serialMutex);
    return m_serial != nullptr && m_serialConstructed != 0;
}

void SerialPortBase::WorkerThread(std::stop_token token)
{
    while(!token.stop_requested())
    {
        std::string err_msg;
        try
        {
            {
                std::scoped_lock serial_lock(m_serialMutex);
                m_serialConstructed = 0;
                if (!is_tcp)
                {
#ifdef _WIN32
                    m_serial = std::make_unique<CallbackAsyncSerial>("\\\\.\\COM" + std::to_string(com_port), m_Baudrate);
#else
                    m_serial = std::make_unique<CallbackAsyncSerial>("/dev/ttyUSB" + std::to_string(com_port), m_Baudrate);
#endif
                }
                else
                {
                    m_serial = std::make_unique<CallbackAsyncSerial>(m_TcpIp, m_TcpPort);
                }
                m_serial->setCallback(m_RecvFunction);
                m_serialConstructed = 1;
                PublishTransportConnectionStateLocked();
            }

            while(!token.stop_requested())
            {
                {
                    std::scoped_lock serial_lock(m_serialMutex);
                    PublishTransportConnectionStateLocked();
                    if(m_serial && m_serial->errorStatus() && m_serial->isOpen() == false)
                    {
                        LOG(LogLevel::Error, "Serial port \"{}\" unexpectedly closed", m_SerialName);
                        m_is_ok = false;
                        /*
                        if(m_IsAutoOpen)
                            break;
                            */
                        break;
                    }
                }

                {
                    std::scoped_lock serial_lock(m_serialMutex);
                    if(!m_serial)
                    {
                        m_is_ok = false;
                        break;
                    }
                }

                m_is_ok = true;
                {
                    std::unique_lock lock(m_mutex);
                    auto now = std::chrono::steady_clock::now();
                    bool ret = m_cv.wait_until(lock, token, now + m_MainTimeout, [this]() { return is_notification_pending != 0; });
                    /*
                    if(!ret)
                    {
                        LOG(LogLevel::Error, "{} - CV timeout", m_SerialName);
                    }
                    */
                }

                if(token.stop_requested())
                    break;

                const bool should_send = is_notification_pending.exchange(false);
                if(should_send && m_SendFunction)
                {
                    std::scoped_lock serial_lock(m_serialMutex);
                    DBG("[SERIAL] Worker send callback name=%s hasSerial=%d open=%d err=%d notified=%d\n", m_SerialName.c_str(),
                        m_serial ? 1 : 0, m_serial && m_serial->isOpen() ? 1 : 0, m_serial && m_serial->errorStatus() ? 1 : 0,
                        should_send ? 1 : 0);
                    if(m_serial)
                        m_SendFunction(*m_serial);
                    PublishTransportConnectionStateLocked();
                }
            }
            try
            {
                m_is_ok = false;
                std::scoped_lock serial_lock(m_serialMutex);
                if(m_IsAutoOpen && m_serial)
                    m_serial->close();
                PublishTransportConnectionStateLocked();
            }
            catch(const std::exception& e)
            {
                LOG(LogLevel::Error, "Exception {} serial close {}", m_SerialName, e.what());
            }
        }
        catch(const std::exception& e)
        {
            LOG(LogLevel::Error, "Exception {} serial {}", m_SerialName, e.what());
            m_is_ok = false;
            m_connectionStatus.Store(SerialPortConnectionState::Error);

            utils::InterruptibleSleep(m_cv, m_mutex, token, m_ExceptionTimeout);
        }
    }

}
