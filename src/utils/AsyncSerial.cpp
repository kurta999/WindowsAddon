/*
 * File:   AsyncSerial.cpp
 * Author: Terraneo Federico
 * Distributed under the Boost Software License, Version 1.0.
 * Created on September 7, 2009, 10:46 AM
 *
 * v1.03: C++11 support
 *
 * v1.02: Fixed a bug in BufferedAsyncSerial: Using the default constructor
 * the callback was not set up and reading didn't work.
 *
 * v1.01: Fixed a bug that did not allow to reopen a closed serial port.
 *
 * v1.00: First release.
 *
 * IMPORTANT:
 * On Mac OS X boost asio's serial ports have bugs, and the usual implementation
 * of this class does not work. So a workaround class was written temporarily,
 * until asio (hopefully) will fix Mac compatibility for serial ports.
 * 
 * Please note that unlike said in the documentation on OS X until asio will
 * be fixed serial port *writes* are *not* asynchronous, but at least
 * asynchronous *read* works.
 * In addition the serial port open ignores the following options: parity,
 * character size, flow, stop bits, and defaults to 8N1 format.
 * I know it is bad but at least it's better than nothing.
 *
 */

#include "AsyncSerial.hpp"

#include <string>
#include <algorithm>
#include <atomic>
#include <thread>
#include <mutex>
#include <boost/bind/bind.hpp>
#include <boost/shared_array.hpp>
#include <boost/scoped_ptr.hpp>

#ifdef _WIN32
#include <Windows.h>
#endif

//#include "../Logger.hpp"

using namespace std;
using namespace boost;

//
//Class AsyncSerial
//

class AsyncSerialImpl: private boost::noncopyable
{
public:
    AsyncSerialImpl(): is_tcp(false), tcp_port(0), io(), port(io), backgroundThread(), open(false),
            error(false)
    {
#ifdef _WIN32
        OutputDebugStringA("~AsyncSerialImpl");
#endif
    }

    bool is_tcp;
    std::string tcp_ip;
    uint16_t tcp_port;

    boost::asio::io_context io; ///< Io service object
    boost::scoped_ptr<boost::asio::ip::tcp::socket> socket;
    boost::asio::serial_port port; ///< Serial port object
    std::thread backgroundThread; ///< Thread that runs read/write operations
    std::atomic_bool open; ///< True if port open
    bool error; ///< Error flag
    mutable std::mutex errorMutex; ///< Mutex for access to error

    /// Data are queued here before they go in writeBuffer
    std::vector<char> writeQueue;
    boost::shared_array<char> writeBuffer; ///< Data being written
    size_t writeBufferSize; ///< Size of writeBuffer
    std::mutex writeQueueMutex; ///< Mutex for access to writeQueue
    char readBuffer[AsyncSerial::readBufferSize]; ///< data being read

    /// Read complete callback
    std::function<void (const char*, size_t)> callback;
};

namespace
{
void JoinBackgroundThread(std::thread& thread) noexcept
{
    if(!thread.joinable())
        return;

    try
    {
        thread.join();
    }
    catch(...)
    {
        try
        {
            thread.detach();
        }
        catch(...)
        {
        }
    }
}
}

AsyncSerial::AsyncSerial(): pimpl(new AsyncSerialImpl)
{

}

AsyncSerial::AsyncSerial(const std::string& ip, uint16_t port)
        : pimpl(new AsyncSerialImpl)
{
    (void)ip;
    (void)port;
    //open(ip, port);
#ifdef _WIN32
    OutputDebugStringA("open");
#endif
}

AsyncSerial::AsyncSerial(const std::string& devname, unsigned int baud_rate,
        asio::serial_port_base::parity opt_parity,
        asio::serial_port_base::character_size opt_csize,
        asio::serial_port_base::flow_control opt_flow,
        asio::serial_port_base::stop_bits opt_stop)
        : pimpl(new AsyncSerialImpl)
{
    open(devname,baud_rate,opt_parity,opt_csize,opt_flow,opt_stop);
}

void AsyncSerial::StartIoThread()
{
    /* The tail both open paths ended with, verbatim: give the io_context its
       first piece of work, run it on the background thread, and only then
       declare the port open and error-free. */
    boost::asio::post(pimpl->io, [this]() {
        doRead();
        });

    std::thread t([this]() {
        try
        {
            pimpl->io.run();
        }
        catch(...)
        {
            pimpl->open = false;
            setErrorStatus(true);
        }
    });
    pimpl->backgroundThread.swap(t);
    setErrorStatus(false);//If we get here, no error
    pimpl->open = true; //Port is now open
}

void AsyncSerial::open(const std::string& devname, unsigned int baud_rate,
        asio::serial_port_base::parity opt_parity,
        asio::serial_port_base::character_size opt_csize,
        asio::serial_port_base::flow_control opt_flow,
        asio::serial_port_base::stop_bits opt_stop)
{
    pimpl->is_tcp = false;
    if(isOpen()) close();
    JoinBackgroundThread(pimpl->backgroundThread);

    setErrorStatus(true);//If an exception is thrown, error_ remains true
    pimpl->port.open(devname);
    pimpl->port.set_option(asio::serial_port_base::baud_rate(baud_rate));
    pimpl->port.set_option(opt_parity);
    pimpl->port.set_option(opt_csize);
    pimpl->port.set_option(opt_flow);
    pimpl->port.set_option(opt_stop);

    StartIoThread();
}

