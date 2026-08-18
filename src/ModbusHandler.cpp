#include "pch.hpp"
#include "ModbusRegisterValueCodec.hpp"

constexpr int NUM_EVENTLOG_ENTRIES = 8;
constexpr int EVENTLOG_ENTRY_SIZE = 9;
constexpr int EVENTLOG_BUFFER_SIZE = NUM_EVENTLOG_ENTRIES * EVENTLOG_ENTRY_SIZE;

constexpr int MAX_INLINE_TIMEOUT_PACKETS = 15;

bool XmlModbusEntryLoader::Load(const std::filesystem::path& path, uint8_t& slave_id, ModbusItemType& coils, ModbusItemType& input_status,
    ModbusItemType& holding, ModbusItemType& input, NumModbusEntries& num_entries, uint32_t branch)
{
    bool ret = true;
    boost::property_tree::ptree pt;
    try
    {
        coils.clear();
        input_status.clear();
        holding.clear();
        input.clear();
        num_entries = {};
        size_t register_offset_coils = 0;
        size_t register_offset_input_status = 0;
        size_t register_offset_holding = 0;
        size_t register_offset_input = 0;

        read_xml(path.generic_string(), pt);
        for(const boost::property_tree::ptree::value_type& v : pt.get_child("Modbus"))
        {
            if(v.first == "NumCoils")           { num_entries.coils           = v.second.get_value<size_t>(0); continue; }
            if(v.first == "NumInputStatus")     { num_entries.inputStatus     = v.second.get_value<size_t>(0); continue; }
            if(v.first == "NumHoldingRegisters"){ num_entries.holdingRegisters = v.second.get_value<size_t>(0); continue; }
            if(v.first == "NumInput" || v.first == "NumInputRegisters")
                                                  { num_entries.inputRegisters  = v.second.get_value<size_t>(0); continue; }
            if(v.first == "CoilsOffset")        { num_entries.coilsOffset     = v.second.get_value<uint16_t>(0); continue; }
            if(v.first == "InputStatusOffset")  { num_entries.inputStatusOffset = v.second.get_value<uint16_t>(0); continue; }
            if(v.first == "InputOffset")        { num_entries.inputOffset     = v.second.get_value<uint16_t>(0); continue; }
            if(v.first == "HoldingOffset")      { num_entries.holdingOffset   = v.second.get_value<uint16_t>(0); continue; }
            if(v.first == "SlaveAddress")       { slave_id = v.second.get_value<size_t>(0); continue; }

            ModbusItemType* item = nullptr;
            size_t* offset = nullptr;
            ModbusBitfieldType register_value_type = ModbusBitfieldType::MBT_BOOL;

            for(const boost::property_tree::ptree::value_type& m : v.second)
            {
                if(m.first == "Coil")
                {
                    item = &coils;  offset = &register_offset_coils;
                    register_value_type = ModbusBitfieldType::MBT_BOOL;
                }
                else if(m.first == "InputStatus")
                {
                    item = &input_status;  offset = &register_offset_input_status;
                    register_value_type = ModbusBitfieldType::MBT_BOOL;
                }
                else if(m.first == "Input")
                {
                    item = &input;  offset = &register_offset_input;
                    register_value_type = ModbusBitfieldType::MBT_UI16;
                }
                else if(m.first == "Holding")
                {
                    item = &holding;  offset = &register_offset_holding;
                    register_value_type = ModbusBitfieldType::MBT_UI16;
                }
                else
                {
                    LOG(LogLevel::Warning, "Invalid modbus register child in Modbus.xml: {}", m.first);
                    continue;
                }

                std::string name = m.second.get_child("Name").get_value<std::string>();
                boost::optional<uint8_t> fav_child_val = m.second.get_optional<uint8_t>("FavLevel");
                uint8_t fav_level = fav_child_val ? *fav_child_val : 0;

                uint64_t last_val = m.second.get<uint64_t>("LastVal", 0);
                const size_t configured_offset = m.second.get<size_t>("Offset", *offset);

                boost::optional<std::string> data_type = m.second.get_optional<std::string>("DataType");
                if (data_type)
                    register_value_type = GetTypeFromString(*data_type);

                ModbusValueFormat val_format = ModbusValueFormat::MVF_DEC;
                boost::optional<std::string> val_format_str = m.second.get_optional<std::string>("Format");
                if (val_format_str)
                {
                    if      (*val_format_str == "hex") val_format = ModbusValueFormat::MVF_HEX;
                    else if (*val_format_str == "bin") val_format = ModbusValueFormat::MVF_BIN;
                }

                boost::optional<int64_t> min_val_child = m.second.get_optional<int64_t>("Min");
                boost::optional<int64_t> max_val_child = m.second.get_optional<int64_t>("Max");

                std::string description;
                boost::optional<std::string> description_child = m.second.get_optional<std::string>("Desc");
                if (description_child)
                {
                    description = *description_child;
                    boost::algorithm::replace_all(description, "\\n", "\n");
                }

                boost::optional<std::string> color;
                boost::optional<std::string> bg_color;
                boost::optional<bool> is_bold;
                boost::optional<float> is_scale;
                boost::optional<std::string> is_font_face;
                boost::optional<std::string> branch_str;
                utils::xml::ReadChildIfexists<std::string>(m, "Color", color);
                utils::xml::ReadChildIfexists<std::string>(m, "BackgroundColor", bg_color);
                utils::xml::ReadChildIfexists<float>(m, "Scale", is_scale);
                utils::xml::ReadChildIfexists<bool>(m, "Bold", is_bold);
                utils::xml::ReadChildIfexists<std::string>(m, "FontFace", is_font_face);
                utils::xml::ReadChildIfexists<std::string>(m, "Branch", branch_str);

                std::optional<uint32_t> color_;
                std::optional<uint32_t> bg_color_;
                std::optional<bool> is_bold_;
                std::optional<float> is_scale_;
                std::optional<std::string> is_font_face_;

                if(color)       color_       = utils::ColorStringToInt(*color);
                if(bg_color)    bg_color_    = utils::ColorStringToInt(*bg_color);
                if(is_bold && *is_bold) is_bold_ = true;
                if(is_scale)    is_scale_    = *is_scale;
                if(is_font_face) is_font_face_ = *is_font_face;

                ModbusMapping mapping;
                for (const boost::property_tree::ptree::value_type& x : m.second)
                {
                    if (x.first != "Mapping") continue;

                    uint8_t map_offset = x.second.get<uint8_t>("<xmlattr>.offset");
                    uint8_t len        = x.second.get<uint8_t>("<xmlattr>.len");
                    std::string type   = x.second.get<std::string>("<xmlattr>.type");
                    std::string map_name = x.second.get_value<std::string>();

                    ModbusBitfieldType bitfield_type = GetTypeFromString(type);
                    if (bitfield_type == MBT_INVALID)
                    {
                        LOG(LogLevel::Warning, "Invalid type used for frame mapping. Type: {}", type);
                        continue;
                    }

                    uint32_t color_val    = wxBLACK->GetRGB();
                    uint32_t bg_color_val = DEFAULT_TXTCTRL_BACKGROUND;
                    bool is_bold_val      = false;
                    float scale_val       = 1.0f;

                    boost::optional<std::string> color_child    = x.second.get_optional<std::string>("<xmlattr>.color");
                    boost::optional<std::string> bg_color_child = x.second.get_optional<std::string>("<xmlattr>.bg_color");
                    boost::optional<bool>  is_bold_child        = x.second.get_optional<bool>("<xmlattr>.bold");
                    boost::optional<float> scale_child          = x.second.get_optional<float>("<xmlattr>.scale");

                    if (color_child)    color_val    = utils::ColorStringToInt(*color_child);
                    if (bg_color_child) bg_color_val = utils::ColorStringToInt(*bg_color_child);
                    if (is_bold_child)  is_bold_val  = *is_bold_child;
                    if (scale_child)    scale_val    = *scale_child;

                    std::string map_desc;
                    boost::optional<std::string> desc_child = x.second.get_optional<std::string>("<xmlattr>.desc");
                    if (desc_child)
                    {
                        map_desc = *desc_child;
                        boost::algorithm::replace_all(map_desc, "\\n", "\n");
                    }

                    auto ptr_map = std::make_unique<ModbusMap>(std::move(map_name), bitfield_type, len,
                        std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(),
                        std::move(map_desc), color_val, bg_color_val, is_bold_val, scale_val);

                    mapping.try_emplace(map_offset, std::move(ptr_map));
                }

                auto ptr = std::make_unique<ModbusItem>(name, fav_level, configured_offset, register_value_type, val_format, description, mapping, 0, 0, last_val,
                    color_, bg_color_, is_bold_, is_scale_, is_font_face_);

                if (branch_str)
                {
                    std::vector<std::string> branch_list;
                    boost::split(branch_list, *branch_str, [](char c) { return c == ','; }, boost::algorithm::token_compress_on);
                    for (auto& b : branch_list)
                        ptr->branches |= ModbusEntryHandler::getBranchIDByName(b);
                }
                else
                {
                    ptr->branches = branch ? branch : 0xFFFFFFFFu;
                }

                *offset = std::max(*offset, configured_offset + ptr->GetSize());
                item->push_back(std::move(ptr));
            }
        }

        if (num_entries.coils           == 0xFFFF) num_entries.coils           = register_offset_coils;
        if (num_entries.inputStatus     == 0xFFFF) num_entries.inputStatus     = register_offset_input_status;
        if (num_entries.holdingRegisters == 0xFFFF) num_entries.holdingRegisters = register_offset_holding;
        if (num_entries.inputRegisters  == 0xFFFF) num_entries.inputRegisters  = register_offset_input;
    }
    catch(const boost::property_tree::xml_parser_error& e)
    {
        LOG(LogLevel::Error, "Exception thrown: {}, {}", e.filename(), e.what());
        ret = false;
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Exception thrown: {}", e.what());
        ret = false;
    }
    return ret;
}

