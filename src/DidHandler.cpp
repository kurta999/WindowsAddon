#include "pch_core.hpp"
#include "DidHandler.hpp"
#include "utils/XmlDocument.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "utils/InterruptibleSleep.hpp"
#include "UdsCodes.hpp"

using namespace std::chrono_literals;

constexpr const char* DID_LIST_FILENAME = "DidList.xml";

constexpr uint8_t MAX_EXTENDED_SESSION_RETRIES = 5;
constexpr auto UDS_TIMEOUT_FOR_RESPONSE = 12000ms;

constexpr auto MAIN_THREAD_SLEEP = 350ms;

DidHandler::DidHandler(IDidLoader& loader, IDidLoader& cache_loader, CanEntryHandler* can_handler) :
    m_loader(loader), m_cache_loader(cache_loader), m_can_handler(can_handler), m_Semaphore(0)
{

}

DidHandler::~DidHandler()
{
    /* Stop taking new responses first, then wake and join the worker, and only
       then drop the queues. Clearing them while the worker was still walking
       them left it popping an already empty queue. */
    m_can_handler->UnregisterObserver(this);

    m_IsAborted = true;
    if(m_worker)
        m_worker->request_stop();
    {
        std::scoped_lock lock(m_CanMessageMutex);
        m_ResponseAborted = true;
    }
    m_CanMessageCv.notify_all();
    m_Semaphore.release();
    m_worker.reset(nullptr);

    std::scoped_lock lock(m);
    m_PendingDidReads.clear();
    m_PendingDidWrites.clear();
}

void DidHandler::Init()
{
    m_loader.Load(DID_LIST_FILENAME, m_DidList);
    m_cache_loader.Load(DID_CACHE_FILENAME, m_DidList);
    m_worker = utils::StartNamedWorker("DidHandler", std::bind_front(&DidHandler::WorkerThread, this));
    m_can_handler->RegisterObserver(this);
}

bool DidHandler::SaveCache() const
{
    std::filesystem::path cache_path = DID_CACHE_FILENAME;
    bool ret = m_cache_loader.Save(cache_path, m_DidList);
    return ret;
}

void DidHandler::AddDidToReadQueue(uint16_t did)
{
    /* Queued from the GUI thread while the worker walks the same queue. */
    std::scoped_lock lock(m);
    m_PendingDidReads.push_back(did);
}

void DidHandler::WriteDid(uint16_t did, uint8_t* data_to_write, uint16_t size)
{
    std::scoped_lock lock(m);
    m_PendingDidWrites[did] = std::string(data_to_write, data_to_write + size);
}

void DidHandler::NotifyDidUpdate()
{
    m_Semaphore.release();
}

void DidHandler::AbortDidUpdate()
{
    {
        std::unique_lock lock{ m };
        m_PendingDidReads.clear();
        m_PendingDidWrites.clear();
    }

    m_IsAborted = true;

    {
        /* Signalled through a dedicated flag: stuffing a sentinel into the
           length made every later bounds check read a bogus size. */
        std::scoped_lock lock(m_CanMessageMutex);
        m_ResponseAborted = true;
    }
    m_CanMessageCv.notify_all();

    LOG(LogLevel::Normal, "In progress DID updating has been aborted");
}

void DidHandler::OnFrameOnBus(uint32_t frame_id, uint8_t* data, uint16_t size)
{

}

void DidHandler::OnIsoTpDataReceived(uint32_t frame_id, const uint8_t* data, uint16_t size)
{
    if(data == nullptr || size == 0)
        return;

    /* The transport is allowed to hand us more than we can hold; clamp rather
       than overflowing the buffer. */
    const uint16_t copy_len = static_cast<uint16_t>(
        std::min<std::size_t>(size, sizeof(m_IsoTpBuffer)));
    if(copy_len != size)
        LOG(LogLevel::Warning, "ISO-TP response truncated from {} to {} bytes", size, copy_len);

    std::string hex;
    {
        /* Same mutex the waiter's condition variable uses, so the bytes and the
           length that describes them are always published together. */
        std::scoped_lock lock(m_CanMessageMutex);
        memcpy(m_IsoTpBuffer, data, copy_len);
        m_IsoTpBufLen = copy_len;
        utils::ConvertHexBufferToString((const char*)m_IsoTpBuffer, copy_len, hex);
    }
    m_CanMessageCv.notify_all();

    LOG(LogLevel::Debug, "OnIsoTpFrameReceived: {} | \"{}\"", copy_len, hex);
}

