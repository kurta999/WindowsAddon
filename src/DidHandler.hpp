#pragma once

#include "utils/EnumNameTable.hpp"

#include <isotp/isotp.h>

#include "IDidLoader.hpp"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <atomic>
#include <cstddef>
#include <semaphore>
#include <string>
#include <vector>

#include "ICanObserver.hpp"
#include "CanEntryHandler.hpp"

constexpr const char* DID_CACHE_FILENAME = "DidCache.xml";

enum DidEntryType : uint8_t
{
    DET_UI8, DET_UI16, DET_UI32, DET_UI64, DET_STRING, DET_BYTEARRAY, DET_INVALID
};

namespace did_types
{
// !\brief Whether a DID holds a fixed-width unsigned integer.
//
// DidPanel spelled this as a four-way || chain, and ProcessReadDidResponse
// spelled it as four switch arms carrying four unrolled copies of one
// little-endian widening.
[[nodiscard]] constexpr bool IsInteger(DidEntryType type)
{
    return type == DET_UI8 || type == DET_UI16 || type == DET_UI32 || type == DET_UI64;
}

// !\brief How many payload bytes an integer DID occupies.
// !\return 1, 2, 4 or 8, or 0 for the types that are not integers.
[[nodiscard]] constexpr std::size_t IntegerWidth(DidEntryType type)
{
    switch(type)
    {
        case DET_UI8:  return 1;
        case DET_UI16: return 2;
        case DET_UI32: return 4;
        case DET_UI64: return 8;
        default:       return 0;
    }
}
}

using DidValueType = std::variant<uint8_t, uint16_t, uint32_t, uint64_t, std::string, std::vector<uint8_t>>;

class DidEntry
{
public:
    DidEntry(uint16_t id_, DidEntryType type_, const std::string& name_, const std::string& min_, const std::string& max_, size_t len_) :
        id(id_), type(type_), name(name_), min(min_), max(max_), len(len_)
    {

    }

    uint16_t id = 0;
    DidEntryType type;
    std::string name;
    DidValueType value;
    std::string value_str;
    std::string min;  /* It's just informative right now */
    std::string max;  /* It's just informative right now */
    size_t len = 0;
    uint16_t nrc = 0;

    boost::posix_time::ptime last_update = boost::posix_time::not_a_date_time;
};

class XmlDidLoader : public IDidLoader
{
public:
    virtual ~XmlDidLoader();

    bool Load(const std::filesystem::path& path, DidMap& m) override;
    bool Save(const std::filesystem::path& path, const DidMap& m) const override;

    static DidEntryType GetTypeFromString(const std::string_view& input);
    static const std::string_view GetStringFromType(DidEntryType type);

private:
    static constexpr utils::EnumNameTable m_DidEntryTypeMap{
        DET_INVALID,
        std::array<utils::EnumName<DidEntryType>, 7>{{
            { DET_INVALID,   "invalid" },
            { DET_UI8,       "uint8_t" },
            { DET_UI16,      "uint16_t" },
            { DET_UI32,      "uint32_t" },
            { DET_UI64,      "uint64_t" },
            { DET_STRING,    "string" },
            { DET_BYTEARRAY, "bytearray" },
        }}
    };
};

class XmlDidCacheLoader : public IDidLoader
{
public:
    virtual ~XmlDidCacheLoader();

    bool Load(const std::filesystem::path& path, DidMap& m) override;
    bool Save(const std::filesystem::path& path, const DidMap& m) const override;

private:

};

namespace did
{
// !\brief Fit a value a user typed to a DID's declared length.
//
// The rule lived twice, verbatim, in DidPanel's write path - once for
// strings, once for byte arrays. Short values are padded by repeating the
// final character; long values keep their first declared_length-1
// characters plus their original last one - not the first declared_length,
// which a reader (and this author) will guess. That is what the panel has
// always sent and what the tests pin.
//
// An empty value returns empty: the string branch used to call .back() on
// it, which was undefined behaviour reachable from an empty grid cell.
[[nodiscard]] inline std::string FitToDeclaredLength(std::string value, std::size_t declared_length)
{
    if(value.empty() || declared_length == 0)
        return value;

    while(value.length() < declared_length)
        value += value.back();
    if(value.length() > declared_length)
        value.erase(declared_length - 1, value.length() - declared_length);
    return value;
}
}

class DidHandler : public ICanObserver
{
public:
    DidHandler(IDidLoader& loader, IDidLoader& cache_loader, CanEntryHandler* can_handler);
    virtual ~DidHandler();

    // !\brief Initialize DID handler
    void Init();

    // !\brief Save DID cache
    bool SaveCache() const;

    // !\brief Add DID to read queue which is being read by this handler
    // !\param did [in] DID to add
    void AddDidToReadQueue(uint16_t did);

    // !\brief Write DID with specified data
    // !\param did [in] DID to write
    // !\param data_to_write [in] Data to write
    // !\param size [in] Size of data to write
    void WriteDid(uint16_t did, uint8_t* data_to_write, uint16_t size);

    // !\brief Notify that a DID has been updated
    void NotifyDidUpdate();

    // !\brief Abort DID reading
    void AbortDidUpdate();

    void OnFrameOnBus(uint32_t frame_id, uint8_t* data, uint16_t size) override;

