#include "pch.hpp"

CanEntryHandler::CanEntryHandler(ICanEntryLoader& loader, ICanRxEntryLoader& rx_loader, ICanMappingLoader& mapping_loader) :
    m_CanEntryLoader(loader), m_CanRxEntryLoader(rx_loader), m_CanMappingLoader(mapping_loader)
{
    m_StartTime = std::chrono::steady_clock::now();
    isotp_init_link(&m_IsoTpLink, m_DefaultEcuId, m_IsoTpSendBuf, sizeof(m_IsoTpSendBuf), m_IsoTpRecvBuf, sizeof(m_IsoTpRecvBuf));
}

CanEntryHandler::~CanEntryHandler()
{
    {
        std::unique_lock lock{ m };
        m_Cv.notify_all();
    }
    m_Worker.reset(nullptr);
}

void CanEntryHandler::Init()
{
    LoadFiles();
    m_Worker = std::make_unique<std::jthread>(std::bind_front(&CanEntryHandler::WorkerThread, this));
    if(m_Worker)
        utils::SetThreadName(*m_Worker, "CanEntryHandler");
}

void CanEntryHandler::LoadFiles()
{
    LoadTxList(default_tx_list);
    LoadRxList(default_rx_list);
    LoadMapping(default_mapping);
}

void CanEntryHandler::SetDefaultEcuId(uint32_t ecu_id)
{
    m_DefaultEcuId = ecu_id;
    m_IsoTpLink.send_arbitration_id = m_DefaultEcuId;
}

void CanEntryHandler::WorkerThread(std::stop_token token)
{
    while(!token.stop_requested())
    {
        {
            std::unique_lock lock{ m };
            std::chrono::steady_clock::time_point time_now = std::chrono::steady_clock::now();
            for(auto& i : entries | std::views::filter([](const auto& e) { return (e->period != 0 && e->send) || e->single_shot; }))
            {
                if(i->single_shot)
                {
                    CanSerialPort::Get()->AddToTxQueue(i->id, i->data.size(), i->data.data());
                    i->single_shot = false;
                }
                else if(time_now - i->last_execution > std::chrono::milliseconds(i->period))
                {
                    i->last_execution = std::chrono::steady_clock::now();
                    CanSerialPort::Get()->AddToTxQueue(i->id, i->data.size(), i->data.data());
                }
            }

            m_Cv.wait_for(lock, token, 1ms, []() { return false; });
            isotp_poll(&m_IsoTpLink);
        }
    }
    DBG("exit");
}

void CanEntryHandler::OnFrameSent(uint32_t frame_id, uint8_t data_len, uint8_t* data)
{
    std::scoped_lock lock{ m };
    auto it = std::ranges::find_if(entries, [frame_id](const auto& e) { return e->id == frame_id; });
    if(it != entries.end())
    {
        auto& i = *it;
        if(wxGetApp().is_init_finished)
        {
            i->count++;
            MyFrame* frame = dynamic_cast<MyFrame*>(wxGetApp().GetTopWindow());
            if(frame && frame->is_initialized)
            {
                frame->can_panel->sender->can_grid_tx->UpdateTxCounter(frame_id, i->count);
                if(m_IsRecording && i->log_level >= m_RecordingLogLevel)
                {
                    if(i->period == 0 && !i->single_shot)
                        i->last_execution = std::chrono::steady_clock::now();
                    m_LogEntries.push_back(std::make_unique<CanLogEntry>(CAN_LOG_DIR_TX, frame_id, data, data_len, i->last_execution));
                }
            }
        }
    }

    if(it == entries.end() && m_IsRecording)
    {
        std::chrono::steady_clock::time_point time_now = std::chrono::steady_clock::now();
        m_LogEntries.push_back(std::make_unique<CanLogEntry>(CAN_LOG_DIR_TX, frame_id, data, data_len, time_now));
    }

    NotifyFrameOnBus(frame_id, data, data_len);
    m_TxFrameCount++;
}