bool XmlModbusEntryLoader::Save(const std::filesystem::path& path, uint8_t& slave_id, ModbusItemType& coils, ModbusItemType& input_status,
    ModbusItemType& holding, ModbusItemType& input, NumModbusEntries& num_entries) const
{
    bool ret = true;
    boost::property_tree::ptree pt;
    auto& root_node = pt.add_child("Modbus", boost::property_tree::ptree{});
    root_node.add("SlaveAddress", slave_id);
    root_node.add("NumCoils", num_entries.coils);
    root_node.add("NumInputStatus", num_entries.inputStatus);
    root_node.add("NumHoldingRegisters", num_entries.holdingRegisters);
    root_node.add("NumInputRegisters", num_entries.inputRegisters);
    root_node.add("CoilsOffset", num_entries.coilsOffset);
    root_node.add("InputStatusOffset", num_entries.inputStatusOffset);
    root_node.add("HoldingOffset", num_entries.holdingOffset);
    root_node.add("InputOffset", num_entries.inputOffset);

    static const std::array<std::string, 4> child_names    = { "Coils", "InputStatuses", "HoldingRegisters", "InputRegisters" };
    static const std::array<std::string, 4> subchild_names = { "Coil", "InputStatus", "Holding", "Input" };
    static const std::array<std::string, 3> format_strs    = { "dec", "hex", "bin" };

    ModbusItemType* items[] = { &coils, &input_status, &holding, &input };
    for(int id = 0; id < 4; id++)
    {
        auto& register_node = root_node.add_child(child_names[id], boost::property_tree::ptree{});
        for(auto& m : *items[id])
        {
            auto& sub_node = register_node.add_child(subchild_names[id], boost::property_tree::ptree{});
            sub_node.add("Name", m->m_Name);
            sub_node.add("Offset", m->m_Offset);
            sub_node.add("FavLevel", m->m_FavLevel);
            sub_node.add("DataType", GetStringFromType(m->m_Type));
            sub_node.add("Format", format_strs[static_cast<size_t>(m->m_Format)]);
            sub_node.add("Min", m->m_Min);
            sub_node.add("Max", m->m_Max);
            if (!m->m_Desc.empty())
            {
                std::string desc_str = m->m_Desc;
                boost::algorithm::replace_all(desc_str, "\n", "\\n");
                sub_node.add("Desc", desc_str);
            }
            sub_node.add("LastVal", m->m_Value);
            if(m->m_color)   sub_node.add("Color", utils::ColorIntToString(*m->m_color));
            if(m->m_bg_color) sub_node.add("BackgroundColor", utils::ColorIntToString(*m->m_bg_color));
            if(m->m_is_bold) sub_node.add("Bold", "1");
            if(m->m_scale != 1.0f) sub_node.add("Scale", std::format("{:.1f}", m->m_scale));
            if(!m->m_font_face.empty()) sub_node.add("FontFace", m->m_font_face);
        }
    }

    try
    {
        boost::property_tree::write_xml(path.generic_string(), pt, std::locale(),
            boost::property_tree::xml_writer_make_settings<boost::property_tree::ptree::key_type>('\t', 1));
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Exception thrown: {}", e.what());
        ret = false;
    }
    return ret;
}

