#include "pch_core.hpp"
#include "ModbusHandler.hpp"
#include "utils/XmlDocument.hpp"
#include "utils/TextStyleXml.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "utils/InterruptibleSleep.hpp"
#include "ModbusJsonPersistence.hpp"
#include "ModbusRegisterValueCodec.hpp"
#include "ModbusValueIo.hpp"
#include "SettingsReader.hpp"
#include "SettingsWriter.hpp"
#include <ostream>

#include <bitfield/bitfield.h>

using namespace std::chrono_literals;

constexpr int NUM_EVENTLOG_ENTRIES = 8;
constexpr int EVENTLOG_ENTRY_SIZE = 9;
constexpr int EVENTLOG_BUFFER_SIZE = NUM_EVENTLOG_ENTRIES * EVENTLOG_ENTRY_SIZE;

constexpr int MAX_INLINE_TIMEOUT_PACKETS = 15;

void ModbusEntryHandler::SetRegisterCount(Table table, size_t count)
{
    std::scoped_lock lock(m);
    switch(table)
    {
        case Table::Coils:       m_layout.counts.coils = count; break;
        case Table::InputStatus: m_layout.counts.inputStatus = count; break;
        case Table::Holding:     m_layout.counts.holdingRegisters = count; break;
        case Table::Input:       m_layout.counts.inputRegisters = count; break;
    }
}

void ModbusEntryHandler::ClearValues()
{
    std::scoped_lock lock(m);
    modbus_values::Clear(m_layout);
}

void ModbusEntryHandler::ExportValues(std::filesystem::path& path)
{
    /* Rendering used to walk the four tables unlocked while the polling worker
       wrote the same items. The lock is held for the render and released before
       the file is touched. */
    std::string out;
    {
        std::scoped_lock lock(m);
        out = modbus_values::Format(m_layout);
    }

    std::ofstream file(path);
    if(!file)
    {
        LOG(LogLevel::Error, "Failed to open file for saving modbus values: {}", path.generic_string());
        return;
    }

    file << out;
    file.flush();
}

void ModbusEntryHandler::ImportValues(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if(!file)
    {
        LOG(LogLevel::Error, "Failed to open file for loading modbus values: {}", path.generic_string());
        return;
    }

    const std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };

    /* Same again: the model is updated under the lock, and the writes it asks
       for are queued afterwards - the write queue takes its own lock, and `m`
       is not recursive. */
    std::vector<ModbusWrite> writes;
    {
        std::scoped_lock lock(m);
        writes = modbus_values::Apply(m_layout, text);
    }

    for(const ModbusWrite& write : writes)
        m_WriteQueue.Push(write);

    {
        std::scoped_lock helper_lock(m_helperMutex);
        if(m_helper)
            m_helper->RefreshItems();
    }
}

ModbusEntryHandler::ModbusEntryHandler(IModbusEntryLoader& loader, IModbusEventSink* event_sink)
    : m_ModbusEntryLoader(loader), m_EventSink(event_sink)
{
    start_time = std::chrono::steady_clock::now();
    m_Serial = std::make_unique<ModbusMasterSerialPort>();
    m_Serial->SetRecorder(this);
}

ModbusEntryHandler::~ModbusEntryHandler()
{
    Shutdown();
    m_Serial.reset();
}

void ModbusEntryHandler::Init()
{
    /* A failed load used to leave the four tables cleared but keep going, so a
       missing configuration file looked like a device with no registers. */
    if(auto layout = m_ModbusEntryLoader.Load(m_DefaultConfigName, m_used_branch, m_layout.slaveId))
        m_layout = std::move(*layout);
    else
        LOG(LogLevel::Error, "Could not load Modbus configuration '{}'", m_DefaultConfigName);

    m_Log.SetRecording(auto_recording);
}

void ModbusEntryHandler::Start()
{
    if(m_workerModbus)
        return;
    if(is_enabled)
        m_Serial->Init();
    m_workerModbus = utils::StartNamedWorker("ModbusWorker",
        std::bind_front(&ModbusEntryHandler::ModbusWorker, this));
}