std::uint64_t DidHandler::ResponseInteger(std::size_t offset, std::size_t width) const noexcept
{
    std::uint64_t value = 0;
    for(std::size_t i = 0; i < width; ++i)
        value |= static_cast<std::uint64_t>(ResponseByte(offset + i)) << (8 * i);
    return value;
}

uint8_t DidHandler::ResponseByte(std::size_t index) const noexcept
{
    return index < m_Response.size() ? m_Response[index] : static_cast<uint8_t>(0);
}

std::string DidHandler::ResponsePayload(std::size_t header_size) const
{
    if(m_Response.size() <= header_size)
        return {};
    return std::string(m_Response.begin() + header_size, m_Response.end());
}

XmlDidLoader::~XmlDidLoader()
{

}

XmlDidCacheLoader::~XmlDidCacheLoader()
{

}

bool XmlDidLoader::Load(const std::filesystem::path& path, DidMap& m)
{
    return utils::xml::LoadEntries(path, "DidListXml", [&m](const boost::property_tree::ptree::value_type& v)
    {
        /* TryParse range-checks at the target width: a static_cast<uint16_t>
           after std::stoi truncated a DID above 0xFFFF instead of rejecting it. */
        const std::optional<uint16_t> parsed_did = utils::xml::ReadHexChild<uint16_t>(v, "DID");
        if(!parsed_did)
            return;
        const uint16_t did = *parsed_did;

        const auto it = std::find_if(m.cbegin(), m.cend(), [&did](const auto& item) { return item.second->id == did; });
        if(it != m.cend())
        {
            LOG(LogLevel::Warning, "DID {:X} has been already added to the list, skipping this one", did);
            return;
        }

        std::string type_str = v.second.get_child("Type").get_value<std::string>();
        DidEntryType did_value_type = GetTypeFromString(type_str);
        if(did_value_type == DidEntryType::DET_INVALID)
        {
            /* Formatted from the parsed value, not the text: "{:X}" on a string
               throws a format_error, and that used to escape the loop and take the
               whole DID list down with it. */
            LOG(LogLevel::Warning, "Invalid type used for DID: {:X}, type: {}", did, type_str);
            return;
        }

        /* A missing or malformed Length has always meant 0; the bare
           catch(...) that expressed that is now the fallback argument. */
        const std::string str_len = v.second.get_child("Length").get_value<std::string>();
        const size_t len = utils::ParseOr<size_t>(str_len, 0);

        std::unique_ptr<DidEntry> entry = std::make_unique<DidEntry>(did, did_value_type, v.second.get_child("Name").get_value<std::string>(),
            v.second.get_child("Min").get_value<std::string>(), v.second.get_child("Max").get_value<std::string>(), len);
        m[did] = std::move(entry);
    });
}

bool XmlDidLoader::Save(const std::filesystem::path& path, const DidMap& m) const
{
    boost::property_tree::ptree pt;
    auto& root_node = pt.add_child("DidListXml", boost::property_tree::ptree{});
    for(auto& i : m)
    {
        /* Every element Load() reads, spelled the way it reads them. This used
           to write <ID> and leave Type and Length out entirely, so a saved DID
           list was one the loader could not read back. */
        auto& frame_node = root_node.add_child("DidEntry", boost::property_tree::ptree{});
        frame_node.add("DID", std::format("{:X}", i.second->id));
        frame_node.add("Name", i.second->name);
        frame_node.add("Type", GetStringFromType(i.second->type));
        frame_node.add("Length", i.second->len);
        frame_node.add("Min", i.second->min);
        frame_node.add("Max", i.second->max);
    }

    return utils::xml::Save(path, pt);
}