    void OnIsoTpDataReceived(uint32_t frame_id, const uint8_t* data, uint16_t size) override;

    // !\brief Reads one byte of the current response snapshot.
    // !\param index [in] Byte offset into the response
    // !\return The byte, or 0 when the response is shorter than index + 1
    [[nodiscard]] uint8_t ResponseByte(std::size_t index) const noexcept;

    // !\brief Copies the response payload that follows the UDS header.
    // !\param header_size [in] Number of leading header bytes to skip
    // !\return The payload, or an empty string when the response is too short
    [[nodiscard]] std::string ResponsePayload(std::size_t header_size) const;

    // !\brief `width` payload bytes from `offset`, read little-endian.
    //
    // The four integer arms of ProcessReadDidResponse were this loop unrolled
    // at one, two, four and eight bytes, each with its own cast ladder.
    [[nodiscard]] std::uint64_t ResponseInteger(std::size_t offset, std::size_t width) const noexcept;
     
    // !\brief The DID list and the pending-update list, under one lock.
    //
    // All three were public. The panel took the mutex by hand at three places
    // and read the list without it at a fourth, where a grid edit reads and
    // writes an entry the UDS worker also writes. Taking the lock had to be
    // remembered; now it cannot be forgotten.
    struct Model
    {
        DidMap& did_list;
        std::vector<uint16_t>& updated_dids;
    };

    // !\brief Run `fn` over the shared state with the lock held.
    //
    // `fn` must not call back into a DidHandler method that takes the same
    // lock, and must not keep a pointer or iterator it is given: the worker is
    // free to touch these as soon as `fn` returns.
    template <typename F> decltype(auto) WithModel(F&& fn)
    {
        std::scoped_lock lock(m);
        Model model{ m_DidList, m_UpdatedDids };
        return std::forward<F>(fn)(model);
    }

private:
    // !\brief DID list
    DidMap m_DidList;

    // !\brief Mutex for entry handler
    std::mutex m;

    // !\brief DIDs whose values have been updated
    std::vector<uint16_t> m_UpdatedDids;

    // !\brief Send a UDS frame and wait for a response
    // !\param frame [in] Frame to send
    bool SendUdsFrameAndWaitForResponse(std::vector<uint8_t> frame);

    // !\brief Wait for a response from the ECU
    bool WaitForResponse();

    // !\brief Process a DID response
    // !\param entry [in] DID entry to process
    void ProcessReadDidResponse(std::unique_ptr<DidEntry>& entry);

    // !\brief Record a DID as changed, under the model lock.
    //
    // The worker publishes into m_UpdatedDids while the view reads it, so
    // every append takes m - the response paths used to do this in some
    // branches and not others.
    void MarkDidUpdated(uint16_t did);

    // !\brief Store a negative response code, under the model lock.
    void SetDidNrc(std::unique_ptr<DidEntry>& entry, uint8_t nrc);

    // !\brief Store a negative response code and stamp it, under the model lock.
    void SetDidNrcAndTimestamp(std::unique_ptr<DidEntry>& entry, uint8_t nrc);

    // !\brief Process a NRC response 
    void ProcessRejectedNrc(std::unique_ptr<DidEntry>& entry);

    // !\brief Take the DID at the head of the read queue off it.
    //
    // The queue drained only on a positive response and on NRC 0x10, so a DID
    // answering with any other NRC stayed at the head of it and was asked for
    // again on every pass.
    void DropCurrentDidRead();

    // !\brief Handle DID reading
    void HandleDidReading(std::stop_token& token);

    // !\brief Handle DID writing
    void HandleDidWriting(std::stop_token& token);

    // !\brief Worker thread
    void WorkerThread(std::stop_token token);

    // !\brief Worker thread object
    std::unique_ptr<std::jthread> m_worker;

    // !\brief Condition variable waiting for CAN message
    std::condition_variable_any m_CanMessageCv;

    // !\brief Mutex for CAN message CV
    std::mutex m_CanMessageMutex;

    // !\brief Semaphore for worker thread
    std::binary_semaphore m_Semaphore;

    // !\brief DIDs whose values are being read
    std::deque<uint16_t> m_PendingDidReads;

    // !\brief DIDs whose values are being written
    std::map<uint16_t, std::string> m_PendingDidWrites;

    // !\brief Callback to call when a DID has been updated
    std::atomic<bool> m_IsAborted{ false };

    // !\brief Buffer for IsoTp frames. Written by the CAN receive thread and
    // read only under m_CanMessageMutex - never touched by the parsing code,
    // which works from the m_Response snapshot instead.
    uint8_t m_IsoTpBuffer[4096] = {};

    // !\brief Length of IsoTp buffer. Non-zero means "a response is waiting".
    std::atomic<uint16_t> m_IsoTpBufLen{};

    // !\brief Set when an in-flight DID operation is cancelled. Kept separate
    // from the length so the length never carries an out-of-band value.
    std::atomic<bool> m_ResponseAborted{ false };

    // !\brief Snapshot of the last response, owned by the DID worker thread.
    std::vector<uint8_t> m_Response;

    // !\brief DID Loader
    IDidLoader& m_loader;

    // !\brief DID Cache Loader
    IDidLoader& m_cache_loader;

    // !\brief Local instance for CAN entry handler
    CanEntryHandler* m_can_handler = nullptr;
};