void ModbusEntryHandler::StopWorker()
{
    if(!m_workerModbus)
        return;
    m_workerModbus->request_stop();
    cv.notify_all();
    m_workerModbus->join();
    m_workerModbus.reset();
}

void ModbusEntryHandler::Shutdown()
{
    {
        std::scoped_lock helper_lock(m_helperMutex);
        m_helper = nullptr;
    }
    m_isMainThreadPaused = false;
    cv.notify_all();
    StopWorker();
    if(m_Serial)
        m_Serial->DeInitInternal();
}

void ModbusEntryHandler::Save()
{
    const std::filesystem::path configured_path(m_DefaultConfigName);
    const std::filesystem::path save_path = configured_path.extension() == ".json"
        ? configured_path : std::filesystem::path("Modbus2.xml");
    m_ModbusEntryLoader.Save(save_path, m_layout);
}

std::vector<std::string> ModbusEntryHandler::GetAvailableDevices() const
{
    /* A single-device layout has no catalog at all, which is a different thing
       from a catalog that happens to be empty. */
    const IModbusDeviceCatalog* catalog = m_ModbusEntryLoader.DeviceCatalog();
    return catalog ? catalog->GetAvailableDevices(m_DefaultConfigName) : std::vector<std::string>{};
}

std::string ModbusEntryHandler::GetSelectedDevice() const
{
    const IModbusDeviceCatalog* catalog = m_ModbusEntryLoader.DeviceCatalog();
    return catalog ? catalog->GetSelectedDevice() : std::string{};
}

bool ModbusEntryHandler::ChangeDevice(const std::string& device)
{
    IModbusDeviceCatalog* catalog = m_ModbusEntryLoader.DeviceCatalog();
    if(!catalog)
    {
        LOG(LogLevel::Warning, "The configured Modbus layout holds a single device; cannot switch to {}", device);
        return false;
    }

    const bool was_running = !m_isMainThreadPaused.load(std::memory_order_acquire);
    StopWorker();
    if(!catalog->SelectDevice(device))
    {
        if(was_running)
            Start();
        return false;
    }

    /* Load into a scratch layout, then swap it in under the lock. Six separate
       locals used to stand in for the value this now is. */
    std::optional<ModbusDeviceLayout> layout = m_ModbusEntryLoader.Load(m_DefaultConfigName, m_used_branch,
        GetSlaveId());
    if(layout)
    {
        std::scoped_lock lock(m);
        m_layout = std::move(*layout);
        /* The queued ids point into the model that is being replaced. */
        m_WriteQueue.Clear();
    }
    if(was_running)
        Start();
    return layout.has_value();
}

void ModbusEntryHandler::SetValueObserver(IModbusValueObserver* helper)
{
    std::scoped_lock lock(m_helperMutex);
    m_helper = helper;
}

void ModbusEntryHandler::SetEnabled(bool enable)
{
    m_Serial->SetEnabled(enable);
    is_enabled = enable;
    if(enable && m_workerModbus)
        m_Serial->Init();
    else if(!enable)
    {
        if(m_Serial->IsOpen())
            m_Serial->Close();
        m_Serial->DeInitInternal();
    }
}

bool ModbusEntryHandler::IsEnabled() const
{
    return is_enabled;
}

void ModbusEntryHandler::SetPollingStatus(bool is_active)
{
    std::lock_guard lock(m);
    m_isMainThreadPaused = !is_active;
    if(is_active)
        cv.notify_one();
}

void ModbusEntryHandler::ToggleAutoSend(bool toggle)
{
    auto_send = toggle;
    SetPollingStatus(toggle);
}

void ModbusEntryHandler::StartRecording()
{
    m_Log.SetRecording(true);
}

void ModbusEntryHandler::PauseRecording()
{
    m_Log.SetRecording(false);
}

void ModbusEntryHandler::StopRecording()
{
    m_Log.SetRecording(false);
    tx_frame_cnt = rx_frame_cnt = err_frame_cnt = 0;
    m_Log.Clear();
}