void CanEntryHandler::OnFrameReceived(uint32_t frame_id, uint8_t data_len, uint8_t* data)
{
    std::scoped_lock lock{ m };
    std::chrono::steady_clock::time_point time_now = std::chrono::steady_clock::now();

    if(!m_rxData.contains(frame_id))
    {
        m_rxData[frame_id] = std::make_unique<CanRxData>(data, data_len);
    }
    else
    {
        m_rxData[frame_id]->data.assign(data, data + data_len);
        uint32_t elapsed = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(time_now - m_rxData[frame_id]->last_execution).count());
        m_rxData[frame_id]->period = elapsed;
        m_rxData[frame_id]->count++;
    }
    m_rxData[frame_id]->last_execution = time_now;

    auto log_it = m_RxLogLevels.find(frame_id);
    m_rxData[frame_id]->log_level = (log_it != m_RxLogLevels.end()) ? log_it->second : 1;
    m_RxFrameCount++;

    if(m_IsRecording && m_rxData[frame_id]->log_level >= m_RecordingLogLevel)
        m_LogEntries.push_back(std::make_unique<CanLogEntry>(CAN_LOG_DIR_RX, frame_id, data, data_len, m_rxData[frame_id]->last_execution));

    if(frame_id == m_IsoTpResponseId)
    {
        isotp_on_can_message(&m_IsoTpLink, data, data_len);

        uint16_t recv_size = 0;
        if(isotp_receive(&m_IsoTpLink, m_UdsRecvData, sizeof(m_UdsRecvData), &recv_size) == ISOTP_RET_OK)
        {
            DBG("iso-tp recv: %d", recv_size);
            m_UdsFrames.push_back(std::string(reinterpret_cast<const char*>(m_UdsRecvData), recv_size));
            m_LastUdsFrameReceived = std::chrono::steady_clock::now();
            NotifyIsoTpData(frame_id, m_UdsRecvData, recv_size);
        }
    }

    NotifyFrameOnBus(frame_id, data, data_len);
    m_Cv.notify_all();
}

void CanEntryHandler::ToggleAutoSend(bool toggle)
{
    m_AutoSend = toggle;
}

void CanEntryHandler::ToggleRecording(bool toggle, bool is_pause)
{
    std::scoped_lock lock{ m };
    m_IsRecording = toggle;

    if(!is_pause && !toggle)
    {
        m_TxFrameCount = m_RxFrameCount = 0;
        m_LogEntries.clear();
    }
}

void CanEntryHandler::ClearRecording()
{
    std::scoped_lock lock{ m };
    m_TxFrameCount = m_RxFrameCount = 0;
    m_LogEntries.clear();
}

void CanEntryHandler::SendDataFrame(uint32_t frame_id, uint8_t* data, uint16_t size)
{
    CanSerialPort::Get()->AddToTxQueue(frame_id, size, data);
}

void CanEntryHandler::SendIsoTpFrame(uint32_t frame_id, uint8_t* data, uint16_t size)
{
    isotp_send_with_id(&m_IsoTpLink, frame_id, data, size);
}

bool CanEntryHandler::LoadTxList(std::filesystem::path& path)
{
    std::scoped_lock lock{ m };
    if(path.empty())
        path = default_tx_list;

    entries.clear();
    bool ret = m_CanEntryLoader.Load(path, entries);
    if(ret)
    {
        if(m_AutoSend)
            std::ranges::for_each(entries, [](auto& i) { i->single_shot = false; i->send = true; });

        m_IsRecording = m_AutoRecording;
    }
    return ret;
}

bool CanEntryHandler::SaveTxList(std::filesystem::path& path)
{
    std::scoped_lock lock{ m };
    if(path.empty())
        path = default_tx_list;
    return m_CanEntryLoader.Save(path, entries);
}

bool CanEntryHandler::LoadRxList(std::filesystem::path& path)
{
    std::scoped_lock lock{ m };
    if(path.empty())
        path = default_rx_list;

    rx_entry_comment.clear();
    return m_CanRxEntryLoader.Load(path, rx_entry_comment, m_RxLogLevels);
}

bool CanEntryHandler::SaveRxList(std::filesystem::path& path)
{
    std::scoped_lock lock{ m };
    if(path.empty())
        path = default_rx_list;
    return m_CanRxEntryLoader.Save(path, rx_entry_comment, m_RxLogLevels);
}

