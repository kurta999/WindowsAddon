#include "pch_core.hpp"
#include "CanEntryHandler.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "SettingsReader.hpp"
#include "SettingsWriter.hpp"
#include <ostream>
#include "CanSerialPort.hpp"
#include <bitfield/bitfield.h>

using namespace std::chrono_literals;

CanEntryHandler::CanEntryHandler(ICanEntryLoader& loader, ICanRxEntryLoader& rx_loader, ICanMappingLoader& mapping_loader,
    ICanTransport& transport, IClock& clock, ICanEventSink* event_sink) :
    m_CanEntryLoader(loader), m_CanRxEntryLoader(rx_loader), m_CanMappingLoader(mapping_loader),
    m_CanTransport(transport), m_Clock(clock), m_EventSink(event_sink),
    m_IsoTp(transport, clock, m_DefaultEcuId)
{
    m_StartTime = m_Clock.Now();
    m_Log.SetStart(m_StartTime);
    m_CanTransport.SetListener(this);
}

CanEntryHandler::~CanEntryHandler()
{
    m_CanTransport.SetListener(nullptr);
    {
        std::unique_lock lock{ m };
        m_Cv.notify_all();
    }
    m_Worker.reset(nullptr);
}

void CanEntryHandler::Init()
{
    LoadFiles();
    m_Worker = utils::StartNamedWorker("CanEntryHandler",
        std::bind_front(&CanEntryHandler::WorkerThread, this));
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
    m_IsoTp.SetRequestId(ecu_id);
}

void CanEntryHandler::WorkerThread(std::stop_token token)
{
    /* Which frames are due is decided under the model lock; sending them is
       not. Send hands the frame to the transport's own queue and Poll takes the
       ISO-TP endpoint's lock and may send as well, so holding m across either
       stalled the receive path and every GUI read for the duration. The 1 ms
       wait already released m - condition_variable_any does that - but the
       sends around it did not. */
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> due;

    while(!token.stop_requested())
    {
        due.clear();
        {
            std::scoped_lock lock{ m };
            const auto time_now = m_Clock.Now();
            for(auto& i : entries | std::views::filter([](const auto& e) { return (e->period != 0 && e->send) || e->single_shot; }))
            {
                if(i->single_shot)
                {
                    due.emplace_back(i->id, i->data);
                    i->single_shot = false;
                }
                else if(time_now - i->last_execution > std::chrono::milliseconds(i->period))
                {
                    i->last_execution = m_Clock.Now();
                    due.emplace_back(i->id, i->data);
                }
            }
        }

        for(const auto& [id, payload] : due)
            m_CanTransport.Send(id, payload);

        {
            std::unique_lock lock{ m };
            m_Cv.wait_for(lock, token, 1ms, []() { return false; });
        }

        m_IsoTp.Poll();
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
        i->count++;
        if(m_EventSink)
            m_EventSink->OnCanFrameTransmitted(frame_id, i->count);
        if(m_Log.IsRecording() && i->log_level >= m_RecordingLogLevel)
        {
            if(i->period == 0 && !i->single_shot)
                i->last_execution = m_Clock.Now();
            m_Log.Append(std::make_unique<CanLogEntry>(CAN_LOG_DIR_TX, frame_id, data, data_len, i->last_execution));
        }
    }

    if(it == entries.end() && m_Log.IsRecording())
    {
        const auto time_now = m_Clock.Now();
        m_Log.Append(std::make_unique<CanLogEntry>(CAN_LOG_DIR_TX, frame_id, data, data_len, time_now));
    }

    NotifyFrameOnBus(frame_id, data, data_len);
    m_TxFrameCount++;
}