void ModbusEntryHandler::ClearRecording()
{
    tx_frame_cnt = rx_frame_cnt = err_frame_cnt = 0;
    m_Log.Clear();
}

void ModbusEntryHandler::NotifySaved(const std::filesystem::path& path, int64_t duration_ns)
{
    if(m_EventSink)
        m_EventSink->OnModbusRecordingSaved(path, duration_ns);
}

bool ModbusEntryHandler::SaveRecordingToFile(std::filesystem::path& path)
{
    const auto t1 = std::chrono::steady_clock::now();
    if(!m_Log.ExportFrames(path, start_time))
        return false;

    NotifySaved(path, std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - t1).count());
    return true;
}

bool ModbusEntryHandler::SaveSpecialRecordingToFile(std::filesystem::path& path)
{
    const auto t1 = std::chrono::steady_clock::now();
    if(!m_Log.ExportEvents(path, start_time))
        return false;

    NotifySaved(path, std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - t1).count());
    return true;
}

// IModbusRecorder
bool ModbusEntryHandler::IsReady() const
{
    return m_isReady.load(std::memory_order_relaxed);
}

void ModbusEntryHandler::RecordFrame(uint8_t direction, uint8_t fc, ModbusErrorType error, const uint8_t* data, size_t len)
{
    m_Log.Append(direction, fc, error, data, len);
}

void ModbusEntryHandler::CheckMaxEntries()
{
    m_Log.TrimIfFull();
}

template <typename T> void ModbusEntryHandler::HandleBitReading(size_t id, bool is_holding, std::unique_ptr<ModbusMap>& m, size_t offset, ModbusBitfieldInfo& info)
{
    /* A copy, not a pointer into the item: get_bitfield wants raw bytes, and
       aliasing the live register as uint8_t only ever worked because the value
       happened to be stored as a bare uint64_t. */
    uint64_t raw = (is_holding ? m_layout.holding[id] : m_layout.input[id])->m_Value.Integer();
    uint64_t value = get_bitfield(reinterpret_cast<uint8_t*>(&raw), sizeof(raw),
        static_cast<uint16_t>(offset), m->m_Size);
    T extracted_data = static_cast<T>(value);
    info.push_back({std::format("{}         (offset: {}, size: {}, range: {} - {})", m->m_Name, offset, m->m_Size, m->m_MinVal, m->m_MaxVal), std::to_string(extracted_data), m.get() });
}

template <typename T> void ModbusEntryHandler::HandleBitWriting(size_t id, uint8_t& pos, uint8_t offset, uint8_t size, uint8_t* byte_array, std::vector<std::string>& new_data)
{
    if(const auto raw_data = utils::TryParse<int64_t>(new_data[pos]))
    {
        /* byte_array decays to a pointer here, so sizeof(byte_array) measured the
           pointer and matched the real length only by coincidence. */
        set_bitfield(static_cast<uint64_t>(static_cast<T>(*raw_data)), offset, size, byte_array, 8);
    }
    else
        LOG(LogLevel::Error, "Invalid input for pos {}: '{}' is not a number", pos, new_data[pos]);

    pos++;
}

ModbusBitfieldInfo ModbusEntryHandler::GetMapForHolding(size_t id, bool is_holding)
{
    ModbusBitfieldInfo info;
    const auto& src = is_holding ? m_layout.holding : m_layout.input;
    if (id >= src.size()) return info;

    for (auto& [offset, m] : src.at(id)->m_Mapping)
    {
        DispatchModbusBitfieldType(m->m_Type, [&]<typename T>()
        {
            HandleBitReading<T>(id, is_holding, m, offset, info);
        });
    }
    return info;
}