bool CanEntryHandler::LoadMapping(std::filesystem::path& path)
{
    std::scoped_lock lock{ m };
    if(path.empty())
        path = default_mapping;

    m_Mapping.clear();
    return m_CanMappingLoader.Load(path, m_Mapping, m_FrameMetadata);
}

bool CanEntryHandler::SaveMapping(std::filesystem::path& path)
{
    std::scoped_lock lock{ m };
    if(path.empty())
        path = default_mapping;
    return m_CanMappingLoader.Save(path, m_Mapping, m_FrameMetadata);
}

bool CanEntryHandler::SaveRecordingToFile(std::filesystem::path& path)
{
    std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
    std::scoped_lock lock{ m };
    bool ret = false;
    if(!m_LogEntries.empty())
    {
        std::ofstream out(path, std::ofstream::binary);
        if(out.is_open())
        {
            out << "Time,Direction,FrameID,DataSize,Data,Comment\n";
            for(auto& i : m_LogEntries)
            {
                std::string hex;
                utils::ConvertHexBufferToString(i->data, hex);
                uint64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(i->last_execution - m_StartTime).count();
                out << std::format("{:.3f},{},{:X},{},{}", static_cast<double>(elapsed) / 1000.0,
                    i->direction == 0 ? "TX" : "RX", static_cast<uint32_t>(i->frame_id), i->data.size(), hex);

                std::string* comment = nullptr;
                if(i->direction == CAN_LOG_DIR_TX)
                {
                    if(auto tx = FindTxCanEntryByFrame(i->frame_id))
                        comment = &tx->get().comment;
                }
                else if(rx_entry_comment.contains(i->frame_id))
                {
                    comment = &rx_entry_comment[i->frame_id];
                }

                out << (comment && !comment->empty() ? "," + *comment : "") << "\n";
            }
            out.flush();
            ret = true;
        }
        else
        {
            LOG(LogLevel::Error, "Failed to open file for saving CAN recording: {}", path.generic_string());
        }
    }

    if(ret)
    {
        int64_t dif = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t1).count();
        MyFrame* frame = dynamic_cast<MyFrame*>(wxGetApp().GetTopWindow());
        if(!frame)
            return ret;
        std::unique_lock lock(frame->mtx);
        frame->pending_msgs.push_back({ static_cast<uint8_t>(PopupMsgIds::CanLogSaved), dif, path.generic_string() });
    }
    return ret;
}

void CanEntryHandler::GenerateLogForFrame(uint32_t frame_id, bool is_rx, std::vector<std::string>& log)
{
    auto format_entry = [&](const auto& i) -> std::string
    {
        std::string hex;
        utils::ConvertHexBufferToString(i->data, hex);
        uint64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(i->last_execution - m_StartTime).count();
        std::string out = std::format("{:<6.03f}{:<6}{:<6X}{:<6}{:<6}", static_cast<double>(elapsed) / 1000.0,
            i->direction == 0 ? "TX" : "RX", static_cast<uint32_t>(i->frame_id), i->data.size(), hex);

        std::string* comment = nullptr;
        if(i->direction == CAN_LOG_DIR_TX)
        {
            if(auto tx = FindTxCanEntryByFrame(i->frame_id))
                comment = &tx->get().comment;
        }
        else if(rx_entry_comment.contains(i->frame_id))
            comment = &rx_entry_comment[i->frame_id];

        if(comment && !comment->empty())
            out += std::format("   {:^6}", *comment);
        return out;
    };

    auto filtered = m_LogEntries
        | std::views::filter([frame_id](const auto& i) { return i->frame_id == frame_id; })
        | std::views::transform(format_entry);
    std::ranges::copy(filtered, std::back_inserter(log));
}

template <typename T> void CanEntryHandler::HandleBitReading(uint32_t frame_id, bool is_rx, std::unique_ptr<CanMap>& m, size_t offset, CanBitfieldInfo& info)
{
    const std::vector<uint8_t>* data_ptr = nullptr;
    size_t data_size = 0;

    if(is_rx)
    {
        if(!m_rxData.contains(frame_id))
            return;
        data_ptr  = &m_rxData[frame_id]->data;
        data_size = data_ptr->size();
    }
    else
    {
        auto tx = FindTxCanEntryByFrame(frame_id);
        if(!tx.has_value())
            return;
        data_ptr  = &tx->get().data;
        data_size = data_ptr->size();
    }

    uint64_t value = get_bitfield(data_ptr->data(), data_size, offset, m->m_Size);
    T extracted = static_cast<T>(value);
    info.push_back({
        std::format("{}         (offset: {}, size: {}, range: {} - {})", m->m_Name, offset, m->m_Size, m->m_MinVal, m->m_MaxVal),
        std::to_string(extracted),
        m.get()
    });
}