void AsyncSerial::open(const std::string& ip, uint16_t port)
{
    try
    {
        pimpl->is_tcp = true;
        if(isOpen()) close();
        JoinBackgroundThread(pimpl->backgroundThread);

        setErrorStatus(true);//If an exception is thrown, error_ remains true
        pimpl->tcp_ip = ip;
        pimpl->tcp_port = port;

        boost::system::error_code ec;
        const auto address = boost::asio::ip::make_address(ip, ec);
        if(ec)
        {
            pimpl->open = false;
            return;
        }

        boost::asio::ip::tcp::endpoint endpoint(address, port);
        pimpl->socket.reset(new boost::asio::ip::tcp::socket(pimpl->io));
        pimpl->socket->set_option(boost::asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>{ 2000 }, ec);
        pimpl->socket->set_option(boost::asio::detail::socket_option::integer<SOL_SOCKET, SO_SNDTIMEO>{ 2000 }, ec);

        auto is_connected = std::make_shared<std::atomic_bool>(false);
        pimpl->io.restart();
        pimpl->socket->async_connect(endpoint, [is_connected](const boost::system::error_code& ec)
            {
                if (!ec)
                    is_connected->store(true);
            });
        try
        {
            pimpl->io.run_for(std::chrono::duration<int, std::milli>(500));
        }
        catch (...)
        {
        }

        if (is_connected->load())
        {
            pimpl->io.stop();
            pimpl->io.restart();
            StartIoThread();
        }
        else
        {
            boost::system::error_code close_ec;
            if(pimpl->socket)
            {
                pimpl->socket->cancel(close_ec);
                pimpl->socket->close(close_ec);
                pimpl->socket.reset();
            }
            pimpl->io.stop();
            pimpl->io.restart();
            pimpl->open = false;
        }
    }
    catch(...)
    {
        pimpl->open = false;
        setErrorStatus(true);
        boost::system::error_code close_ec;
        if(pimpl->socket)
        {
            pimpl->socket->cancel(close_ec);
            pimpl->socket->close(close_ec);
            pimpl->socket.reset();
        }
        pimpl->io.stop();
        pimpl->io.restart();
    }
}

bool AsyncSerial::isOpen() const
{
    return pimpl->open.load();
}

bool AsyncSerial::errorStatus() const
{
    lock_guard<mutex> l(pimpl->errorMutex);
    return pimpl->error;
}

void AsyncSerial::close()
{
    try
    {
        if(!isOpen())
        {
            JoinBackgroundThread(pimpl->backgroundThread);
            pimpl->io.restart();
            return;
        }

        pimpl->open=false;
        boost::asio::post(pimpl->io, [this]() {
            doClose();
            });
        JoinBackgroundThread(pimpl->backgroundThread);
        pimpl->io.restart();
        if(!pimpl->is_tcp && errorStatus())
        {
            throw(boost::system::system_error(boost::system::error_code(),
                    "Error while closing the device"));
        }
    }
    catch(...)
    {
        pimpl->open = false;
        setErrorStatus(true);
        if(!pimpl->is_tcp)
            throw;
    }
}

void AsyncSerial::write(std::span<const char> data)
{
    /* The one body the three public spellings share. They were three verbatim
       copies of this guard, queue append, post and catch, differing only in
       how the bytes were appended. */
    try
    {
        if(pimpl->is_tcp && !isOpen())
        {
            setErrorStatus(true);
            return;
        }

        {
            lock_guard<mutex> l(pimpl->writeQueueMutex);
            pimpl->writeQueue.insert(pimpl->writeQueue.end(), data.begin(), data.end());
        }
        boost::asio::post(pimpl->io, [this]() {
            doWrite();
            });
    }
    catch(...)
    {
        pimpl->open = false;
        setErrorStatus(true);
        if(!pimpl->is_tcp)
            throw;
    }
}

void AsyncSerial::write(const char *data, size_t size)
{
    write(std::span<const char>(data, size));
}

void AsyncSerial::write(const std::vector<char>& data)
{
    write(std::span<const char>(data));
}

void AsyncSerial::writeString(const std::string& s)
{
    write(std::span<const char>(s.data(), s.size()));
}

AsyncSerial::~AsyncSerial()
{
    try
    {
        if(isOpen())
            close();

        JoinBackgroundThread(pimpl->backgroundThread);
    }
    catch(...)
    {
        // Destruction must not propagate transport errors.
    }
}

void AsyncSerial::doRead()
{
    if (!pimpl->is_tcp)
    {
        pimpl->port.async_read_some(asio::buffer(pimpl->readBuffer, readBufferSize),
            boost::bind(&AsyncSerial::readEnd,
                this,
                asio::placeholders::error,
                asio::placeholders::bytes_transferred));
    }
    else
    {
        if(!pimpl->socket || !pimpl->socket->is_open())
        {
            pimpl->open = false;
            setErrorStatus(true);
            return;
        }
        pimpl->socket->async_read_some(asio::buffer(pimpl->readBuffer, readBufferSize),
            boost::bind(&AsyncSerial::readEnd,
                this,
                asio::placeholders::error,
                asio::placeholders::bytes_transferred));
    }
}