bool XmlDidCacheLoader::Load(const std::filesystem::path& path, DidMap& m)
{
    if(!std::filesystem::exists(path))
    {
        LOG(LogLevel::Normal, "DID cache is missing ({}), skip loading", path.generic_string());
        return false;
    }

    return utils::xml::LoadEntries(path, "DidCacheXml", [&m](const boost::property_tree::ptree::value_type& v)
    {
        const std::optional<uint16_t> parsed_did = utils::xml::ReadHexChild<uint16_t>(v, "DID");
        if(!parsed_did)
            return;
        const uint16_t did = *parsed_did;

        auto did_it = m.find(did);
        if(did_it == m.end())
        {
            LOG(LogLevel::Warning, "DID {:X} from cache isn't found on map, skipping...", did);
            return;
        }

        did_it->second->value_str = v.second.get_child("Value").get_value<std::string>();

        const std::optional<uint16_t> parsed_nrc = utils::xml::ReadHexChild<uint16_t>(v, "NRC");
        if(!parsed_nrc)
            return;
        did_it->second->nrc = *parsed_nrc;

        std::string last_update_str = v.second.get_child("Timestamp").get_value<std::string>();
        if(!last_update_str.empty())
            did_it->second->last_update = boost::posix_time::from_iso_extended_string(last_update_str);
    });
}

bool XmlDidCacheLoader::Save(const std::filesystem::path& path, const DidMap& m) const
{
    boost::property_tree::ptree pt;
    auto& root_node = pt.add_child("DidCacheXml", boost::property_tree::ptree{});
    for(auto& i : m)
    {
        auto& frame_node = root_node.add_child("DidCache", boost::property_tree::ptree{});
        frame_node.add("DID", std::format("{:X}", i.second->id));
        frame_node.add("Value", i.second->value_str);
        frame_node.add("NRC", std::format("{:X}", i.second->nrc));

        std::string last_update_str;
        if(!i.second->last_update.is_not_a_date_time())
            last_update_str = boost::posix_time::to_iso_extended_string(i.second->last_update);
        frame_node.add("Timestamp", last_update_str);
    }

    return utils::xml::Save(path, pt);
}

DidEntryType XmlDidLoader::GetTypeFromString(const std::string_view& input)
{
    return m_DidEntryTypeMap.FromName(input);
}

const std::string_view XmlDidLoader::GetStringFromType(DidEntryType type)
{
    return m_DidEntryTypeMap.NameOf(type);
}

bool DidHandler::SendUdsFrameAndWaitForResponse(std::vector<uint8_t> frame)
{
    if(!m_can_handler)
        return false;

    {
        std::scoped_lock lock(m_CanMessageMutex);
        m_IsoTpBufLen = 0;
        m_ResponseAborted = false;
        m_Response.clear();
    }
    m_can_handler->SendIsoTpFrame(m_can_handler->GetDefaultEcuId(), frame.data(), static_cast<uint16_t>(frame.size()));
    LOG(LogLevel::Verbose, "DidHandler::SendUdsFrameAndWaitForResponse");

    bool ret = WaitForResponse();
    return ret;
}

bool DidHandler::WaitForResponse()
{
    std::string hex;
    std::size_t response_size = 0;
    {
        std::unique_lock lk(m_CanMessageMutex);
        auto now = std::chrono::system_clock::now();
        const bool signalled = m_CanMessageCv.wait_until(lk, now + UDS_TIMEOUT_FOR_RESPONSE,
            [this]() { return m_IsoTpBufLen != 0 || m_ResponseAborted; });

        if(!signalled)
        {
            LOG(LogLevel::Verbose, "DidHandler::Timeout");
            return false;
        }

        if(m_ResponseAborted)
        {
            m_Response.clear();
            return false;
        }

        /* Take a private copy while the lock is held. Everything downstream
           parses this snapshot, so a frame arriving mid-parse cannot change
           the bytes under it. */
        const std::size_t length = std::min<std::size_t>(m_IsoTpBufLen.load(), sizeof(m_IsoTpBuffer));
        m_Response.assign(m_IsoTpBuffer, m_IsoTpBuffer + length);
        response_size = length;
        utils::ConvertHexBufferToString((const char*)m_Response.data(), response_size, hex);
    }

    LOG(LogLevel::Verbose, "DidHandler::Finished waiting. Response len: {} | \"{}\"", response_size, hex);
    return true;
}

