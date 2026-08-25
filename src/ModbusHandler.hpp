#pragma once

#include "IModbusEntry.hpp"
#include "IModbusRecorder.hpp"
#include "IModbusEventSink.hpp"
#include "ModbusRegisterGrouping.hpp"
#include "ModbusWriteQueue.hpp"
#include "ModbusFrameLog.hpp"
#include "ModbusTypeTraits.hpp"
#include "ModbusItemRender.hpp"
#include <atomic>
#include <map>
#include "ModbusMasterSerialPort.hpp"
#include "interface/ISettingsBinding.hpp"
#include <iosfwd>
#include <string_view>

class XmlModbusEntryLoader : public IModbusEntryLoader
{
public:
    virtual ~XmlModbusEntryLoader() = default;

    [[nodiscard]] std::optional<ModbusDeviceLayout> Load(const std::filesystem::path& path, uint32_t branch,
        uint8_t fallback_slave_id) override;
    bool Save(const std::filesystem::path& path, const ModbusDeviceLayout& layout) const override;

    // The names live in modbus_types::kTraits with everything else about a
    // type; these stay as the spelling the loaders already use.
    static ModbusBitfieldType GetTypeFromString(const std::string_view& input)
    {
        return modbus_types::FromName(input);
    }

    static std::string_view GetStringFromType(ModbusBitfieldType type)
    {
        return modbus_types::NameOf(type);
    }
};

// Preserves the existing XML format while also accepting the multi-device
// JSON format used by PGL_DebugApp.
class HybridModbusEntryLoader : public IModbusEntryLoader, public IModbusDeviceCatalog
{
public:
    // The JSON format is the multi-device one, so this loader is also a catalog.
    [[nodiscard]] IModbusDeviceCatalog* DeviceCatalog() override { return this; }

    [[nodiscard]] std::optional<ModbusDeviceLayout> Load(const std::filesystem::path& path, uint32_t branch,
        uint8_t fallback_slave_id) override;
    bool Save(const std::filesystem::path& path, const ModbusDeviceLayout& layout) const override;
    [[nodiscard]] std::vector<std::string> GetAvailableDevices(const std::filesystem::path& path) const override;
    bool SelectDevice(const std::string& device) override;
    [[nodiscard]] std::string GetSelectedDevice() const override { return m_selectedDevice; }

private:
    static bool IsJson(const std::filesystem::path& path);

    XmlModbusEntryLoader m_xml;
    std::string m_selectedDevice;
};

using ModbusBitfieldInfo = std::vector<std::tuple<std::string, std::string, ModbusMap*>>;

class ModbusMasterSerialPort;
class ModbusEntryHandler : public IModbusRecorder, public ISettingsBinding
{
public:
    // ISettingsBinding - this subsystem owns its own block of settings.ini.
    [[nodiscard]] std::string_view SettingsSection() const override { return "ModbusMaster"; }
    void LoadSettings(SettingsReader& reader) override;
    void SaveSettings(std::ostream& out) const override;

    ModbusEntryHandler(IModbusEntryLoader& loader, IModbusEventSink* event_sink = nullptr);
    ~ModbusEntryHandler();

