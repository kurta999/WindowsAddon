#pragma once

#include <filesystem>
#include <thread>

#include "CanModels.hpp"
#include "CanFrameLog.hpp"
#include "ICanEntry.hpp"
#include "ICanEventSink.hpp"
#include "ICanSubscriber.hpp"
#include "ICanTransport.hpp"
#include "IClock.hpp"

/* These four used to sit inside a single extern "C" block opened for
   <isotp/isotp.h> and never closed until after the last of them.

   <iosfwd> and <string_view> declare templates, which may not have C language
   linkage at all, so that block was only well formed as long as something had
   already included them - the precompiled header pulls both in transitively
   through <iostream> and <fstream>, leaving nothing but include guards here.
   ISettingsBinding declares one class of virtual members, which language
   linkage does not reach, so it was inside the block by accident rather than
   to any effect.

   The ISO-TP link now lives behind CanIsoTpEndpoint, which does its own
   extern "C" wrapping around the one header that needs it. */
#include "CanIsoTpEndpoint.hpp"
#include "interface/ISettingsBinding.hpp"

#include <iosfwd>
#include <string_view>

class CanEntryHandler : public ICanSubscriber, public ICanTransportListener
{
public:
    CanEntryHandler(ICanEntryLoader& loader, ICanRxEntryLoader& rx_loader, ICanMappingLoader& mapping_loader,
        ICanTransport& transport, IClock& clock, ICanEventSink* event_sink = nullptr);
    ~CanEntryHandler();

    void Init();
    void LoadFiles();
    void WorkerThread(std::stop_token token);

    void OnFrameSent(uint32_t frame_id, uint8_t data_len, uint8_t* data) override;
    void OnFrameReceived(uint32_t frame_id, uint8_t data_len, uint8_t* data) override;

    void ToggleAutoSend(bool toggle);
    bool IsAutoSend() const { return m_AutoSend; }

    void ToggleAutoRecord(bool toggle) { m_AutoRecording = toggle; }
    bool IsAutoRecord() const { return m_AutoRecording; }

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

    void SendDataFrame(uint32_t frame_id, std::span<const uint8_t> data);
    void SendIsoTpFrame(uint32_t frame_id, const uint8_t* data, uint16_t size);

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

    void     SetIsoTpResponseFrame(uint32_t frame_id) { m_IsoTp.SetResponseId(frame_id); }
    uint32_t GetIsoTpResponseFrameId() const { return m_IsoTp.ResponseId(); }

    uint64_t GetTxFrameCount() const { return m_TxFrameCount; }
    uint64_t GetRxFrameCount() const { return m_RxFrameCount; }

    CanBitfieldInfo GetMapForFrameId(uint32_t frame_id, bool is_rx);
    void ApplyEditingOnFrameId(uint32_t frame_id, std::vector<std::string> new_data);

    uint32_t FindFrameIdOnMapByName(const std::string& name);

    CanMapping&         GetMapping()         { return m_Mapping; }
    CanFrameMetadataMap& GetFrameMetadata()  { return m_FrameMetadata; }

    /* !\brief Everything received since the last clear, and clears it.
       Was a reference to the vector itself, which the raw UDS dialog held
       across a send and a wait while the receive thread appended to it. */
    [[nodiscard]] std::vector<std::string> TakeUdsRawFrames() { return m_IsoTp.TakeReceivedFrames(); }

    void ClearUdsRawFrames() { m_IsoTp.ClearReceivedFrames(); }
    uint32_t GetElapsedTimeSinceLastUdsFrame() const;
    std::chrono::steady_clock::time_point GetStartTime() const { return m_StartTime; }

    std::optional<std::reference_wrapper<CanTxEntry>> FindTxCanEntryByFrame(uint32_t frame_id);
    void AssignNewBufferToTxEntry(uint32_t frame_id, uint8_t* buffer, size_t size);

    // !\brief The mutable state the GUI shares with the CAN worker threads.
    //
    // These five containers were public, next to the mutex that guards them.
    // Roughly half the GUI accesses took the lock and half did not - the CAN
    // log panel walked m_LogEntries while the receive thread was appending to
    // it, and the RX grid iterated and erased from m_rxData unlocked - so the
    // guarded half only made the unguarded half harder to spot.
    struct Model
    {
        std::vector<std::unique_ptr<CanTxEntry>>& tx_entries;
        std::unordered_map<uint32_t, std::unique_ptr<CanRxData>>& rx_data;
        std::unordered_map<uint32_t, std::string>& rx_comments;
        std::unordered_map<uint32_t, uint8_t>& rx_log_levels;
        CanFrameLog& log;
    };

