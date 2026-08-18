#pragma once

#include "IModbusEntry.hpp"
#include "IModbusRecorder.hpp"
#include "IModbusEventSink.hpp"
#include "ModbusRegisterGrouping.hpp"
#include <atomic>
#include <map>

class XmlModbusEntryLoader : public IModbusEntryLoader
{
public:
    virtual ~XmlModbusEntryLoader() = default;

    bool Load(const std::filesystem::path& path, uint8_t& slave_id, ModbusItemType& coils, ModbusItemType& input_status,
        ModbusItemType& holding, ModbusItemType& input, NumModbusEntries& num_entries, uint32_t branch) override;
    bool Save(const std::filesystem::path& path, uint8_t& slave_id, ModbusItemType& coils, ModbusItemType& input_status,
        ModbusItemType& holding, ModbusItemType& input, NumModbusEntries& num_entries) const override;

    static ModbusBitfieldType GetTypeFromString(const std::string_view& input);
    static const std::string_view GetStringFromType(ModbusBitfieldType type);

private:
    static inline std::map<ModbusBitfieldType, std::string> m_ModbusBitfieldTypeMap
    {
        {MBT_BOOL, "bool"},
        {MBT_UI8, "uint8_t"},
        {MBT_I8, "int8_t"},
        {MBT_UI16, "uint16_t"},
        {MBT_I16, "int16_t"},
        {MBT_UI32, "uint32_t"},
        {MBT_I32, "int32_t"},
        {MBT_UI64, "uint64_t"},
        {MBT_I64, "int64_t"},
        {MBT_FLOAT, "float"},
        {MBT_DOUBLE, "double"},
        {MBT_STRING, "string"},
        {MBT_INVALID, "invalid"}
    };
};

class ModbusLogEntry
{
public:
    ModbusLogEntry(uint8_t dir, uint8_t fc, ModbusErrorType error, uint8_t* data_, size_t data_len, std::chrono::steady_clock::time_point timepoint) :
        direction(dir), fcode(fc), error_type(error), last_execution(timepoint)
    {
        if(data_ && data_len)
            data.insert(data.end(), data_, data_ + data_len);
    }

    std::vector<uint8_t> data;
    uint8_t direction;
    uint8_t fcode;
    ModbusErrorType error_type;
    std::chrono::steady_clock::time_point last_execution;
};

class EventLogEntry
{
public:
    EventLogEntry(std::vector<uint16_t>& data_, std::chrono::steady_clock::time_point timepoint) :
        last_execution(timepoint)
    {
        data = std::move(data_);
    }

    std::vector<uint16_t> data;
    std::chrono::steady_clock::time_point last_execution;
};

using ModbusBitfieldInfo = std::vector<std::tuple<std::string, std::string, ModbusMap*>>;

class ModbusMasterSerialPort;
class ModbusEntryHandler : public IModbusRecorder
{
public:
    ModbusEntryHandler(IModbusEntryLoader& loader, IModbusEventSink* event_sink = nullptr);
    ~ModbusEntryHandler();

    void Init();
    void Start();
    void Shutdown();
    void Save();
    void ClearValues();
    void ExportValues(std::filesystem::path& path);
    void ImportValues(const std::filesystem::path& path);
    void SetModbusHelper(IModbusHelper* helper);
    void SetEnabled(bool enable);
    bool IsEnabled() const;
    void SetPollingStatus(bool is_active);
    void ToggleAutoRecord(bool toggle) { auto_recording = toggle; }
    bool IsAutoRecord() const { return auto_recording; }
    bool IsAutoSend() const { return auto_send; }
    void ToggleAutoSend(bool toggle);
    void ToggleRecording(bool toggle, bool is_pause);
    void ClearRecording();
    bool SaveRecordingToFile(std::filesystem::path& path);
    bool SaveSpecialRecordingToFile(std::filesystem::path& path);
    void SetPollingRate(uint16_t rate_ms) { m_pollingRate.store(rate_ms, std::memory_order_relaxed); }
    uint16_t GetPollingRate() const { return m_pollingRate.load(std::memory_order_relaxed); }
    void SetSlaveId(uint8_t slave_id) { std::scoped_lock lock(m); m_slaveId = slave_id; }
    uint8_t GetSlaveId() const { std::scoped_lock lock(m); return m_slaveId; }
    void SetDefaultConfigName(const std::string& default_config) { m_DefaultConfigName = default_config; }
    const std::string& GetDefaultConfigName() const { return m_DefaultConfigName; }
    uint8_t GetFavouriteLevel() const { return m_DefaultFavouriteLevel; }
    void SetFavouriteLevel(uint8_t favourite_level) { m_DefaultFavouriteLevel = favourite_level; }
    void SetMaxRecordedEntries(size_t max_entries) { max_recorded_entries = max_entries; }
    size_t GetMaxRecordedEntries() const { return max_recorded_entries; }
    void SetDefaultBranch(const std::string& branch)
    {
        m_DefaultBranch = branch;
        m_used_branch = getBranchIDByName(branch);
    }
    const std::string& GetDefaultBranch() const { return m_DefaultBranch; }
    std::vector<std::string> GetAvailableDevices() const;
    std::string GetSelectedDevice() const;
    bool ChangeDevice(const std::string& device);
    void EditCoil(size_t id, bool value) { std::scoped_lock lock(m); m_pendingCoilWrites.push_back({ id, value }); }
    void EditHolding(size_t id, uint64_t value) { std::scoped_lock lock(m); m_pendingHoldingWrites.push_back({ id, value }); }
    void EditHoldingFloat(size_t id, float value) { std::scoped_lock lock(m); m_pendingHoldingWritesFloat.push_back({ id, value }); }
    void EditHoldingDouble(size_t id, double value) { std::scoped_lock lock(m); m_pendingHoldingWritesDouble.push_back({ id, value }); }