    void Init();
    void Start();
    void Shutdown();
    void Save();
    void ClearValues();
    void ExportValues(std::filesystem::path& path);
    void ImportValues(const std::filesystem::path& path);
    void SetValueObserver(IModbusValueObserver* helper);
    void SetEnabled(bool enable);
    bool IsEnabled() const;
    void SetPollingStatus(bool is_active);
    void ToggleAutoRecord(bool toggle) { auto_recording = toggle; }
    bool IsAutoRecord() const { return auto_recording; }
    bool IsAutoSend() const { return auto_send; }
    void ToggleAutoSend(bool toggle);
    // !\brief Begin recording, keeping whatever is already captured.
    void StartRecording();
    // !\brief Stop capturing but keep what was captured.
    void PauseRecording();
    // !\brief Stop capturing and discard what was captured.
    //
    // These three were ToggleRecording(bool toggle, bool is_pause), called at
    // nine places with exactly three of the four argument combinations - the
    // fourth, (true, true), does not mean anything.
    void StopRecording();
    void ClearRecording();
    bool SaveRecordingToFile(std::filesystem::path& path);
    bool SaveSpecialRecordingToFile(std::filesystem::path& path);
    void SetPollingRate(uint16_t rate_ms) { m_pollingRate.store(rate_ms, std::memory_order_relaxed); }
    uint16_t GetPollingRate() const { return m_pollingRate.load(std::memory_order_relaxed); }
    void SetSlaveId(uint8_t slave_id) { std::scoped_lock lock(m); m_layout.slaveId = slave_id; }
    uint8_t GetSlaveId() const { std::scoped_lock lock(m); return m_layout.slaveId; }
    void SetDefaultConfigName(const std::string& default_config) { m_DefaultConfigName = default_config; }
    const std::string& GetDefaultConfigName() const { return m_DefaultConfigName; }
    uint8_t GetFavouriteLevel() const { return m_DefaultFavouriteLevel; }
    void SetFavouriteLevel(uint8_t favourite_level) { m_DefaultFavouriteLevel = favourite_level; }
    void SetMaxRecordedEntries(size_t max_entries) { m_Log.SetMaxEntries(max_entries); }
    size_t GetMaxRecordedEntries() const { return m_Log.GetMaxEntries(); }

    // !\brief The recording buffer. Rendering walks it incrementally, so it is
    // exposed rather than copied out on every refresh.
    [[nodiscard]] ModbusFrameLog& FrameLog() { return m_Log; }
    [[nodiscard]] const ModbusFrameLog& FrameLog() const { return m_Log; }
    void SetDefaultBranch(const std::string& branch)
    {
        m_DefaultBranch = branch;
        m_used_branch = static_cast<uint16_t>(getBranchIDByName(branch));
    }
    const std::string& GetDefaultBranch() const { return m_DefaultBranch; }
    std::vector<std::string> GetAvailableDevices() const;
    std::string GetSelectedDevice() const;
    bool ChangeDevice(const std::string& device);
    // Edits are queued and applied on the next poll, in the order they were made.
    void EditCoil(size_t id, bool value) { m_WriteQueue.Push(ModbusCoilWrite{ id, value }); }
    void EditHolding(size_t id, uint64_t value) { m_WriteQueue.Push(ModbusHoldingWrite{ id, value }); }
    void EditHoldingFloat(size_t id, float value) { m_WriteQueue.Push(ModbusFloatWrite{ id, value }); }
    void EditHoldingDouble(size_t id, double value) { m_WriteQueue.Push(ModbusDoubleWrite{ id, value }); }

    template <typename T> void HandleBitReading(size_t id, bool is_holding, std::unique_ptr<ModbusMap>& m, size_t offset, ModbusBitfieldInfo& info);
    template <typename T> void HandleBitWriting(size_t id, uint8_t& pos, uint8_t offset, uint8_t size, uint8_t* byte_array, std::vector<std::string>& new_data);

    ModbusBitfieldInfo GetMapForHolding(size_t id, bool is_holding);
    void ApplyEditingOnHolding(size_t id, std::vector<std::string> new_data);

    ModbusMasterSerialPort& GetSerial() { return *m_Serial; }
    size_t GetTxFrameCount() const { return tx_frame_cnt.load(std::memory_order_relaxed); }
    size_t GetRxFrameCount() const { return rx_frame_cnt.load(std::memory_order_relaxed); }
    size_t GetErrFrameCount() const { return err_frame_cnt.load(std::memory_order_relaxed); }
    std::chrono::steady_clock::time_point GetStartTime() const { std::scoped_lock lock(m); return start_time; }

    // !\brief Frame recording starts only once the composition root has finished
    // wiring everything up. The flag is pushed in from there rather than read
    // back out of the wxWidgets application object, so the handler stays usable
    // without a GUI.
    void SetReady(bool ready) { m_isReady.store(ready, std::memory_order_relaxed); }