void ModbusEntryHandler::ClearValues()
{
    for (auto* items : { &m_coils, &m_inputStatus, &m_Holding, &m_Input })
        for (auto& i : *items) { i->m_Value = 0; i->m_fValue = 0.0f; i->m_dValue = 0.0; }
}

void ModbusEntryHandler::ExportValues(std::filesystem::path& path)
{
    std::string out;
    out += "Coils\n";
    for (auto& i : m_coils)
        out += std::format("{}: {}\n", i->m_Name, i->m_Value != 0);
    out += "Input Status\n";
    for (auto& i : m_inputStatus)
        out += std::format("{}: {}\n", i->m_Name, i->m_Value != 0);
    out += "Holding Registers\n";
    for (auto& i : m_Holding)
        out += std::format("{}: {}\n", i->m_Name, i->m_Type == MBT_FLOAT ? i->m_fValue : i->m_Value);
    out += "Input Registers\n";
    for (auto& i : m_Input)
        out += std::format("{}: {}\n", i->m_Name, i->m_Type == MBT_FLOAT ? i->m_fValue : i->m_Value);

    std::ofstream file(path);
    if (file)
    {
        file << out;
        file.flush();
    }
    else
    {
        LOG(LogLevel::Error, "Failed to open file for saving modbus values: {}", path.generic_string());
    }
}