void CanEntryHandler::OnFrameReceived(uint32_t frame_id, uint8_t data_len, uint8_t* data)
{
    std::scoped_lock lock{ m };
    const auto time_now = m_Clock.Now();

    /* One lookup. This was a contains() followed by eight operator[] calls on
       the same key, on the path every received frame takes. */
    const auto [entry, inserted] = m_rxData.try_emplace(frame_id);
    if(inserted || !entry->second)
    {
        /* The null check is not reachable through this function - contains()
           ruled it out before - but try_emplace can hand back an entry someone
           else inserted empty, and dereferencing it would be undefined rather
           than wrong. */
        entry->second = std::make_unique<CanRxData>(data, data_len);
    }
    else
    {
        CanRxData& rx = *entry->second;
        rx.data.assign(data, data + data_len);
        rx.period = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            time_now - rx.last_execution).count());
        rx.count++;
    }

    CanRxData& rx = *entry->second;
    rx.last_execution = time_now;

    const auto log_it = m_RxLogLevels.find(frame_id);
    rx.log_level = (log_it != m_RxLogLevels.end()) ? log_it->second : 1;
    m_RxFrameCount++;

    if(m_Log.IsRecording() && rx.log_level >= m_RecordingLogLevel)
        m_Log.Append(std::make_unique<CanLogEntry>(CAN_LOG_DIR_RX, frame_id, data, data_len, rx.last_execution));

    if(frame_id == m_IsoTp.ResponseId())
    {
        if(const auto message = m_IsoTp.OnCanFrame(data, data_len))
            NotifyIsoTpData(frame_id, message->data(), static_cast<uint16_t>(message->size()));
    }

    NotifyFrameOnBus(frame_id, data, data_len);
    m_Cv.notify_all();
}

uint32_t CanEntryHandler::InsertDefaultTxEntryAfter(size_t index)
{
    std::unique_ptr<CanTxEntry> entry = std::make_unique<CanTxEntry>();
    entry->data = { 0, 0, 0, 0, 0, 0, 0, 0 };
    entry->id = kNewTxEntryDefaultId;

    std::scoped_lock lock{ m };
    /* Choosing a free frame id and inserting under it is one critical
       section: run apart, two frames could be handed the same id. */
    while(std::ranges::any_of(entries,
        [frame_id = entry->id](const auto& item) { return item && item->id == frame_id; }))
    {
        entry->id++;
    }
    const uint32_t chosen = entry->id;
    const size_t position = std::min(index, entries.size());
    entries.insert(entries.begin() + position, std::move(entry));
    return chosen;
}

bool CanEntryHandler::DuplicateFirstTxEntry(uint32_t frame_id)
{
    std::scoped_lock lock{ m };
    const auto it = std::ranges::find_if(entries,
        [frame_id](const auto& item) { return item && item->id == frame_id; });
    if(it == entries.end())
        return false;

    /* Duplicated under the lock: the page used to copy the entry through a
       raw grid-row pointer with no lock at all, while the worker stamps
       last_execution on the same entries. */
    entries.push_back(std::make_unique<CanTxEntry>((*it)->Duplicate()));
    return true;
}

void CanEntryHandler::RemoveTxEntries(uint32_t frame_id)
{
    std::scoped_lock lock{ m };
    std::erase_if(entries, [frame_id](auto& item) { return item->id == frame_id; });
}

void CanEntryHandler::RotateTxFrontToBack()
{
    std::scoped_lock lock{ m };
    if(!entries.empty())
        std::rotate(entries.begin(), entries.begin() + 1, entries.end());
}

void CanEntryHandler::RotateTxBackToFront()
{
    std::scoped_lock lock{ m };
    if(!entries.empty())
        std::rotate(entries.rbegin(), entries.rbegin() + 1, entries.rend());
}

std::optional<size_t> CanEntryHandler::SwapTxEntriesById(uint32_t frame_id, uint32_t other_id)
{
    std::scoped_lock lock{ m };
    const auto this_entry = std::ranges::find_if(entries,
        [frame_id](const auto& item) { return item && item->id == frame_id; });
    const auto other_entry = std::ranges::find_if(entries,
        [other_id](const auto& item) { return item && item->id == other_id; });
    if(this_entry == entries.end() || other_entry == entries.end())
        return std::nullopt;

    std::iter_swap(this_entry, other_entry);
    return static_cast<size_t>(std::distance(entries.begin(), other_entry));
}