    // IModbusRecorder
    bool IsRecording() const override { return m_Log.IsRecording(); }
    bool IsReady() const override;
    void RecordFrame(uint8_t direction, uint8_t fc, ModbusErrorType error, const uint8_t* data, size_t len) override;
    void CheckMaxEntries() override;

    static std::string getBranchNameByID(uint32_t id);
    static uint32_t getBranchIDByName(const std::string& name);
    static const std::map<uint32_t, std::string> gModbusBranches;

    // !\brief Which register table an operation addresses.
    using Table = IModbusValueObserver::Table;

    // !\brief Run `fn` over one register table with the model lock held.
    //
    // The four tables used to be public members. The GUI took a reference to
    // one at construction and read the items on its 10 ms timer while the
    // polling worker wrote the same items under `m` - a data race that the
    // mutex sitting next to them did nothing to prevent, because nothing
    // obliged a reader to take it. One call site out of thirty did.
    //
    // Reaching a table only through a visitor makes the lock impossible to
    // forget. `fn` receives `ModbusItemType&` (or a const reference from the
    // const overload) and may return a value, which is forwarded on.
    //
    // `fn` must not call back into a handler method that takes the model lock -
    // SetSlaveId, GetSlaveId, GetStartTime, ChangeDevice, SetPollingStatus,
    // ApplyEditingOnHolding or GetMapForHolding - because `m` is not recursive.
    template <typename F> decltype(auto) WithTable(Table table, F&& fn)
    {
        std::scoped_lock lock(m);
        return std::forward<F>(fn)(TableRef(table));
    }

    template <typename F> decltype(auto) WithTable(Table table, F&& fn) const
    {
        std::scoped_lock lock(m);
        return std::forward<F>(fn)(TableRef(table));
    }

    // !\brief How many items a table holds.
    [[nodiscard]] size_t ItemCount(Table table) const
    {
        std::scoped_lock lock(m);
        return TableRef(table).size();
    }

    // !\brief How one item should currently appear, or nothing when `index` is
    // no longer in range - which happens when the device changed underneath the
    // caller.
    [[nodiscard]] std::optional<ModbusCellRender> RenderItem(Table table, size_t index) const
    {
        std::scoped_lock lock(m);
        const ModbusItemType& items = TableRef(table);
        if(index >= items.size() || !items[index])
            return std::nullopt;
        return RenderModbusItem(*items[index]);
    }

    // !\brief One register's current value as a number, for the live graph, or
    // nothing when `index` no longer exists.
    [[nodiscard]] std::optional<double> SampleValue(Table table, size_t index) const
    {
        std::scoped_lock lock(m);
        const ModbusItemType& items = TableRef(table);
        if(index >= items.size() || !items[index])
            return std::nullopt;
        return GetModbusItemDisplayNumericValue(*items[index]);
    }

    // !\brief The configured entry counts, as an owning copy.
    [[nodiscard]] NumModbusEntries EntryCounts() const
    {
        std::scoped_lock lock(m);
        return m_layout.counts;
    }

    // !\brief Record how many registers a table now spans, after the register
    // editor has added or removed one.
    void SetRegisterCount(Table table, size_t count);

private:
    enum class RegisterTable { Holding, Input };

    // !\brief The entry list for one Modbus table.
    //
    // One function for both constnesses. It used to be two, and the const one
    // cast away its own constness to call the other - legal, because the object
    // is never actually const, but a cast every reader has to re-justify. An
    // explicit object parameter deduces what that cast was reinstating.
    [[nodiscard]] auto& TableRef(this auto& self, Table table)
    {
        switch(table)
        {
            case Table::Coils:       return self.m_layout.coils;
            case Table::InputStatus: return self.m_layout.inputStatus;
            case Table::Holding:     return self.m_layout.holding;
            case Table::Input:       return self.m_layout.input;
        }
        return self.m_layout.coils;
    }

    /* The slave address, the four register tables and their configured counts
       were six separate members that the loader filled through six
       out-parameters. One value makes "load into a scratch copy, then swap it
       in under the lock" expressible - which is what Init and ChangeDevice
       need and could not previously say. */
    ModbusDeviceLayout m_layout;

