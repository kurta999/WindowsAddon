#include "pch.hpp"

CanScriptHandler::CanScriptHandler(ICanResultPanel& result_panel, CanEntryHandler& handler) :
    m_Result(result_panel), m_Handler(handler)
{
    m_operands["SetFrameField"]    = [this](OperandParams& p) { SetFrameField(p); };
    m_operands["SetFrameFieldRaw"] = [this](OperandParams& p) { SetFrameFieldRaw(p); };
    m_operands["SendFrame"]        = [this](OperandParams& p) { SendFrame(p); };
    m_operands["WaitForFrame"]     = [this](OperandParams& p) { WaitForFrame(p); };
    m_operands["Sleep"]            = [this](OperandParams& p) { Sleep(p); };

    m_Handler.RegisterObserver(this);
}

CanScriptHandler::~CanScriptHandler()
{
    m_Handler.UnregisterObserver(this);
    AbortRunningScript();
}

void CanScriptHandler::ExecuteScript(std::string script)
{
    std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
    std::istringstream ss(script);
    std::string line;
    size_t line_cnt = 0;
    while(std::getline(ss, line, '\n') && !m_IsAborted)
    {
        line_cnt++;

        boost::algorithm::trim_right(line);
        std::vector<std::string> params;
        boost::split(params, line, boost::is_any_of(" "));

        if(params[0].starts_with("#"))
            continue;

        if(params[0].empty())
        {
            LOG(LogLevel::Verbose, "Skipping empty line at line: {}", line_cnt);
            continue;
        }

        auto it = m_operands.find(params[0]);
        if(it != m_operands.end())
            it->second(params);
        else
        {
            LOG(LogLevel::Warning, "Unknown script function name: {}", params[0]);
            m_Result.AddToLog(std::format("Unknown script function name: {}\n", params[0]));
        }
    }

    int64_t dif = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t1).count();
    m_Result.AddToLog(std::format("Script finished {}. Execution took {:.6f} ms\n",
        m_IsAborted ? "by abort" : "successfully", static_cast<double>(dif) / 1'000'000.0));
    m_IsAborted = false;
}

void CanScriptHandler::AbortRunningScript()
{
    m_IsAborted = true;
    m_WaitingFrame = std::numeric_limits<uint32_t>::max();
    m_WaitingFrameReceived = true;
    cv.notify_all();

    raw_frame_blocks.clear();
    m_FrameData.clear();

    if(m_FutureHandle.valid())
        if(m_FutureHandle.wait_for(std::chrono::milliseconds(10)) == std::future_status::ready)
            m_FutureHandle.get();
}

void CanScriptHandler::RunScript(std::string script)
{
    if(m_FutureHandle.valid())
        if(m_FutureHandle.wait_for(std::chrono::nanoseconds(1)) != std::future_status::ready)
            m_FutureHandle.get();

    m_FutureHandle = std::async(&CanScriptHandler::ExecuteScript, this, script);
}

bool CanScriptHandler::IsScriptRunning() const
{
    if(m_FutureHandle.valid())
        if(m_FutureHandle.wait_for(std::chrono::nanoseconds(1)) != std::future_status::ready)
            return true;
    return false;
}

void CanScriptHandler::OnFrameOnBus(uint32_t frame_id, uint8_t* data, uint16_t size)
{
    if(m_WaitingFrame == frame_id && m_WaitingFrame != std::numeric_limits<uint32_t>::max() && !m_WaitingFrameReceived)
    {
        std::unique_lock lk(cv_m);
        m_WaitingFrameReceived = true;
        m_WaitingFrameData.assign(data, data + size);
        cv.notify_all();
    }
}

void CanScriptHandler::OnIsoTpDataReceived(uint32_t frame_id, uint8_t* data, uint16_t size)
{
}

template <typename T> void CanScriptHandler::HandleBitWriting(uint32_t frame_id, uint8_t& pos, uint8_t offset, uint8_t size, uint8_t* byte_array, std::string& new_data)
{
    try
    {
        T raw_data = static_cast<T>(std::stoll(new_data, nullptr, 16));
        set_bitfield(raw_data, offset, size, byte_array, 8);
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Invalid input for pos {}. Exception: {}", pos, e.what());
    }
    pos++;
}

uint32_t CanScriptHandler::FindFrameIdByFieldName(const CanMapping& mapping, std::string_view field_name) const
{
    for(const auto& [id, fields] : mapping)
        for(const auto& [offset, field] : fields)
            if(field->m_Name == field_name)
                return id;
    return 0;
}