void ModbusEntryHandler::ApplyEditingOnHolding(size_t id, std::vector<std::string> new_data)
{
    if (id >= m_layout.holding.size()) return;

    uint8_t cnt = 0;
    std::array<uint8_t, 8> byte_array{};
    byte_array = std::bit_cast<std::array<uint8_t, 8>>(m_layout.holding.at(id)->m_Value.Integer());

    for (auto& [offset, m] : m_layout.holding.at(id)->m_Mapping)
    {
        DispatchModbusBitfieldType(m->m_Type, [&]<typename T>()
        {

            HandleBitWriting<T>(id, cnt, offset, m->m_Size, byte_array.data(), new_data);
        });
    }

    /* bit_cast instead of a reinterpreting pointer cast: the old form aliased a
       uint8_t array as a uint64_t, which the optimiser is free to break. */
    const uint64_t value = std::bit_cast<uint64_t>(byte_array);
    m_layout.holding.at(id)->m_Value.SetInteger(value);
    EditHolding(id, value);
}

void ModbusEntryHandler::HandleBoolReadingByOffset(const std::map<size_t, uint8_t>& reg,
    ModbusItemType& items, IModbusValueObserver::Table table)
{
    std::vector<uint8_t> changed_rows;
    {
        std::scoped_lock state_lock(m);
        for(size_t index = 0; index < items.size(); ++index)
        {
            auto& item = items[index];
            if(!item || !(item->branches & m_used_branch))
                continue;
            const auto value = reg.find(item->m_Offset);
            if(value == reg.end() || !item->m_Value.SetInteger(value->second))
                continue;
            if(index <= std::numeric_limits<uint8_t>::max())
                changed_rows.push_back(static_cast<uint8_t>(index));
        }
    }
    if(!changed_rows.empty())
    {
        std::scoped_lock helper_lock(m_helperMutex);
        if(m_helper)
            m_helper->QueueValueChanges(table, changed_rows);
    }
}

void ModbusEntryHandler::HandleRegisterReadingByOffset(const std::map<size_t, uint16_t>& reg,
    ModbusItemType& items, IModbusValueObserver::Table table)
{
    std::vector<uint8_t> changed_rows;
    auto word = [&](size_t offset) -> std::optional<uint16_t>
    {
        const auto found = reg.find(offset);
        return found == reg.end() ? std::nullopt : std::optional<uint16_t>(found->second);
    };

    {
        std::scoped_lock state_lock(m);
        for(size_t index = 0; index < items.size(); ++index)
        {
            auto& item = items[index];
            if(!item || !(item->branches & m_used_branch))
                continue;
            const auto w0 = word(item->m_Offset);
            if(!w0)
                continue;

            /* How wide a value is and whether it is floating point are two
               columns of modbus_types::kTraits. This was a switch that grouped
               the types by width and then asked m_Type a second time inside
               two of its arms to pick float from integer. */
            if(item->m_Type == MBT_STRING || item->m_Type == MBT_INVALID)
                continue;

            const ModbusTypeTraits& traits = modbus_types::Of(item->m_Type);

            std::array<uint16_t, 4> words{ *w0, 0, 0, 0 };
            bool complete = true;
            for(uint8_t offset = 1; offset < traits.register_count && complete; ++offset)
            {
                if(const auto next = word(item->m_Offset + offset))
                    words[offset] = *next;
                else
                    complete = false;
            }
            if(!complete)
                continue;

            bool changed = false;
            if(traits.is_floating)
            {
                changed = traits.register_count == 2
                    ? item->m_Value.SetFloat(
                        DecodeModbusRegisterFloat(words[0], words[1], item->m_NetworkByteOrder))
                    : item->m_Value.SetDouble(
                        DecodeModbusRegisterDouble(words[0], words[1], words[2], words[3],
                            item->m_NetworkByteOrder));
            }
            else
            {
                uint64_t value = 0;
                if(traits.register_count == 1)
                    value = DecodeModbusRegister16(words[0], item->m_NetworkByteOrder);
                else if(traits.register_count == 2)
                    value = DecodeModbusRegister32(words[0], words[1], item->m_NetworkByteOrder);
                else
                    value = DecodeModbusRegister64(words[0], words[1], words[2], words[3],
                        item->m_NetworkByteOrder);

                changed = item->m_Value.SetInteger(value);
            }

            if(changed && index <= std::numeric_limits<uint8_t>::max())
                changed_rows.push_back(static_cast<uint8_t>(index));
        }
    }

    if(!changed_rows.empty())
    {
        std::scoped_lock helper_lock(m_helperMutex);
        if(m_helper)
            m_helper->QueueValueChanges(table, changed_rows);
    }
}