void ModbusEntryHandler::ImportValues(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file)
    {
        LOG(LogLevel::Error, "Failed to open file for loading modbus values: {}", path.generic_string());
        return;
    }

    std::string line;
    enum class Section { None, Coils, InputStatus, HoldingRegisters, InputRegisters };
    Section currentSection = Section::None;

    while (std::getline(file, line))
    {
        if      (line == "Coils")            { currentSection = Section::Coils;            continue; }
        else if (line == "Input Status")     { currentSection = Section::InputStatus;      continue; }
        else if (line == "Holding Registers"){ currentSection = Section::HoldingRegisters; continue; }
        else if (line == "Input Registers")  { currentSection = Section::InputRegisters;   continue; }

        std::istringstream iss(line);
        std::string name, valueStr;
        if (!std::getline(iss, name, ':') || !(iss >> valueStr)) continue;

        name = boost::algorithm::trim_copy(name);
        bool isFloat = valueStr.find('.') != std::string::npos;
        bool isBool  = valueStr.find("true") != std::string::npos || valueStr.find("false") != std::string::npos;
        uint64_t value  = (isFloat || isBool) ? 0 : std::stoull(valueStr);
        float    fValue = (isFloat && !isBool) ? std::stof(valueStr) : 0.0f;
        if (isBool) value = valueStr.find("true") != std::string::npos;

        auto applyToCoils = [&]()
        {
            int id = 0;
            for (auto& i : m_coils)
            {
                if (i->m_Name == name && i->m_Value != value) { i->m_Value = value; EditCoil(id, value); break; }
                id++;
            }
        };

        auto applyToHolding = [&]()
        {
            int id = 0;
            for (auto& i : m_Holding)
            {
                if (i->m_Name == name)
                {
                    if (i->m_Type == ModbusBitfieldType::MBT_FLOAT && i->m_fValue != fValue)
                        { i->m_fValue = fValue; EditHolding(id, fValue); }
                    else if (i->m_Value != value)
                        { i->m_Value = value; EditHolding(id, value); }
                    break;
                }
                id++;
            }
        };

        if      (currentSection == Section::Coils)            applyToCoils();
        else if (currentSection == Section::HoldingRegisters) applyToHolding();
    }

    {
        std::scoped_lock helper_lock(m_helperMutex);
        if(m_helper)
            m_helper->RefreshItems();
    }
}

ModbusBitfieldType XmlModbusEntryLoader::GetTypeFromString(const std::string_view& input)
{
    auto ret = std::find_if(m_ModbusBitfieldTypeMap.cbegin(), m_ModbusBitfieldTypeMap.cend(),
        [&input](const auto& item) { return item.second == input; });
    if (ret == m_ModbusBitfieldTypeMap.cend())
        return ModbusBitfieldType::MBT_INVALID;
    return ret->first;
}

const std::string_view XmlModbusEntryLoader::GetStringFromType(ModbusBitfieldType type)
{
    auto it = m_ModbusBitfieldTypeMap.find(type);
    if (it != m_ModbusBitfieldTypeMap.end())
        return it->second;
    return m_ModbusBitfieldTypeMap[MBT_INVALID];
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
    m_ModbusEntryLoader.Load(m_DefaultConfigName, m_slaveId, m_coils, m_inputStatus, m_Holding, m_Input, m_numEntries, m_used_branch);
    is_recording = auto_recording;
}

void ModbusEntryHandler::Start()
{
    if(m_workerModbus)
        return;
    if(is_enabled)
        m_Serial->Init();
    m_workerModbus = std::make_unique<std::jthread>(std::bind_front(&ModbusEntryHandler::ModbusWorker, this));
    utils::SetThreadName(*m_workerModbus, "ModbusWorker");
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
    const std::filesystem::path save_path("Modbus2.xml");
    m_ModbusEntryLoader.Save(save_path, m_slaveId, m_coils, m_inputStatus, m_Holding, m_Input, m_numEntries);
}

std::vector<std::string> ModbusEntryHandler::GetAvailableDevices() const
{
    return m_ModbusEntryLoader.GetAvailableDevices(m_DefaultConfigName);
}

std::string ModbusEntryHandler::GetSelectedDevice() const
{
    return m_ModbusEntryLoader.GetSelectedDevice();
}

bool ModbusEntryHandler::ChangeDevice(const std::string& device)
{
    const bool was_running = !m_isMainThreadPaused.load(std::memory_order_acquire);
    StopWorker();
    if(!m_ModbusEntryLoader.SelectDevice(device))
    {
        if(was_running)
            Start();
        return false;
    }

    ModbusItemType coils;
    ModbusItemType input_status;
    ModbusItemType holding;
    ModbusItemType input;
    NumModbusEntries entries;
    uint8_t slave_id = m_slaveId;
    const bool loaded = m_ModbusEntryLoader.Load(m_DefaultConfigName, slave_id, coils, input_status,
        holding, input, entries, m_used_branch);
    if(loaded)
    {
        std::scoped_lock lock(m);
        m_slaveId = slave_id;
        m_coils = std::move(coils);
        m_inputStatus = std::move(input_status);
        m_Holding = std::move(holding);
        m_Input = std::move(input);
        m_numEntries = entries;
        m_pendingCoilWrites.clear();
        m_pendingHoldingWrites.clear();
        m_pendingHoldingWritesFloat.clear();
        m_pendingHoldingWritesDouble.clear();
    }
    if(was_running)
        Start();
    return loaded;
}