void DidHandler::ProcessReadDidResponse(std::unique_ptr<DidEntry>& entry)
{
    std::unique_lock lock(m);
    entry->nrc = 0x0;
    entry->last_update = boost::posix_time::second_clock::local_time();

    /* Response layout for a positive read: kReadPositiveResponse, DID high,
   DID low, payload... */
    constexpr std::size_t UDS_READ_HEADER_SIZE = 3;

    switch(entry->type)
    {
        case DET_UI8:
        case DET_UI16:
        case DET_UI32:
        case DET_UI64:
        {
            /* No width or fill in the format spec, so formatting the widened
               value prints the same digits the narrow types did. */
            entry->value_str = std::format("{:X}", ResponseInteger(UDS_READ_HEADER_SIZE,
                did_types::IntegerWidth(entry->type)));
            break;
        }
        case DET_STRING:
        {
            entry->value_str = ResponsePayload(UDS_READ_HEADER_SIZE);
            break;
        }
        case DET_BYTEARRAY:
        {
            const std::string response = ResponsePayload(UDS_READ_HEADER_SIZE);
            std::string hex;
            utils::ConvertHexBufferToString(response.data(), response.length(), hex);

            entry->value_str = hex;
            break;
        }
        default:
        {
            break;
        }
    }

    /* An abort can have dropped the entry while its answer was still in
       flight, so the queue is not guaranteed to still hold it. */
    if(!m_PendingDidReads.empty())
        m_PendingDidReads.pop_front();
    m_UpdatedDids.push_back(entry->id);

    LOG(LogLevel::Verbose, "Received response for DID: {:X} | \"{}\"", entry->id, entry->value_str);
}

void DidHandler::MarkDidUpdated(uint16_t did)
{
    std::scoped_lock lock(m);
    m_UpdatedDids.push_back(did);
}

void DidHandler::SetDidNrc(std::unique_ptr<DidEntry>& entry, uint8_t nrc)
{
    std::scoped_lock lock(m);
    entry->nrc = nrc;
}

void DidHandler::SetDidNrcAndTimestamp(std::unique_ptr<DidEntry>& entry, uint8_t nrc)
{
    std::scoped_lock lock(m);
    entry->nrc = nrc;
    entry->last_update = boost::posix_time::second_clock::local_time();
}

void DidHandler::DropCurrentDidRead()
{
    std::scoped_lock lock(m);
    /* An abort can have dropped the entry while its answer was still in
       flight, so the queue is not guaranteed to still hold it. */
    if(!m_PendingDidReads.empty())
        m_PendingDidReads.pop_front();
}

void DidHandler::ProcessRejectedNrc(std::unique_ptr<DidEntry>& entry)
{
    LOG(LogLevel::Warning, "Rejected");
    std::unique_lock lock(m);
    entry->nrc = uds::kGeneralReject;
    entry->last_update = boost::posix_time::second_clock::local_time();
    if(!m_PendingDidReads.empty())
        m_PendingDidReads.pop_front();
}

