#pragma once

#include <filesystem>

#include "CanModels.hpp"
#include "ICanEntry.hpp"
#include "ICanSubscriber.hpp"

extern "C"
{
#include <isotp/isotp.h>
}

class CanEntryHandler : public ICanSubscriber
{
public:
    CanEntryHandler(ICanEntryLoader& loader, ICanRxEntryLoader& rx_loader, ICanMappingLoader& mapping_loader);
    ~CanEntryHandler();

    void Init();
    void LoadFiles();
    void WorkerThread(std::stop_token token);

    void OnFrameSent(uint32_t frame_id, uint8_t data_len, uint8_t* data);
    void OnFrameReceived(uint32_t frame_id, uint8_t data_len, uint8_t* data);

    void ToggleAutoSend(bool toggle);
    bool IsAutoSend() const { return m_AutoSend; }

    void ToggleAutoRecord(bool toggle) { m_AutoRecording = toggle; }
    bool IsAutoRecord() const { return m_AutoRecording; }

    void ToggleRecording(bool toggle, bool is_pause);
    void ClearRecording();

    void SendDataFrame(uint32_t frame_id, uint8_t* data, uint16_t size);
    void SendIsoTpFrame(uint32_t frame_id, uint8_t* data, uint16_t size);

    bool LoadTxList(std::filesystem::path& path);
    bool SaveTxList(std::filesystem::path& path);
    bool LoadRxList(std::filesystem::path& path);
    bool SaveRxList(std::filesystem::path& path);
    bool LoadMapping(std::filesystem::path& path);
    bool SaveMapping(std::filesystem::path& path);
    bool SaveRecordingToFile(std::filesystem::path& path);

    uint8_t GetRecordingLogLevel() const { return m_RecordingLogLevel; }
    void    SetRecordingLogLevel(uint8_t log_level) { m_RecordingLogLevel = log_level; }

    uint8_t GetFavouriteLevel() const { return m_DefaultFavouriteLevel; }
    void    SetFavouriteLevel(uint8_t favourite_level) { m_DefaultFavouriteLevel = favourite_level; }

    uint32_t GetDefaultEcuId() const { return m_DefaultEcuId; }
    void     SetDefaultEcuId(uint32_t ecu_id);

    void GenerateLogForFrame(uint32_t frame_id, bool is_rx, std::vector<std::string>& log);

    void     SetIsoTpResponseFrame(uint32_t frame_id) { m_IsoTpResponseId = frame_id; }
    uint32_t GetIsoTpResponseFrameId() const { return m_IsoTpResponseId; }

    uint64_t GetTxFrameCount() const { return m_TxFrameCount; }
    uint64_t GetRxFrameCount() const { return m_RxFrameCount; }

    CanBitfieldInfo GetMapForFrameId(uint32_t frame_id, bool is_rx);
    void ApplyEditingOnFrameId(uint32_t frame_id, std::vector<std::string> new_data);

    uint32_t FindFrameIdOnMapByName(const std::string& name);

    CanMapping&         GetMapping()         { return m_Mapping; }
    CanFrameMetadataMap& GetFrameMetadata()  { return m_FrameMetadata; }

    std::vector<std::string>& GetUdsRawBuffer() { return m_UdsFrames; }
    uint32_t GetElapsedTimeSinceLastUdsFrame() const;
    std::chrono::steady_clock::time_point GetStartTime() const { return m_StartTime; }

    std::optional<std::reference_wrapper<CanTxEntry>> FindTxCanEntryByFrame(uint32_t frame_id);
    void AssignNewBufferToTxEntry(uint32_t frame_id, uint8_t* buffer, size_t size);

    /* --- Publicly visible state (consumed by GUI layer) --- */
    std::vector<std::unique_ptr<CanTxEntry>>          entries;
    std::unordered_map<uint32_t, std::unique_ptr<CanRxData>> m_rxData;
    std::unordered_map<uint32_t, std::string>         rx_entry_comment;
    std::unordered_map<uint32_t, uint8_t>             m_RxLogLevels;
    std::vector<std::unique_ptr<CanLogEntry>>         m_LogEntries;

    std::filesystem::path default_tx_list = "TxList.xml";
    std::filesystem::path default_rx_list = "RxList.xml";
    std::filesystem::path default_mapping = "FrameMapping.xml";

    std::mutex m;

private:
    template <typename T> void HandleBitReading(uint32_t frame_id, bool is_rx, std::unique_ptr<CanMap>& m, size_t offset, CanBitfieldInfo& info);
    template <typename T> void HandleBitWriting(uint32_t frame_id, uint8_t& pos, uint8_t offset, uint8_t size, uint8_t* byte_array, std::vector<std::string>& new_data);

    ICanEntryLoader&    m_CanEntryLoader;
    ICanRxEntryLoader&  m_CanRxEntryLoader;
    ICanMappingLoader&  m_CanMappingLoader;

    bool m_AutoSend      = false;
    bool m_AutoRecording = false;
    bool m_IsRecording   = false;

    uint64_t m_TxFrameCount = 0;
    uint64_t m_RxFrameCount = 0;

    std::unique_ptr<std::jthread>    m_Worker;
    std::condition_variable_any      m_Cv;
    std::chrono::steady_clock::time_point m_StartTime;

    CanMapping          m_Mapping;
    CanFrameMetadataMap m_FrameMetadata;

    uint8_t  m_RecordingLogLevel   = 1;
    uint8_t  m_DefaultFavouriteLevel = 1;
    uint32_t m_DefaultEcuId        = 0x8AB;
    uint32_t m_IsoTpResponseId     = 0x7DA;

    /* ISO-TP state — internal only */
    IsoTpLink m_IsoTpLink;
    uint8_t   m_IsoTpSendBuf[MAX_ISOTP_FRAME_LEN];
    uint8_t   m_IsoTpRecvBuf[MAX_ISOTP_FRAME_LEN];
    uint8_t   m_UdsRecvData[4096] = {};
    std::vector<std::string>              m_UdsFrames;
    std::chrono::steady_clock::time_point m_LastUdsFrameReceived;
};