void ModbusEntryHandler::SetModbusHelper(IModbusHelper* helper)
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

void ModbusEntryHandler::ToggleRecording(bool toggle, bool is_pause)
{
    std::scoped_lock lock{ m };
    is_recording = toggle;
    if(!is_pause && !toggle)
    {
        tx_frame_cnt = rx_frame_cnt = err_frame_cnt = 0;
        m_LogEntries.clear();
    }
}

void ModbusEntryHandler::ClearRecording()
{
    std::scoped_lock lock{ m };
    tx_frame_cnt = rx_frame_cnt = err_frame_cnt = 0;
    m_LogEntries.clear();
}

void ModbusEntryHandler::NotifySaved(const std::filesystem::path& path, int64_t duration_ns)
{
    if(m_EventSink)
        m_EventSink->OnModbusRecordingSaved(path, duration_ns);
}

bool ModbusEntryHandler::SaveRecordingToFile(std::filesystem::path& path)
{
    auto t1 = std::chrono::steady_clock::now();
    std::scoped_lock lock{ m };
    if(m_LogEntries.empty()) return false;

    std::ofstream out(path, std::ofstream::binary);
    if(!out.is_open())
    {
        LOG(LogLevel::Error, "Failed to open file for saving Modbus recording: {}", path.generic_string());
        return false;
    }

    out << "Time,Direction,FunctionCode,DataSize,Data\n";
    for(auto& i : m_LogEntries)
    {
        std::string hex;
        utils::ConvertHexBufferToString(i->data, hex);
        double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(i->last_execution - start_time).count() / 1000.0;
        out << std::format("{:.3f},{},{},{},{}\n", elapsed,
            i->direction == MODBUS_LOG_DIR_TX ? "TX" : "RX",
            static_cast<uint32_t>(i->fcode), i->data.size(), hex);
    }
    out.flush();

    int64_t dif = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t1).count();
    NotifySaved(path, dif);
    return true;
}

bool ModbusEntryHandler::SaveSpecialRecordingToFile(std::filesystem::path& path)
{
    auto t1 = std::chrono::steady_clock::now();
    std::scoped_lock lock{ m };
    if(m_EventLogEntries.empty()) return false;

    std::ofstream out(path, std::ofstream::binary);
    if(!out.is_open())
    {
        LOG(LogLevel::Error, "Failed to open file for saving Modbus recording: {}", path.generic_string());
        return false;
    }

    out << "Time,DataSize,Data\n";
    for(auto& i : m_EventLogEntries)
    {
        std::string hex;
        utils::ConvertHexBufferToString(i->data, hex);
        double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(i->last_execution - start_time).count() / 1000.0;
        out << std::format("{:.3f},{}\n", elapsed, hex);
    }
    out.flush();

    int64_t dif = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t1).count();
    NotifySaved(path, dif);
    return true;
}

// IModbusRecorder
bool ModbusEntryHandler::IsReady() const
{
    return wxGetApp().is_init_finished;
}

void ModbusEntryHandler::RecordFrame(uint8_t direction, uint8_t fc, ModbusErrorType error, const uint8_t* data, size_t len)
{
    auto now = std::chrono::steady_clock::now();
    std::scoped_lock lock{ m };
    m_LogEntries.emplace_back(std::make_unique<ModbusLogEntry>(direction, fc, error, const_cast<uint8_t*>(data), len, now));
}

void ModbusEntryHandler::CheckMaxEntries()
{
    std::scoped_lock lock{ m };
    if (m_LogEntries.size() >= max_recorded_entries)
        m_LogEntries.clear();
}

template <typename T> void ModbusEntryHandler::HandleBitReading(size_t id, bool is_holding, std::unique_ptr<ModbusMap>& m, size_t offset, ModbusBitfieldInfo& info)
{
    uint8_t* register_pointer = is_holding ? (uint8_t*)&m_Holding[id]->m_Value : (uint8_t*)&m_Input[id]->m_Value;
    uint64_t value = get_bitfield(register_pointer, 8, offset, m->m_Size);
    T extracted_data = static_cast<T>(value);
    info.push_back({std::format("{}         (offset: {}, size: {}, range: {} - {})", m->m_Name, offset, m->m_Size, m->m_MinVal, m->m_MaxVal), std::to_string(extracted_data), m.get() });
}