void DidHandler::HandleDidReading(std::stop_token& token)
{
    uint8_t extended_session_retry_count = 0;
    while(!token.stop_requested() && !m_IsAborted)
    {
        {
            /* The queue can be emptied by an abort or by teardown between two
               passes, so the emptiness check and the read of the front element
               have to happen under the same lock. */
            uint16_t curr_did = 0;
            {
                std::scoped_lock lock(m);
                if(m_PendingDidReads.empty())
                    break;
                curr_did = m_PendingDidReads.front();
            }

            auto& did_it = m_DidList[curr_did];

            //m_PendingDids.pop_front();

            bool is_ok = SendUdsFrameAndWaitForResponse({ uds::kDiagnosticSessionControl, uds::kExtendedDiagnosticSession });
            if(is_ok)
            {
                extended_session_retry_count = 0;
                m_IsoTpBufLen = 0;
                uint8_t data[3] = { uds::kReadDataByIdentifier };
                data[1] = did_it->id >> 8 & 0xFF;
                data[2] = did_it->id & 0xFF;
                is_ok = SendUdsFrameAndWaitForResponse(std::vector<uint8_t>(data, data + 3));
                if(is_ok)
                {
                    {
                        utils::InterruptibleSleep(m_CanMessageCv, m, token, 50ms);
                    }

                    if(ResponseByte(0) == uds::kReadPositiveResponse)
                    {
                        ProcessReadDidResponse(did_it);
                    }
                    else if(ResponseByte(0) == uds::kNegativeResponse)
                    {
                        LOG(LogLevel::Warning, "7F received for DID ({:X} {:X}): {:X}", ResponseByte(1), ResponseByte(2), did_it->id);
                        if(ResponseByte(1) == uds::kReadDataByIdentifier && ResponseByte(2) == uds::kResponsePending)
                        {
                            while(!token.stop_requested() && !m_IsAborted)
                            {
                                m_IsoTpBufLen = 0;
                                bool is_recv_ok = WaitForResponse();

                                if(!is_recv_ok)
                                {
                                    LOG(LogLevel::Warning, "Pending response timeout");
                                    break;
                                }

                                const auto kind = uds::ClassifyPendingResponse(uds::kReadDataByIdentifier,
                                    ResponseByte(0), ResponseByte(1), ResponseByte(2));

                                if(kind == uds::ResponseKind::Positive)
                                {
                                    ProcessReadDidResponse(did_it);
                                    break;
                                }
                                else if(kind == uds::ResponseKind::Pending)
                                {
                                    LOG(LogLevel::Warning, "Pending response NRC 78");
                                    SetDidNrc(did_it, uds::kResponsePending);
                                }
                                else if(kind == uds::ResponseKind::Rejected)
                                {
                                    ProcessRejectedNrc(did_it);
                                    break;
                                }
                                else if(kind == uds::ResponseKind::NegativeOther)
                                {
                                    LOG(LogLevel::Warning, "Pending response general NRC: {}", ResponseByte(2));
                                    SetDidNrcAndTimestamp(did_it, ResponseByte(2));
                                    DropCurrentDidRead();
                                    break;
                                }
                                else
                                {
                                    LOG(LogLevel::Warning, "Pending response else case: {}", ResponseByte(1));
                                }
                                MarkDidUpdated(curr_did);
                            }

                            LOG(LogLevel::Warning, "Pending response base");
                        }
                        else
                        {
                            if(ResponseByte(2) == uds::kGeneralReject)
                            {
                                ProcessRejectedNrc(did_it);
                            }
                            else if(ResponseByte(1) == uds::kReadDataByIdentifier && uds::IsNrc(ResponseByte(2)))
                            {
                                /* This used to break the outer loop over the queue,
                                   abandoning every DID behind this one, and it did not
                                   drop this DID either. Its sibling branch above, for
                                   NRC 0x10, does neither of those things. */
                                LOG(LogLevel::Warning, "Pending response general NRC: {}", ResponseByte(2));
                                SetDidNrcAndTimestamp(did_it, ResponseByte(2));
                                DropCurrentDidRead();
                            }
                            else
                            {
                                LOG(LogLevel::Warning, "Unimplemented NRC received, rejecting this DID");
                                ProcessRejectedNrc(did_it);
                                SetDidNrc(did_it, ResponseByte(2));
                            }
                        }
                        MarkDidUpdated(curr_did);
                    }
                    else if(ResponseByte(0) == uds::kSessionPositiveResponse)
                    {
                        LOG(LogLevel::Verbose, "Ignoring extended session response");
                    }
                }
            }
            else
            {
                if(++extended_session_retry_count > MAX_EXTENDED_SESSION_RETRIES)
                {
                    LOG(LogLevel::Error, "No response for extended session after {} retries, abort questioning.", MAX_EXTENDED_SESSION_RETRIES);
                    extended_session_retry_count = 0;
                    {
                        std::scoped_lock lock(m);
                        m_PendingDidReads.clear();
                    }
                    break;
                }
            }
        }

        {
            utils::InterruptibleSleep(m_CanMessageCv, m, token, MAIN_THREAD_SLEEP);
        }
    }
}

