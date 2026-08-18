#pragma once

#include "ICanResultPanel.hpp"
#include "ICanObserver.hpp"
#include "Logger.hpp"

#include <map>
#include <array>

using CanScriptReturn = void;
using OperandParams = std::vector<std::string>;

class CanEntryHandler;

class CanScriptHandler : public ICanObserver
{
public:
    CanScriptHandler(ICanResultPanel& result_panel, CanEntryHandler& handler);
    ~CanScriptHandler();

    void RunScript(std::string script);
    bool IsScriptRunning() const;
    void AbortRunningScript();

private:
    [[msvc::forceinline]] bool CheckParams(size_t actual, size_t required)
    {
        if(actual != required)
        {
            LOG(LogLevel::Error, "Invalid param count. {} should be instead of {}", required, actual);
            return false;
        }
        return true;
    }

    void ExecuteScript(std::string script);

    template <typename T> void HandleBitWriting(uint32_t frame_id, uint8_t& pos, uint8_t offset, uint8_t size, uint8_t* byte_array, std::string& new_data);
    void ApplyEditingOnFrameId(uint32_t frame_id, const std::string& field_name, std::string new_data);
    uint32_t FindFrameIdByFieldName(const CanMapping& mapping, std::string_view field_name) const;

    CanScriptReturn SetFrameField(OperandParams& params);
    CanScriptReturn SetFrameFieldRaw(OperandParams& params);
    CanScriptReturn SendFrame(OperandParams& params);
    CanScriptReturn WaitForFrame(OperandParams& params);
    CanScriptReturn Sleep(OperandParams& params);

    void OnFrameOnBus(uint32_t frame_id, uint8_t* data, uint16_t size) override;
    void OnIsoTpDataReceived(uint32_t frame_id, uint8_t* data, uint16_t size) override;

    std::map<std::string, std::function<void(OperandParams&)>> m_operands;
    std::map<uint32_t, std::array<uint8_t, 8>> m_FrameData;
    std::map<uint32_t, std::vector<uint8_t>>  raw_frame_blocks;

    ICanResultPanel& m_Result;
    CanEntryHandler& m_Handler;

    std::atomic<bool>      m_IsAborted{};
    std::condition_variable cv;
    std::mutex             cv_m;
    uint32_t               m_WaitingFrame = std::numeric_limits<uint32_t>::max();
    std::atomic<bool>      m_WaitingFrameReceived{false};
    std::vector<uint8_t>   m_WaitingFrameData;
    std::future<void>      m_FutureHandle;
};