template <typename T> void ModbusEntryHandler::HandleBitWriting(size_t id, uint8_t& pos, uint8_t offset, uint8_t size, uint8_t* byte_array, std::vector<std::string>& new_data)
{
    try
    {
        T raw_data = static_cast<T>(std::stoi(new_data[pos]));
        set_bitfield(raw_data, offset, size, byte_array, sizeof(byte_array));
    }
    catch (const std::exception& e)
    {
        LOG(LogLevel::Error, "Invalid input for pos {}. Exception: {}", pos, e.what());
    }
    pos++;
}

ModbusBitfieldInfo ModbusEntryHandler::GetMapForHolding(size_t id, bool is_holding)
{
    ModbusBitfieldInfo info;
    const auto& src = is_holding ? m_Holding : m_Input;
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
    if (id >= m_Holding.size()) return;

    uint8_t cnt = 0;
    uint8_t byte_array[8] = {};
    memcpy(byte_array, &m_Holding.at(id)->m_Value, 8);

    for (auto& [offset, m] : m_Holding.at(id)->m_Mapping)
    {
        DispatchModbusBitfieldType(m->m_Type, [&]<typename T>()
        {
            HandleBitWriting<T>(id, cnt, offset, m->m_Size, byte_array, new_data);
        });
    }

    uint64_t value = m_Holding.at(id)->m_Value = *(uint64_t*)byte_array;
    EditHolding(id, value);
}

void ModbusEntryHandler::HandleBoolReading(std::vector<uint8_t>& reg, ModbusItemType& items, size_t num_items)
{
    std::scoped_lock state_lock(m);
    bool done = false;
    for(int cnt = 0; !done && cnt < (int)reg.size(); cnt++)
    {
        for(uint8_t x = 0; x != 8; x++)
        {
            size_t coil_pos = (cnt * 8) + x;
            if(coil_pos >= num_items) { done = true; break; }
            if(coil_pos < items.size())
            {
                bool bit_val = reg[cnt] & (1 << x);
                if(items[coil_pos]->m_Value != (uint64_t)bit_val)
                {
                    items[coil_pos]->m_Value = bit_val;
                }
            }
        }
    }
    rx_frame_cnt++;
}

void ModbusEntryHandler::HandleBoolReadingByOffset(const std::map<size_t, uint8_t>& reg,
    ModbusItemType& items, IModbusHelper::Table table)
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
            if(value == reg.end() || item->m_Value == value->second)
                continue;
            item->m_Value = value->second;
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

typedef union
{
    float asFloat;
    uint16_t asShorts[2];
} FloatUnion;

void ModbusEntryHandler::HandleRegisterReading(std::vector<uint16_t>& reg, ModbusItemType& items, size_t num_items)
{
    std::scoped_lock state_lock(m);
    int pos = 0;
    for(auto i = reg.begin(); i != reg.end(); i++, pos++)
    {
        if(pos >= (int)num_items) break;
        if(pos >= (int)items.size()) break;

        if (!(items[pos]->branches & m_used_branch)) continue;

        const auto type = items[pos]->m_Type;
        if (type == ModbusBitfieldType::MBT_UI16 || type == ModbusBitfieldType::MBT_I16)
        {
            if(items[pos]->m_Value != *i)
                items[pos]->m_Value = *i;
        }
        else if (type == ModbusBitfieldType::MBT_UI32 || type == ModbusBitfieldType::MBT_I32)
        {
            if (i + 1 == reg.end()) { LOG(LogLevel::Warning, "End reached!"); break; }
            items[pos]->m_Value = (*(i + 1) << 16 | *i) & 0xFFFFFFFF;
            ++i;
        }
        else if (type == ModbusBitfieldType::MBT_FLOAT)
        {
            if (i + 1 == reg.end()) { LOG(LogLevel::Warning, "End reached!"); break; }
            FloatUnion un;
            un.asShorts[0] = *i;
            un.asShorts[1] = *(i + 1);
            items[pos]->m_fValue = un.asFloat;
            ++i;
        }
    }

    rx_frame_cnt++;
}