void DidHandler::HandleDidWriting(std::stop_token& token)
{
    /* A write queued from the GUI while this loop runs would otherwise
       invalidate the iteration, so work from a snapshot. */
    std::map<uint16_t, std::string> pending_writes;
    {
        std::scoped_lock lock(m);
        pending_writes = m_PendingDidWrites;
    }

    if(pending_writes.size() > 0 && !token.stop_requested() && !m_IsAborted)
    {
        for(auto& [did, raw_value] : pending_writes)
        {
            bool is_ok = SendUdsFrameAndWaitForResponse({ uds::kDiagnosticSessionControl, uds::kExtendedDiagnosticSession });
            if(is_ok)
            {
                m_IsoTpBufLen = 0;
                std::vector<uint8_t> data_to_write = {uds::kWriteDataByIdentifier};
                data_to_write.push_back(did >> 8 & 0xFF);
                data_to_write.push_back(did & 0xFF);
                std::copy(raw_value.begin(), raw_value.end(), std::back_inserter(data_to_write));

                is_ok = SendUdsFrameAndWaitForResponse(data_to_write);
                if(is_ok)
                {
                    if(ResponseByte(0) == uds::kWritePositiveResponse)
                    {
                        LOG(LogLevel::Warning, "Did write OK");
                    }
                    else if(ResponseByte(0) == uds::kNegativeResponse)
                    {
                        LOG(LogLevel::Warning, "7F received for write DID ({:X} {:X}): {:X}", ResponseByte(1), ResponseByte(2), did);
                        if(ResponseByte(1) == uds::kWriteDataByIdentifier && ResponseByte(2) == uds::kResponsePending)
                        {
                            while(!token.stop_requested() && !m_IsAborted)
                            {
                                m_IsoTpBufLen = 0;
                                bool is_recv_ok = WaitForResponse();

                                if(!is_recv_ok)
                                {
                                    LOG(LogLevel::Warning, "Pending response timeout");
                                    break;
                                }

                                const auto kind = uds::ClassifyPendingResponse(uds::kWriteDataByIdentifier,
                                    ResponseByte(0), ResponseByte(1), ResponseByte(2));

                                if(kind == uds::ResponseKind::Positive)
                                {
                                    LOG(LogLevel::Warning, "Did write OK");
                                    break;
                                }
                                else if(kind == uds::ResponseKind::Pending)
                                {
                                    LOG(LogLevel::Warning, "Pending response NRC 78");
                                }
                                else if(kind == uds::ResponseKind::Rejected)
                                {
                                    LOG(LogLevel::Warning, "Did write rejected");
                                    break;
                                }
                                else
                                {
                                    /* NegativeOther lands here too: the write loop, unlike the read
                                       loop, never had a branch for a general NRC. */
                                    LOG(LogLevel::Warning, "Pending write response else case: {}", ResponseByte(1));
                                }
                            }

                            LOG(LogLevel::Warning, "Pending response base");
                        }
                        else
                        {
                            if(ResponseByte(2) == uds::kGeneralReject)
                            {
                                LOG(LogLevel::Warning, "Did write rejected");
                            }
                        }
                    }
                    else if(ResponseByte(0) == uds::kSessionPositiveResponse)
                    {
                        LOG(LogLevel::Verbose, "Ignoring extended session response");
                    }
                }
            }

            {
                utils::InterruptibleSleep(m_CanMessageCv, m, token, MAIN_THREAD_SLEEP);
            }
        }

        {
            std::unique_lock lock{ m };
            m_PendingDidWrites.clear();
        }
    }
    m_IsAborted = false;
}

void DidHandler::WorkerThread(std::stop_token token)
{
    while(!token.stop_requested())
    {
        m_Semaphore.acquire();
        HandleDidReading(token);
        HandleDidWriting(token);
    }
}