std::optional<GroupedModbusRegisterReadResult> ModbusEntryHandler::ReadRegisterGroups(
    const ModbusItemType& items, RegisterTable table)
{
    const uint16_t base = table == RegisterTable::Holding ? m_layout.counts.holdingOffset : m_layout.counts.inputOffset;
    return ReadGroupedModbusRegisters(items, m_used_branch,
        [&](uint16_t offset, uint16_t count) -> std::expected<std::vector<uint16_t>, ModbusError>
        {
            return table == RegisterTable::Holding
                ? m_Serial->ReadHoldingRegisters(GetSlaveId(), offset, count)
                : m_Serial->ReadInputRegisters(GetSlaveId(), offset, count);
        }, base);
}

std::optional<GroupedModbusBitReadResult> ModbusEntryHandler::ReadBitGroups(
    const ModbusItemType& items, bool input_status)
{
    const uint16_t base = input_status ? m_layout.counts.inputStatusOffset : m_layout.counts.coilsOffset;
    return ReadGroupedModbusBits(items, m_used_branch,
        [&](uint16_t offset, uint16_t count) -> std::expected<std::vector<uint8_t>, ModbusError>
        {
            return input_status ? m_Serial->ReadInputStatus(GetSlaveId(), offset, count)
                : m_Serial->ReadCoilStatus(GetSlaveId(), offset, count);
        }, base);
}

void ModbusEntryHandler::CloseConnectionIfOpen()
{
    if(!GetSerial().IsOpen())
        return;

    try
    {
        GetSerial().Close();
        LOG(LogLevel::Notification, "Closing modbus connection");
    }
    catch(boost::system::system_error&)
    {
        /* Swallowed, as it always was: the worker's next pass finds the port
           shut or tries again. */
    }
}