void ModbusEntryHandler::HandleRegisterReadingByOffset(const std::map<size_t, uint16_t>& reg,
    ModbusItemType& items, IModbusHelper::Table table)
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

            bool changed = false;
            switch(item->m_Type)
            {
                case MBT_UI8:
                case MBT_I8:
                case MBT_UI16:
                case MBT_I16:
                case MBT_BOOL:
                {
                    const uint64_t value = DecodeModbusRegister16(*w0, item->m_NetworkByteOrder);
                    changed = item->m_Value != value;
                    item->m_Value = value;
                    break;
                }
                case MBT_UI32:
                case MBT_I32:
                case MBT_FLOAT:
                {
                    const auto w1 = word(item->m_Offset + 1);
                    if(!w1)
                        break;
                    if(item->m_Type == MBT_FLOAT)
                    {
                        const float value = DecodeModbusRegisterFloat(*w0, *w1, item->m_NetworkByteOrder);
                        changed = item->m_fValue != value;
                        item->m_fValue = value;
                    }
                    else
                    {
                        const uint64_t value = DecodeModbusRegister32(*w0, *w1, item->m_NetworkByteOrder);
                        changed = item->m_Value != value;
                        item->m_Value = value;
                    }
                    break;
                }
                case MBT_UI64:
                case MBT_I64:
                case MBT_DOUBLE:
                {
                    const auto w1 = word(item->m_Offset + 1);
                    const auto w2 = word(item->m_Offset + 2);
                    const auto w3 = word(item->m_Offset + 3);
                    if(!w1 || !w2 || !w3)
                        break;
                    if(item->m_Type == MBT_DOUBLE)
                    {
                        const double value = DecodeModbusRegisterDouble(*w0, *w1, *w2, *w3,
                            item->m_NetworkByteOrder);
                        changed = item->m_dValue != value;
                        item->m_dValue = value;
                    }
                    else
                    {
                        const uint64_t value = DecodeModbusRegister64(*w0, *w1, *w2, *w3,
                            item->m_NetworkByteOrder);
                        changed = item->m_Value != value;
                        item->m_Value = value;
                    }
                    break;
                }
                case MBT_STRING:
                case MBT_INVALID:
                    break;
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
    const uint16_t base = table == RegisterTable::Holding ? m_numEntries.holdingOffset : m_numEntries.inputOffset;
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
    const uint16_t base = input_status ? m_numEntries.inputStatusOffset : m_numEntries.coilsOffset;
    return ReadGroupedModbusBits(items, m_used_branch,
        [&](uint16_t offset, uint16_t count) -> std::expected<std::vector<uint8_t>, ModbusError>
        {
            return input_status ? m_Serial->ReadInputStatus(GetSlaveId(), offset, count)
                : m_Serial->ReadCoilStatus(GetSlaveId(), offset, count);
        }, base);
}

bool ModbusEntryHandler::WaitIfPaused(std::stop_token token)
{
    std::unique_lock lk(m);
    cv.wait(lk, token, [this] { return !m_isMainThreadPaused; });
    return !token.stop_requested();
}

void ModbusEntryHandler::HandlePolling()
{
    if(!m_coils.empty())
    {
        const auto result = ReadBitGroups(m_coils, false);
        if(result)
        {
            tx_frame_cnt.fetch_add(result->read_count, std::memory_order_relaxed);
            rx_frame_cnt.fetch_add(result->read_count - result->failed_read_count, std::memory_order_relaxed);
            err_frame_cnt.fetch_add(result->failed_read_count, std::memory_order_relaxed);
            HandleBoolReadingByOffset(result->values, m_coils, IModbusHelper::Table::Coils);
        }
    }

    if(!m_inputStatus.empty())
    {
        const auto result = ReadBitGroups(m_inputStatus, true);
        if(result)
        {
            tx_frame_cnt.fetch_add(result->read_count, std::memory_order_relaxed);
            rx_frame_cnt.fetch_add(result->read_count - result->failed_read_count, std::memory_order_relaxed);
            err_frame_cnt.fetch_add(result->failed_read_count, std::memory_order_relaxed);
            HandleBoolReadingByOffset(result->values, m_inputStatus, IModbusHelper::Table::InputStatus);
        }
    }

    if(!m_Holding.empty())
    {
        const auto result = ReadRegisterGroups(m_Holding, RegisterTable::Holding);
        if(result)
        {
            tx_frame_cnt.fetch_add(result->read_count, std::memory_order_relaxed);
            rx_frame_cnt.fetch_add(result->read_count - result->failed_read_count, std::memory_order_relaxed);
            err_frame_cnt.fetch_add(result->failed_read_count, std::memory_order_relaxed);
            HandleRegisterReadingByOffset(result->values, m_Holding, IModbusHelper::Table::Holding);
        }
    }

    if(!m_Input.empty())
    {
        const auto result = ReadRegisterGroups(m_Input, RegisterTable::Input);
        if(result)
        {
            tx_frame_cnt.fetch_add(result->read_count, std::memory_order_relaxed);
            rx_frame_cnt.fetch_add(result->read_count - result->failed_read_count, std::memory_order_relaxed);
            err_frame_cnt.fetch_add(result->failed_read_count, std::memory_order_relaxed);
            HandleRegisterReadingByOffset(result->values, m_Input, IModbusHelper::Table::Input);
        }
    }
}