template <typename T> void CanEntryHandler::HandleBitWriting(uint32_t frame_id, uint8_t& pos, uint8_t offset, uint8_t size, uint8_t* byte_array, std::vector<std::string>& new_data)
{
    try
    {
        T raw_data = static_cast<T>(std::stoi(new_data[pos]));
        set_bitfield(raw_data, offset, size, byte_array, 8);
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Invalid input for pos {}. Exception: {}", pos, e.what());
    }
    pos++;
}

CanBitfieldInfo CanEntryHandler::GetMapForFrameId(uint32_t frame_id, bool is_rx)
{
    CanBitfieldInfo info;
    if(!m_Mapping.contains(frame_id))
        return info;

    for(auto& [offset, m] : m_Mapping[frame_id])
    {
        DispatchBitfieldType(m->m_Type, [&]<typename T>()
        {
            HandleBitReading<T>(frame_id, is_rx, m, offset, info);
        });
    }
    return info;
}

void CanEntryHandler::ApplyEditingOnFrameId(uint32_t frame_id, std::vector<std::string> new_data)
{
    uint8_t cnt = 0;
    uint8_t byte_array[8] = {};
    if(auto tx = FindTxCanEntryByFrame(frame_id))
    {
        size_t copy_len = std::min(tx->get().data.size(), sizeof(byte_array));
        memcpy(byte_array, tx->get().data.data(), copy_len);
    }

    if(!m_Mapping.contains(frame_id))
        return;

    LOG(LogLevel::Normal, "MapSize: {}, NewDataSize: {}", m_Mapping[frame_id].size(), new_data.size());
    for(auto& [offset, m] : m_Mapping[frame_id])
    {
        DispatchBitfieldType(m->m_Type, [&]<typename T>()
        {
            HandleBitWriting<T>(frame_id, cnt, offset, m->m_Size, byte_array, new_data);
        });
    }
    AssignNewBufferToTxEntry(frame_id, byte_array, sizeof(byte_array));
}

void CanEntryHandler::AssignNewBufferToTxEntry(uint32_t frame_id, uint8_t* buffer, size_t size)
{
    if(auto tx = FindTxCanEntryByFrame(frame_id))
        tx->get().data.assign(buffer, buffer + size);
}

std::optional<std::reference_wrapper<CanTxEntry>> CanEntryHandler::FindTxCanEntryByFrame(uint32_t frame_id)
{
    auto ret = std::ranges::find_if(entries, [frame_id](const auto& item) { return item->id == frame_id; });
    if(ret == entries.cend())
        return {};
    return **ret;
}

uint32_t CanEntryHandler::FindFrameIdOnMapByName(const std::string& name)
{
    auto it = std::ranges::find_if(m_FrameMetadata, [&name](const auto& kv) { return kv.second.name == name; });
    return it != m_FrameMetadata.end() ? it->first : 0;
}

uint32_t CanEntryHandler::GetElapsedTimeSinceLastUdsFrame() const
{
    int64_t diff = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_LastUdsFrameReceived).count();
    return static_cast<uint32_t>(std::clamp<int64_t>(diff, 0, std::numeric_limits<uint32_t>::max()));
}

extern "C" void isotp_user_debug(const char* message, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, message);
    vsnprintf(buffer, sizeof(buffer), message, args);
    va_end(args);

    LOG(LogLevel::Verbose, "IsoTP: {}", buffer);
    DBG("%s", buffer);
}

extern "C" uint32_t isotp_user_get_ms(void)
{
    using namespace utils;
    return GetTickCount();
}

extern "C" int isotp_user_send_can(const uint32_t arbitration_id, const uint8_t* data, const uint8_t size)
{
    CanSerialPort::Get()->AddToTxQueue(arbitration_id, size, data);
    return 0;
}