    bool auto_send = false;
    bool auto_recording = false;
    std::string m_DefaultBranch = "default";
    uint16_t m_used_branch = 1;
    std::chrono::steady_clock::time_point start_time;
    mutable std::mutex m;

    void HandleBoolReadingByOffset(const std::map<size_t, uint8_t>& reg, ModbusItemType& items,
        IModbusValueObserver::Table table);
    void HandleRegisterReadingByOffset(const std::map<size_t, uint16_t>& reg, ModbusItemType& items,
        IModbusValueObserver::Table table);
    std::optional<GroupedModbusRegisterReadResult> ReadRegisterGroups(const ModbusItemType& items,
        RegisterTable table);
    std::optional<GroupedModbusBitReadResult> ReadBitGroups(const ModbusItemType& items, bool input_status);

    /* !\brief Reads one table and hands the values on.
       The four tables were polled by four blocks that differed only in which
       table, which read and which decode - the three frame counters were
       stepped identically in each. */
    void PollBitTable(ModbusItemType& items, bool input_status, IModbusValueObserver::Table table);
    void PollRegisterTable(ModbusItemType& items, RegisterTable source, IModbusValueObserver::Table table);

    /* !\brief What one grouped read did to the frame counts. */
    template <typename ReadResult>
    void CountPolledFrames(const ReadResult& result)
    {
        tx_frame_cnt.fetch_add(result.read_count, std::memory_order_relaxed);
        rx_frame_cnt.fetch_add(result.read_count - result.failed_read_count, std::memory_order_relaxed);
        err_frame_cnt.fetch_add(result.failed_read_count, std::memory_order_relaxed);
    }
    bool WaitIfPaused(std::stop_token token);

    // !\brief Close the port if it is open, swallowing the failure to.
    void CloseConnectionIfOpen();

    // !\brief Open the port, with the delay before and the longer delay after a
    // failure that the worker has always waited.
    void OpenConnection();
    void StopWorker();
    void HandlePolling();
    void HandleWrites();

    // !\brief The wire address of an item within its table, or nothing when the
    // configured offset would run past the end of the address space.
    [[nodiscard]] std::optional<uint16_t> ResolveAddress(const ModbusItem& item, uint16_t table_offset) const;

    // !\brief Send one already-encoded holding-register value and count the result.
    void SendHoldingRegisters(const ModbusItem& item, const std::vector<uint16_t>& values);

    // One overload per alternative of ModbusWrite; std::visit picks between them,
    // so a new alternative fails to compile until it is handled here.
    void ApplyWrite(const ModbusCoilWrite& write);
    void ApplyWrite(const ModbusHoldingWrite& write);
    void ApplyWrite(const ModbusFloatWrite& write);
    void ApplyWrite(const ModbusDoubleWrite& write);
    void ModbusWorker(std::stop_token token);

    void NotifySaved(const std::filesystem::path& path, int64_t duration_ns);

    IModbusEntryLoader& m_ModbusEntryLoader;
    IModbusEventSink* m_EventSink = nullptr;
    std::atomic_bool is_enabled{ true };
    std::atomic_bool m_isReady{ false };
    std::atomic_uint16_t m_pollingRate{ 500 };
    std::string m_DefaultConfigName{ "Modbus.xml" };
    std::atomic_size_t tx_frame_cnt = 0;
    std::atomic_size_t rx_frame_cnt = 0;
    std::atomic_size_t err_frame_cnt = 0;
    uint8_t m_DefaultFavouriteLevel = 0;
    ModbusWriteQueue m_WriteQueue;
    ModbusFrameLog m_Log;
    std::unique_ptr<std::jthread> m_workerModbus;
    std::condition_variable_any cv;
    std::atomic_bool m_isMainThreadPaused = false;
    std::unique_ptr<ModbusMasterSerialPort> m_Serial;
    IModbusValueObserver* m_helper = nullptr;
    std::mutex m_helperMutex;
};