void CanEntryHandler::ClearRxData()
{
    std::scoped_lock lock{ m };
    m_rxData.clear();
}

void CanEntryHandler::EraseRxData(uint32_t frame_id)
{
    std::scoped_lock lock{ m };
    m_rxData.erase(frame_id);
}

void CanEntryHandler::ToggleAutoSend(bool toggle)
{
    m_AutoSend = toggle;
}

void CanEntryHandler::StartRecording()
{
    std::scoped_lock lock{ m };
    m_Log.SetRecording(true);
}

void CanEntryHandler::PauseRecording()
{
    std::scoped_lock lock{ m };
    m_Log.SetRecording(false);
}

void CanEntryHandler::StopRecording()
{
    std::scoped_lock lock{ m };
    m_Log.SetRecording(false);
    m_TxFrameCount = m_RxFrameCount = 0;
    m_Log.Clear();
}

void CanEntryHandler::ClearRecording()
{
    std::scoped_lock lock{ m };
    m_TxFrameCount = m_RxFrameCount = 0;
    m_Log.Clear();
}

void CanEntryHandler::SendDataFrame(uint32_t frame_id, std::span<const uint8_t> data)
{
    m_CanTransport.Send(frame_id, data);
}

void CanEntryHandler::SendIsoTpFrame(uint32_t frame_id, const uint8_t* data, uint16_t size)
{
    m_IsoTp.Send(frame_id, data, size);
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

        m_Log.SetRecording(m_AutoRecording);
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

std::string CanEntryHandler::CommentForFrame(const CanLogEntry& entry)
{
    if(entry.direction == CAN_LOG_DIR_TX)
    {
        if(auto tx = FindTxCanEntryByFrame(entry.frame_id))
            return tx->get().comment;

        return {};
    }

    const auto it = rx_entry_comment.find(entry.frame_id);
    return it != rx_entry_comment.end() ? it->second : std::string{};
}

std::string CanEntryHandler::FormatLogLine(const CanLogEntry& entry, CanFrameLog::LineStyle style)
{
    return m_Log.FormatLine(entry, CommentForFrame(entry), style);
}

bool CanEntryHandler::SaveRecordingToFile(std::filesystem::path& path)
{
    const auto t1 = m_Clock.Now();

    /* Rendered under the lock, written outside it. This used to hold the model
       lock across the whole file write, so a recording of any size blocked the
       transmit thread for as long as the disk took. */
    std::vector<std::string> lines;
    {
        std::scoped_lock lock{ m };
        if(m_Log.Empty())
            return false;

        lines.reserve(m_Log.Entries().size());
        for(const auto& entry : m_Log.Entries())
            lines.push_back(FormatLogLine(*entry, CanFrameLog::LineStyle::Csv));
    }

    std::ofstream out(path, std::ofstream::binary);
    if(!out.is_open())
    {
        LOG(LogLevel::Error, "Failed to open file for saving CAN recording: {}", path.generic_string());
        return false;
    }

    out << CanFrameLog::kCsvHeader << "\n";
    for(const std::string& line : lines)
        out << line << "\n";
    out.flush();

    const int64_t dif = std::chrono::duration_cast<std::chrono::nanoseconds>(m_Clock.Now() - t1).count();
    if(m_EventSink)
        m_EventSink->OnCanRecordingSaved(path, dif);
    return true;
}

void CanEntryHandler::GenerateLogForFrame(uint32_t frame_id, bool is_rx, std::vector<std::string>& log)
{
    /* The receive thread appends to the log and to rx_entry_comment while
       this walks them, and FindTxCanEntryByFrame reads the entry list without
       locking on the assumption that its caller already has. */
    std::scoped_lock lock(m);

    auto format_entry = [this](const auto& i) { return FormatLogLine(*i, CanFrameLog::LineStyle::Columns); };

    auto filtered = m_Log.Entries()
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

    uint64_t value = get_bitfield(data_ptr->data(), static_cast<uint16_t>(data_size),
        static_cast<uint16_t>(offset), m->m_Size);
    T extracted = static_cast<T>(value);
    info.push_back({
        std::format("{}         (offset: {}, size: {}, range: {} - {})", m->m_Name, offset, m->m_Size, m->m_MinVal, m->m_MaxVal),
        std::to_string(extracted),
        m.get()
    });
}

template <typename T> void CanEntryHandler::HandleBitWriting(uint32_t frame_id, uint8_t& pos, uint8_t offset, uint8_t size, uint8_t* byte_array, std::vector<std::string>& new_data)
{
    if(const auto raw_data = utils::TryParse<int64_t>(new_data[pos]))
        set_bitfield(static_cast<uint64_t>(static_cast<T>(*raw_data)), offset, size, byte_array, 8);
    else
        LOG(LogLevel::Error, "Invalid input for pos {}: '{}' is not a number", pos, new_data[pos]);
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
    return m_IsoTp.MillisecondsSinceLastFrame();
}

void CanSenderSettings::LoadSettings(SettingsReader& reader)
{
    m_Port.SetEnabled(utils::stob(reader.Required("CANSender", "Enable")));
    m_Port.SetComPort(utils::stoi<uint16_t>(reader.Required("CANSender", "COM")));
    m_Port.SetDeviceType(static_cast<CanDeviceType>(utils::stoi<uint8_t>(reader.Required("CANSender", "DeviceType"))));
    m_Entries.ToggleAutoSend(utils::stob(reader.Required("CANSender", "AutoSend")));
    m_Entries.ToggleAutoRecord(utils::stob(reader.Required("CANSender", "AutoRecord")));
    m_Entries.SetRecordingLogLevel(utils::stoi<uint8_t>(reader.Required("CANSender", "DefaultRecordingLogLevel")));
    m_Entries.SetFavouriteLevel(utils::stoi<uint8_t>(reader.Required("CANSender", "DefaultFavouriteLevel")));
    m_Entries.SetDefaultEcuId(static_cast<uint32_t>(std::strtol(reader.Required("CANSender", "DefaultEcuId").c_str(), nullptr, 16)));
    m_Entries.default_tx_list = reader.Required("CANSender", "DefaultTxList");
    m_Entries.default_rx_list = reader.Required("CANSender", "DefaultRxList");
    m_Entries.default_mapping = reader.Required("CANSender", "DefaultMapping");
}

void CanSenderSettings::SaveSettings(std::ostream& out) const
{
    SettingsWriter(out, "CANSender")
        .Key("Enable", m_Port.IsEnabled())
        .Key("COM", m_Port.GetComPort(), "Com port for CAN UART where data is received/sent from/to STM32")
        .Key("DeviceType", static_cast<int>(m_Port.GetDeviceType()), "0 = STM32, 1 = LAWICEL")
        .Key("AutoSend", m_Entries.IsAutoSend())
        .Key("AutoRecord", m_Entries.IsAutoRecord())
        .Key("DefaultRecordingLogLevel", static_cast<int>(m_Entries.GetRecordingLogLevel()))
        .Key("DefaultFavouriteLevel", static_cast<int>(m_Entries.GetFavouriteLevel()))
        .Key("DefaultEcuId", std::format("{:X}", m_Entries.GetDefaultEcuId()))
        .Key("DefaultTxList", m_Entries.default_tx_list.generic_string())
        .Key("DefaultRxList", m_Entries.default_rx_list.generic_string())
        .Key("DefaultMapping", m_Entries.default_mapping.generic_string())
        .Blank();
}