void ModbusEntryHandler::HandleWrites()
{
    decltype(m_pendingCoilWrites) coil_writes;
    decltype(m_pendingHoldingWrites) holding_writes;
    decltype(m_pendingHoldingWritesFloat) float_writes;
    decltype(m_pendingHoldingWritesDouble) double_writes;
    {
        std::scoped_lock lock(m);
        coil_writes.swap(m_pendingCoilWrites);
        holding_writes.swap(m_pendingHoldingWrites);
        float_writes.swap(m_pendingHoldingWritesFloat);
        double_writes.swap(m_pendingHoldingWritesDouble);
    }

    for(auto& c : coil_writes)
    {
        if(c.first >= m_coils.size())
            continue;
        const auto& item = *m_coils[c.first];
        const size_t offset = item.m_ManualAddress >= 0
            ? static_cast<size_t>(item.m_ManualAddress) : item.m_Offset;
        if(offset > std::numeric_limits<uint16_t>::max() - m_numEntries.coilsOffset)
        {
            err_frame_cnt++;
            continue;
        }
        auto reg = m_Serial->ForceSingleCoil(GetSlaveId(),
            static_cast<uint16_t>(m_numEntries.coilsOffset + offset), c.second);
        if(reg.has_value() && !reg->empty()) { tx_frame_cnt++; rx_frame_cnt++; }
        else err_frame_cnt++;
    }
    for(auto& c : holding_writes)
    {
        if(c.first >= m_Holding.size())
            continue;
        const auto& item = *m_Holding[c.first];
        const std::vector<uint16_t> vec = EncodeModbusRegisterValue(item, c.second);
        const size_t reg_offset = item.m_ManualAddress >= 0
            ? static_cast<size_t>(item.m_ManualAddress) : item.m_Offset;
        if(reg_offset > std::numeric_limits<uint16_t>::max() - m_numEntries.holdingOffset)
        {
            err_frame_cnt++;
            continue;
        }
        auto reg = m_Serial->WriteHoldingRegister(GetSlaveId(),
            static_cast<uint16_t>(m_numEntries.holdingOffset + reg_offset), vec.size(), vec);
        if(reg.has_value() && !reg->empty()) { tx_frame_cnt++; rx_frame_cnt++; }
        else err_frame_cnt++;
    }
    for(auto& c : float_writes)
    {
        if(c.first >= m_Holding.size())
            continue;
        const auto& item = *m_Holding[c.first];
        const auto vec = EncodeModbusRegisterFloatValue(item, c.second);
        const size_t offset = item.m_ManualAddress >= 0 ? static_cast<size_t>(item.m_ManualAddress) : item.m_Offset;
        if(offset > std::numeric_limits<uint16_t>::max() - m_numEntries.holdingOffset)
        {
            err_frame_cnt++;
            continue;
        }
        auto reg = m_Serial->WriteHoldingRegister(GetSlaveId(),
            static_cast<uint16_t>(m_numEntries.holdingOffset + offset), vec.size(), vec);
        if(reg.has_value() && !reg->empty()) { tx_frame_cnt++; rx_frame_cnt++; }
        else err_frame_cnt++;
    }
    for(auto& c : double_writes)
    {
        if(c.first >= m_Holding.size())
            continue;
        const auto& item = *m_Holding[c.first];
        const auto vec = EncodeModbusRegisterDoubleValue(item, c.second);
        const size_t offset = item.m_ManualAddress >= 0 ? static_cast<size_t>(item.m_ManualAddress) : item.m_Offset;
        if(offset > std::numeric_limits<uint16_t>::max() - m_numEntries.holdingOffset)
        {
            err_frame_cnt++;
            continue;
        }
        auto reg = m_Serial->WriteHoldingRegister(GetSlaveId(),
            static_cast<uint16_t>(m_numEntries.holdingOffset + offset), vec.size(), vec);
        if(reg.has_value() && !reg->empty()) { tx_frame_cnt++; rx_frame_cnt++; }
        else err_frame_cnt++;
    }
}

void ModbusEntryHandler::ModbusWorker(std::stop_token token)
{
    m_Serial->SetStopToken(token);
    while(!token.stop_requested())
    {
        while(!token.stop_requested() && !m_Serial->IsInstanceInited())
        {
            std::unique_lock lock(m);
            cv.wait_for(lock, token, 10ms, [] { return false; });
        }
        if(token.stop_requested())
            break;

        if (m_isMainThreadPaused)
        {
            m_isOpenInProgress = false;
            if (GetSerial().IsOpen() && !m_isCloseInProgress)
            {
                m_isCloseInProgress = true;
                try
                {
                    GetSerial().Close();
                    LOG(LogLevel::Notification, "Closing modbus connection");
                }
                catch (boost::system::system_error&) {}
                m_isCloseInProgress = false;
            }
        }

        if(!WaitIfPaused(token))
            break;

        if (is_enabled)
        {
            if (!GetSerial().IsOpen() && !m_isOpenInProgress)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                LOG(LogLevel::Notification, "Opening modbus connection");
                m_isOpenInProgress = true;
                GetSerial().Open();
                m_isOpenInProgress = false;
                if (!GetSerial().IsOpen())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }

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
                    std::unique_lock lock(m);
                    cv.wait_for(lock, token, std::chrono::milliseconds(GetPollingRate()), []{ return false; });
                }
            }
            else if(!token.stop_requested())
            {
                std::unique_lock lock(m);
                cv.wait_for(lock, token, 100ms, [] { return false; });
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