    template <typename T> void HandleBitReading(size_t id, bool is_holding, std::unique_ptr<ModbusMap>& m, size_t offset, ModbusBitfieldInfo& info);
    template <typename T> void HandleBitWriting(size_t id, uint8_t& pos, uint8_t offset, uint8_t size, uint8_t* byte_array, std::vector<std::string>& new_data);

    ModbusBitfieldInfo GetMapForHolding(size_t id, bool is_holding);
    void ApplyEditingOnHolding(size_t id, std::vector<std::string> new_data);

    ModbusMasterSerialPort& GetSerial() { return *m_Serial; }
    size_t GetTxFrameCount() const { return tx_frame_cnt.load(std::memory_order_relaxed); }
    size_t GetRxFrameCount() const { return rx_frame_cnt.load(std::memory_order_relaxed); }
    size_t GetErrFrameCount() const { return err_frame_cnt.load(std::memory_order_relaxed); }
    std::chrono::steady_clock::time_point GetStartTime() { return start_time; }

    // IModbusRecorder
    bool IsRecording() const override { return is_recording; }
    bool IsReady() const override;
    void RecordFrame(uint8_t direction, uint8_t fc, ModbusErrorType error, const uint8_t* data, size_t len) override;
    void CheckMaxEntries() override;

    static std::string getBranchNameByID(uint32_t id);
    static uint32_t getBranchIDByName(const std::string& name);
    static const std::map<uint32_t, std::string> gModbusBranches;

    // Public data accessed by GUI panels
    uint8_t m_slaveId = 1;
    ModbusItemType m_coils;
    ModbusItemType m_inputStatus;
    ModbusItemType m_Holding;
    ModbusItemType m_Input;
    NumModbusEntries m_numEntries;
    std::vector<std::unique_ptr<ModbusLogEntry>> m_LogEntries;
    std::vector<std::unique_ptr<EventLogEntry>> m_EventLogEntries;
    bool auto_send = false;
    bool auto_recording = false;
    bool is_recording = false;
    size_t max_recorded_entries = 30000;
    std::string m_DefaultBranch = "default";
    uint16_t m_used_branch = 1;
    std::chrono::steady_clock::time_point start_time;
    mutable std::mutex m;

private:
    enum class RegisterTable { Holding, Input };

    void HandleBoolReading(std::vector<uint8_t>& reg, ModbusItemType& items, size_t num_items);
    void HandleBoolReadingByOffset(const std::map<size_t, uint8_t>& reg, ModbusItemType& items,
        IModbusHelper::Table table);
    void HandleRegisterReading(std::vector<uint16_t>& reg, ModbusItemType& items, size_t num_items);
    void HandleRegisterReadingByOffset(const std::map<size_t, uint16_t>& reg, ModbusItemType& items,
        IModbusHelper::Table table);
    std::optional<GroupedModbusRegisterReadResult> ReadRegisterGroups(const ModbusItemType& items,
        RegisterTable table);
    std::optional<GroupedModbusBitReadResult> ReadBitGroups(const ModbusItemType& items, bool input_status);
    bool WaitIfPaused(std::stop_token token);
    void StopWorker();
    void HandlePolling();
    void HandleWrites();
    void ModbusWorker(std::stop_token token);

    void NotifySaved(const std::filesystem::path& path, int64_t duration_ns);

    IModbusEntryLoader& m_ModbusEntryLoader;
    IModbusEventSink* m_EventSink = nullptr;
    std::atomic_bool is_enabled{ true };
    std::atomic_uint16_t m_pollingRate{ 500 };
    std::string m_DefaultConfigName{ "Modbus.xml" };
    std::atomic_size_t tx_frame_cnt = 0;
    std::atomic_size_t rx_frame_cnt = 0;
    std::atomic_size_t err_frame_cnt = 0;
    uint8_t m_DefaultFavouriteLevel = 0;
    std::vector<std::pair<size_t, bool>> m_pendingCoilWrites;
    std::vector<std::pair<size_t, uint64_t>> m_pendingHoldingWrites;
    std::vector<std::pair<size_t, float>> m_pendingHoldingWritesFloat;
    std::vector<std::pair<size_t, double>> m_pendingHoldingWritesDouble;
    std::unique_ptr<std::jthread> m_workerModbus;
    std::condition_variable_any cv;
    std::atomic_bool m_isMainThreadPaused = false;
    bool m_isOpenInProgress = false;
    bool m_isCloseInProgress = false;
    std::unique_ptr<ModbusMasterSerialPort> m_Serial;
    IModbusHelper* m_helper = nullptr;
    std::mutex m_helperMutex;
};