void ModbusEntryHandler::OpenConnection()
{
    /* Both waits are inherited. The first spaces out reopen attempts; the
       second backs off after one that did not take. */
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    LOG(LogLevel::Notification, "Opening modbus connection");
    GetSerial().Open();
    if(!GetSerial().IsOpen())
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

bool ModbusEntryHandler::WaitIfPaused(std::stop_token token)
{
    std::unique_lock lk(m);
    cv.wait(lk, token, [this] { return !m_isMainThreadPaused; });
    return !token.stop_requested();
}

void ModbusEntryHandler::PollBitTable(ModbusItemType& items, bool input_status,
    IModbusValueObserver::Table table)
{
    if(items.empty())
        return;

    const auto result = ReadBitGroups(items, input_status);
    if(!result)
        return;

    CountPolledFrames(*result);
    HandleBoolReadingByOffset(result->values, items, table);
}

void ModbusEntryHandler::PollRegisterTable(ModbusItemType& items, RegisterTable source,
    IModbusValueObserver::Table table)
{
    if(items.empty())
        return;

    const auto result = ReadRegisterGroups(items, source);
    if(!result)
        return;

    CountPolledFrames(*result);
    HandleRegisterReadingByOffset(result->values, items, table);
}

void ModbusEntryHandler::HandlePolling()
{
    PollBitTable(m_layout.coils, false, IModbusValueObserver::Table::Coils);
    PollBitTable(m_layout.inputStatus, true, IModbusValueObserver::Table::InputStatus);
    PollRegisterTable(m_layout.holding, RegisterTable::Holding, IModbusValueObserver::Table::Holding);
    PollRegisterTable(m_layout.input, RegisterTable::Input, IModbusValueObserver::Table::Input);
}

std::optional<uint16_t> ModbusEntryHandler::ResolveAddress(const ModbusItem& item, uint16_t table_offset) const
{
    const size_t offset = item.m_ManualAddress >= 0
        ? static_cast<size_t>(item.m_ManualAddress) : item.m_Offset;
    if(offset > static_cast<size_t>(std::numeric_limits<uint16_t>::max()) - table_offset)
        return std::nullopt;
    return static_cast<uint16_t>(table_offset + offset);
}

void ModbusEntryHandler::SendHoldingRegisters(const ModbusItem& item, const std::vector<uint16_t>& values)
{
    const auto address = ResolveAddress(item, m_layout.counts.holdingOffset);
    if(!address)
    {
        err_frame_cnt++;
        return;
    }

    const auto reg = m_Serial->WriteHoldingRegister(GetSlaveId(), *address,
        static_cast<uint16_t>(values.size()), values);
    if(reg.has_value() && !reg->empty()) { tx_frame_cnt++; rx_frame_cnt++; }
    else err_frame_cnt++;
}

void ModbusEntryHandler::ApplyWrite(const ModbusCoilWrite& write)
{
    if(write.id >= m_layout.coils.size())
        return;

    const auto address = ResolveAddress(*m_layout.coils[write.id], m_layout.counts.coilsOffset);
    if(!address)
    {
        err_frame_cnt++;
        return;
    }

    const auto reg = m_Serial->ForceSingleCoil(GetSlaveId(), *address, write.value);
    if(reg.has_value() && !reg->empty()) { tx_frame_cnt++; rx_frame_cnt++; }
    else err_frame_cnt++;
}

void ModbusEntryHandler::ApplyWrite(const ModbusHoldingWrite& write)
{
    if(write.id >= m_layout.holding.size())
        return;
    const auto& item = *m_layout.holding[write.id];
    SendHoldingRegisters(item, EncodeModbusRegisterValue(item, write.value));
}

void ModbusEntryHandler::ApplyWrite(const ModbusFloatWrite& write)
{
    if(write.id >= m_layout.holding.size())
        return;
    const auto& item = *m_layout.holding[write.id];
    SendHoldingRegisters(item, EncodeModbusRegisterFloatValue(item, write.value));
}

void ModbusEntryHandler::ApplyWrite(const ModbusDoubleWrite& write)
{
    if(write.id >= m_layout.holding.size())
        return;
    const auto& item = *m_layout.holding[write.id];
    SendHoldingRegisters(item, EncodeModbusRegisterDoubleValue(item, write.value));
}

void ModbusEntryHandler::HandleWrites()
{
    /* One queue, drained in the order the edits were made. It used to be four
       vectors drained by type, so a coil edit always went out before a holding
       edit made earlier. */
    for(const ModbusWrite& write : m_WriteQueue.Drain())
        std::visit([this](const auto& typed) { ApplyWrite(typed); }, write);
}

void ModbusEntryHandler::ModbusWorker(std::stop_token token)
{
    m_Serial->SetStopToken(token);
    while(!token.stop_requested())
    {
        while(!token.stop_requested() && !m_Serial->IsInstanceInited())
        {
            utils::InterruptibleSleep(cv, m, token, 10ms);
        }
        if(token.stop_requested())
            break;

        /* m_isOpenInProgress and m_isCloseInProgress guarded these two blocks.
           Both were plain bools written only here, on this thread, set true
           immediately before a synchronous call and false immediately after -
           so !m_isOpenInProgress and !m_isCloseInProgress were always true when
           tested, and the assignment above the close was setting a flag that
           was already false. Neither Open nor Close reaches back into this
           handler, so there was no re-entrancy for them to guard either. */
        if(m_isMainThreadPaused)
            CloseConnectionIfOpen();

        if(!WaitIfPaused(token))
            break;

        if (is_enabled)
        {
            if(!GetSerial().IsOpen())
                OpenConnection();

            if (!token.stop_requested() && GetSerial().IsOpen())
            {
                HandlePolling();
                HandleWrites();

                if (GetSerial().GetTimeoutPackets() > MAX_INLINE_TIMEOUT_PACKETS)
                {
                    LOG(LogLevel::Error, "Modbus connection lost");
                    GetSerial().Close();
                    GetSerial().ResetTimeoutPackets();
                }

                {
                    utils::InterruptibleSleep(cv, m, token, std::chrono::milliseconds(GetPollingRate()));
                }
            }
            else if(!token.stop_requested())
            {
                utils::InterruptibleSleep(cv, m, token, 100ms);
            }
        }
    }
}

const std::map<uint32_t, std::string> ModbusEntryHandler::gModbusBranches = {
    {0, "unknown"},
    {1, "default"},
    {2, "branch_1"},
    {4, "branch_2"}
};

std::string ModbusEntryHandler::getBranchNameByID(uint32_t id)
{
    auto it = gModbusBranches.find(id);
    return it != gModbusBranches.end() ? it->second : gModbusBranches.at(0);
}

uint32_t ModbusEntryHandler::getBranchIDByName(const std::string& name)
{
    for (const auto& pair : gModbusBranches)
        if (pair.second == name) return pair.first;
    return 0;
}

void ModbusEntryHandler::LoadSettings(SettingsReader& reader)
{
    SetEnabled(utils::stob(reader.Required("ModbusMaster", "Enable")));
    GetSerial().SetTcp(reader.Required("ModbusMaster", "ConnectionType") == "TCP");
    GetSerial().SetTcpIp(reader.Required("ModbusMaster", "TcpIp"));
    GetSerial().SetTcpPort(utils::stoi<uint16_t>(reader.Required("ModbusMaster", "TcpPort")));
    GetSerial().SetComPort(utils::stoi<uint16_t>(reader.Required("ModbusMaster", "COM")));
    SetPollingRate(utils::stoi<uint16_t>(reader.Required("ModbusMaster", "PollingRate")));
    GetSerial().m_ResponseTimeout = utils::stoi<uint16_t>(reader.Required("ModbusMaster", "ResponseTimeout"));
    SetDefaultConfigName(reader.Required("ModbusMaster", "DefaultModbusConfig"));
    ToggleAutoSend(utils::stob(reader.Required("ModbusMaster", "AutoSend")));
    ToggleAutoRecord(utils::stob(reader.Required("ModbusMaster", "AutoRecord")));
    SetMaxRecordedEntries(utils::stoi<size_t>(reader.Required("ModbusMaster", "MaxRecordedEntries")));
    SetDefaultBranch(reader.Required("ModbusMaster", "Branch"));

    /* Optional: a single-device layout has no Device key. */
    if(const auto section = reader.OptionalSection("ModbusMaster"))
        if(const auto device = section->get_optional<std::string>("Device"))
            (void)ChangeDevice(*device);
}

void ModbusEntryHandler::SaveSettings(std::ostream& out) const
{
    SettingsWriter writer(out, "ModbusMaster");
    writer.Key("Enable", IsEnabled())
        .Key("ConnectionType", m_Serial->IsTcp() ? "TCP" : "RTU")
        .Key("TcpIp", m_Serial->GetTcpIp())
        .Key("TcpPort", m_Serial->GetTcpPort())
        .Key("COM", m_Serial->GetComPort(), "Com port for Modbus Master UART where data is received/sent from/to Modbus")
        .Key("PollingRate", GetPollingRate())
        .Key("ResponseTimeout", m_Serial->m_ResponseTimeout)
        .Key("DefaultModbusConfig", GetDefaultConfigName())
        .Key("AutoSend", IsAutoSend())
        .Key("AutoRecord", IsAutoRecord())
        .Key("MaxRecordedEntries", GetMaxRecordedEntries())
        .Key("Branch", GetDefaultBranch());

    /* The only optional key in the file: an empty device is left out rather
       than written blank, because the loader treats absent and empty alike. */
    if(!GetSelectedDevice().empty())
        writer.Key("Device", GetSelectedDevice());

    writer.Blank();
}