void CanScriptHandler::ApplyEditingOnFrameId(uint32_t frame_id, const std::string& field_name, std::string new_data)
{
    uint8_t cnt = 0;
    std::array<uint8_t, 8> byte_array{};

    if(auto it = m_FrameData.find(frame_id); it != m_FrameData.end())
        byte_array = it->second;

    CanMapping& mapping = m_Handler.GetMapping();
    if(!mapping.contains(frame_id))
        return;

    LOG(LogLevel::Normal, "MapSize: {}, NewDataSize: {}", mapping[frame_id].size(), new_data.size());
    for(auto& [offset, m] : mapping[frame_id])
    {
        if(m->m_Name != field_name)
            continue;

        DispatchBitfieldType(m->m_Type, [&]<typename T>()
        {
            HandleBitWriting<T>(frame_id, cnt, offset, m->m_Size, byte_array.data(), new_data);
        });
    }

    m_FrameData[frame_id] = byte_array;
    m_Handler.AssignNewBufferToTxEntry(frame_id, byte_array.data(), byte_array.size());
    m_Result.OnTxFrameUpdated(frame_id, byte_array);
}

CanScriptReturn CanScriptHandler::SetFrameField(OperandParams& params)
{
    if(!CheckParams(params.size(), 3))
        return;

    const std::string& field_name  = params[1];
    const std::string& frame_value = params[2];

    const uint32_t frame_id = FindFrameIdByFieldName(m_Handler.GetMapping(), field_name);
    if(!frame_id)
    {
        LOG(LogLevel::Error, "Field '{}' wasn't found in any frame", field_name);
        return;
    }

    ApplyEditingOnFrameId(frame_id, field_name, frame_value);

    uint8_t  size_in_bytes = 8;
    auto& meta = m_Handler.GetFrameMetadata();
    if(auto it = meta.find(frame_id); it != meta.end())
        size_in_bytes = std::min<uint8_t>(it->second.size, 8);

    const auto& array_to_send = m_FrameData[frame_id];

    std::string hex;
    utils::ConvertHexBufferToString(reinterpret_cast<const char*>(array_to_send.data()), size_in_bytes, hex);
    m_Result.AddToLog(std::format("SetFrameField {} (FrameID: {:X}, Size: {}, Data: {})\n", field_name, frame_id, size_in_bytes, hex));
}

CanScriptReturn CanScriptHandler::SetFrameFieldRaw(OperandParams& params)
{
    if(!CheckParams(params.size(), 3))
        return;

    const std::string& field_name = params[1];
    std::string frame_value = params[2].starts_with("0x") ? params[2].substr(2) : params[2];

    const uint32_t frame_id = FindFrameIdByFieldName(m_Handler.GetMapping(), field_name);
    if(!frame_id)
    {
        LOG(LogLevel::Error, "Field '{}' wasn't found in any frame", field_name);
        return;
    }

    if(frame_value.length() > 16)
    {
        LOG(LogLevel::Error, "Frame value too long: max 8 bytes, got {}", frame_value.length() / 2);
        return;
    }

    if(frame_value.length() & 1)
        frame_value += '0';

    std::array<uint8_t, 8> array_to_send{};
    utils::ConvertHexStringToBuffer(frame_value, std::span{ array_to_send });
    raw_frame_blocks[frame_id].assign(array_to_send.begin(), array_to_send.end());

    std::string hex;
    utils::ConvertHexBufferToString(reinterpret_cast<const char*>(array_to_send.data()), array_to_send.size(), hex);
    m_Result.AddToLog(std::format("SetFrameFieldRaw {} (FrameID: {:X}, Size: {}, Data: {})\n", field_name, frame_id, array_to_send.size(), hex));
}