    /* The TX-list and RX-map edits the sender page performs. These were
       implemented inside the page's button handlers through WithModel - the
       free-frame-id search, a std::rotate of the service's own container, and
       in one case wxGrid calls, all under the model lock. The invariants live
       here now; WithModel stays for reads and single-field writes. */

    // !\brief Insert a fresh entry after `index` (clamped), under the first
    // frame id at or above kNewTxEntryDefaultId not already in the list.
    // !\return The id it chose.
    uint32_t InsertDefaultTxEntryAfter(size_t index);

    // !\brief Append a duplicate of the first entry with `frame_id`.
    bool DuplicateFirstTxEntry(uint32_t frame_id);

    // !\brief Remove every entry with `frame_id`.
    void RemoveTxEntries(uint32_t frame_id);

    // !\brief Wrap the front entry to the back - what Move Up does on row 0.
    void RotateTxFrontToBack();

    // !\brief Wrap the back entry to the front - what Move Down does on the
    // last row.
    void RotateTxBackToFront();

    // !\brief Swap the first entries carrying the two ids.
    // !\return The moved entry's new index - where `frame_id` landed,
    // which is the row the panel reselects.
    std::optional<size_t> SwapTxEntriesById(uint32_t frame_id, uint32_t other_id);

    // !\brief Forget every received frame.
    void ClearRxData();

    // !\brief Forget one received frame.
    void EraseRxData(uint32_t frame_id);

    static constexpr uint32_t kNewTxEntryDefaultId = 0x123;

    // !\brief Run `fn` over the shared state with the model lock held.
    //
    // `fn` must not call back into a CanEntryHandler method that takes the same
    // lock, and must not keep any pointer or iterator it is given: the worker
    // threads are free to reallocate these containers as soon as `fn` returns.
    template <typename F> decltype(auto) WithModel(F&& fn)
    {
        std::scoped_lock lock(m);
        Model model{ entries, m_rxData, rx_entry_comment, m_RxLogLevels, m_Log };
        return std::forward<F>(fn)(model);
    }

    std::filesystem::path default_tx_list = "TxList.xml";
    std::filesystem::path default_rx_list = "RxList.xml";
    std::filesystem::path default_mapping = "FrameMapping.xml";

private:
    std::vector<std::unique_ptr<CanTxEntry>>          entries;
    std::unordered_map<uint32_t, std::unique_ptr<CanRxData>> m_rxData;
    std::unordered_map<uint32_t, std::string>         rx_entry_comment;
    std::unordered_map<uint32_t, uint8_t>             m_RxLogLevels;
    CanFrameLog                                      m_Log;

    /* !\brief The comment a frame carries, or empty. Caller holds `m`: this
       reads the entry list and the receive-side comment map, which is the one
       part of a log line CanFrameLog cannot render for itself. */
    [[nodiscard]] std::string CommentForFrame(const CanLogEntry& entry);

    /* !\brief One recorded frame as a line. Caller holds `m`. */
    [[nodiscard]] std::string FormatLogLine(const CanLogEntry& entry, CanFrameLog::LineStyle style);

    std::mutex m;

    template <typename T> void HandleBitReading(uint32_t frame_id, bool is_rx, std::unique_ptr<CanMap>& m, size_t offset, CanBitfieldInfo& info);
    template <typename T> void HandleBitWriting(uint32_t frame_id, uint8_t& pos, uint8_t offset, uint8_t size, uint8_t* byte_array, std::vector<std::string>& new_data);

    ICanEntryLoader&    m_CanEntryLoader;
    ICanRxEntryLoader&  m_CanRxEntryLoader;
    ICanMappingLoader&  m_CanMappingLoader;
    ICanTransport&      m_CanTransport;
    IClock&             m_Clock;
    ICanEventSink*      m_EventSink = nullptr;

    bool m_AutoSend      = false;
    bool m_AutoRecording = false;

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

    CanIsoTpEndpoint m_IsoTp;
};

class CanSerialPort;

// !\brief The [CANSender] block, which configures both the transport and the
// frame handler, so it is a collaborator of the two rather than a member of one.
class CanSenderSettings : public ISettingsBinding
{
public:
    CanSenderSettings(CanSerialPort& port, CanEntryHandler& entries) :
        m_Port(port), m_Entries(entries) {}

    [[nodiscard]] std::string_view SettingsSection() const override { return "CANSender"; }
    void LoadSettings(SettingsReader& reader) override;
    void SaveSettings(std::ostream& out) const override;

private:
    CanSerialPort& m_Port;
    CanEntryHandler& m_Entries;
};