void AsyncSerial::readEnd(const boost::system::error_code& error,
        size_t bytes_transferred)
{
    if(error)
    {
        #ifdef __APPLE__
        if(error.value()==45)
        {
            //Bug on OS X, it might be necessary to repeat the setup
            //http://osdir.com/ml/lib.boost.asio.user/2008-08/msg00004.html
            doRead();
            return;
        }
        #endif //__APPLE__
        //error can be true even because the serial port was closed.
        //In this case it is not a real error, so ignore
        if(isOpen())
        {
            doClose();
            pimpl->open = false;
            setErrorStatus(true);
        }
    } else {
        if(pimpl->callback) pimpl->callback(pimpl->readBuffer,
                bytes_transferred);
        doRead();
    }
}

void AsyncSerial::PumpNextWrite()
{
    /* Caller holds writeQueueMutex. This body existed twice - once in doWrite
       to start the first write, once in writeEnd to chain the next - and the
       two copies had to stay in step over which stream to write and what a
       missing TCP socket means. */
    pimpl->writeBufferSize=pimpl->writeQueue.size();
    pimpl->writeBuffer.reset(new char[pimpl->writeQueue.size()]);
    copy(pimpl->writeQueue.begin(),pimpl->writeQueue.end(),
            pimpl->writeBuffer.get());
    pimpl->writeQueue.clear();

    if (!pimpl->is_tcp)
    {
        async_write(pimpl->port, asio::buffer(pimpl->writeBuffer.get(),
            pimpl->writeBufferSize),
            boost::bind(&AsyncSerial::writeEnd, this, asio::placeholders::error));
    }
    else
    {
        if(!pimpl->socket || !pimpl->socket->is_open())
        {
            setErrorStatus(true);
            pimpl->open = false;
            return;
        }
        async_write(*pimpl->socket, asio::buffer(pimpl->writeBuffer.get(),
            pimpl->writeBufferSize),
            boost::bind(&AsyncSerial::writeEnd, this, asio::placeholders::error));
    }
}

void AsyncSerial::doWrite()
{
    //If a write operation is already in progress, do nothing
    if(pimpl->writeBuffer==0)
    {
        lock_guard<mutex> l(pimpl->writeQueueMutex);
        PumpNextWrite();
    }
}

void AsyncSerial::writeEnd(const boost::system::error_code& error)
{
    if(!error)
    {
        lock_guard<mutex> l(pimpl->writeQueueMutex);
        if(pimpl->writeQueue.empty())
        {
            pimpl->writeBuffer.reset();
            pimpl->writeBufferSize=0;

            return;
        }
        PumpNextWrite();
    } else {
        setErrorStatus(true);
        doClose();
        pimpl->open = false;
    }
}

void AsyncSerial::doClose()
{
    boost::system::error_code ec;
    pimpl->open = false;
    if (!pimpl->is_tcp)
        pimpl->port.cancel(ec);
    else if(pimpl->socket)
        pimpl->socket->cancel(ec);

    if(ec) setErrorStatus(true);
    if (!pimpl->is_tcp)
        pimpl->port.close(ec);
    else if(pimpl->socket)
    {
        pimpl->socket->close(ec);
        pimpl->socket.reset();
    }
    if(ec) setErrorStatus(true);
}

void AsyncSerial::setErrorStatus(bool e)
{
    lock_guard<mutex> l(pimpl->errorMutex);
    pimpl->error=e;
}

void AsyncSerial::setReadCallback(const std::function<void (const char*, size_t)>& callback)
{
    pimpl->callback=callback;
}

void AsyncSerial::clearReadCallback()
{
    std::function<void (const char*, size_t)> empty;
    pimpl->callback.swap(empty);
}

//
//Class CallbackAsyncSerial
//

CallbackAsyncSerial::CallbackAsyncSerial(): AsyncSerial()
{

}

CallbackAsyncSerial::CallbackAsyncSerial(const std::string& devname,
        unsigned int baud_rate,
        asio::serial_port_base::parity opt_parity,
        asio::serial_port_base::character_size opt_csize,
        asio::serial_port_base::flow_control opt_flow,
        asio::serial_port_base::stop_bits opt_stop)
        :AsyncSerial(devname,baud_rate,opt_parity,opt_csize,opt_flow,opt_stop)
{

}

CallbackAsyncSerial::CallbackAsyncSerial(const std::string& ip, uint16_t port)
    : AsyncSerial(ip, port)
{

}

void CallbackAsyncSerial::setCallback(const std::function<void (const char*, size_t)>& callback)
{
    setReadCallback(callback);
}

void CallbackAsyncSerial::clearCallback()
{
    clearReadCallback();
}

CallbackAsyncSerial::~CallbackAsyncSerial()
{
    clearReadCallback();
}