CanScriptReturn CanScriptHandler::SendFrame(OperandParams& params)
{
    if(!CheckParams(params.size(), 2))
        return;

    const std::string& frame_name = params[1];
    const uint32_t frame_id = m_Handler.FindFrameIdOnMapByName(frame_name);
    if(!frame_id)
    {
        m_Result.AddToLog(std::format("SendFrame {} failed (FrameID not found)\n", frame_name));
        return;
    }

    uint8_t size_in_bytes = 8;
    auto& meta = m_Handler.GetFrameMetadata();
    if(auto it = meta.find(frame_id); it != meta.end())
        size_in_bytes = std::min<uint8_t>(it->second.size, 8);

    std::string hex;
    if(m_FrameData.contains(frame_id))
    {
        const auto& data_to_send = m_FrameData[frame_id];
        m_Handler.SendDataFrame(frame_id, std::span<const uint8_t>{data_to_send}.first(size_in_bytes));
        utils::ConvertHexBufferToString(reinterpret_cast<const char*>(data_to_send.data()), size_in_bytes, hex);
    }
    else if(raw_frame_blocks.contains(frame_id))
    {
        const auto& to_send = raw_frame_blocks[frame_id];
        m_Handler.SendDataFrame(frame_id, to_send);
        utils::ConvertHexBufferToString(reinterpret_cast<const char*>(to_send.data()), to_send.size(), hex);
    }
    else
    {
        m_Result.AddToLog("SetFrameField or SetFrameFieldRaw wasn't called before SendFrame\n");
        return;
    }
    m_Result.AddToLog(std::format("SendFrame {} (FrameID: {:X}, Size: {}, Data: {})\n", frame_name, frame_id, size_in_bytes, hex));
}

CanScriptReturn CanScriptHandler::WaitForFrame(OperandParams& params)
{
    if(!CheckParams(params.size(), 3))
        return;

    const std::string& field_name = params[1];
    uint32_t wait_ms = 0;
    try
    {
        wait_ms = static_cast<uint32_t>(std::stoi(params[2]));
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Invalid WaitForFrame parameter, stoi exception: {} (Input: {})", e.what(), params[2]);
        return;
    }

    const uint32_t frame_id = FindFrameIdByFieldName(m_Handler.GetMapping(), field_name);
    if(!frame_id)
    {
        LOG(LogLevel::Error, "Field '{}' wasn't found in any frame", field_name);
        return;
    }

    m_Result.AddToLog(std::format("WaitForFrame START {}, timeout: {} (FrameID: {:X})\n", field_name, wait_ms, frame_id));

    m_WaitingFrame = frame_id;
    m_WaitingFrameReceived = false;
    std::unique_lock lk(cv_m);
    bool ret = cv.wait_until(lk, std::chrono::system_clock::now() + std::chrono::milliseconds(wait_ms),
        [this]() { return m_WaitingFrameReceived || m_IsAborted.load(); });

    if(ret && m_WaitingFrameReceived && !m_IsAborted)
    {
        if(raw_frame_blocks[frame_id] == m_WaitingFrameData)
        {
            m_Result.AddToLog(std::format("WaitForFrame OK {} (FrameID: {:X})\n", field_name, frame_id));
        }
        else
        {
            std::string recv, expected;
            utils::ConvertHexBufferToString(reinterpret_cast<const char*>(m_WaitingFrameData.data()), m_WaitingFrameData.size(), recv);
            utils::ConvertHexBufferToString(reinterpret_cast<const char*>(raw_frame_blocks[frame_id].data()), raw_frame_blocks[frame_id].size(), expected);
            m_Result.AddToLog(std::format("WaitForFrame INVALID DATA Recv: {}, Expected: {} (FrameName: {}, FrameID: {:X})\n", recv, expected, field_name, frame_id));
        }
    }
    else if(ret)
    {
        m_Result.AddToLog(std::format("WaitForFrame FAILED with ABORT {} (FrameID: {:X})\n", field_name, frame_id));
    }
    else
    {
        m_Result.AddToLog(std::format("WaitForFrame FAILED with timeout {} (FrameID: {:X})\n", field_name, frame_id));
    }

    m_WaitingFrame = std::numeric_limits<uint32_t>::max();
    m_WaitingFrameReceived = false;
    raw_frame_blocks.erase(frame_id);
}

CanScriptReturn CanScriptHandler::Sleep(OperandParams& params)
{
    if(!CheckParams(params.size(), 2))
        return;

    uint32_t sleep_ms = 0;
    try
    {
        sleep_ms = static_cast<uint32_t>(std::stoi(params[1]));
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Invalid Sleep parameter, stoi exception: {} (Input: {})", e.what(), params[1]);
        return;
    }

    m_Result.AddToLog(std::format("Wait {} ms... ", sleep_ms));
    std::unique_lock lk(cv_m);
    bool aborted = cv.wait_until(lk, std::chrono::system_clock::now() + std::chrono::milliseconds(sleep_ms),
        [this]() { return m_IsAborted.load(); });
    m_Result.AddToLog(aborted ? "Aborted\n" : "OK\n");
}
