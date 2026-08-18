#include "pch.hpp"

#include <cerrno>

namespace
{
constexpr const char* SETTINGS_FILE_PATH = "./settings.ini";

std::filesystem::path AbsoluteSettingsPath()
{
    std::error_code error;
    auto path = std::filesystem::absolute(SETTINGS_FILE_PATH, error);
    return error ? std::filesystem::path(SETTINGS_FILE_PATH) : path.lexically_normal();
}

std::string LastStreamError()
{
    if(errno == 0)
        return "stream failure (no operating-system error code was provided)";
    return std::error_code(errno, std::generic_category()).message();
}

class SettingsReader
{
public:
    explicit SettingsReader(boost::property_tree::ptree& tree) : m_tree(tree) {}

    boost::property_tree::ptree& RequiredSection(std::string_view section)
    {
        m_section = section;
        m_key.clear();
        m_value.clear();

        auto child = m_tree.get_child_optional(m_section);
        if(!child)
            throw std::runtime_error(std::format("Required settings section [{}] is missing", m_section));
        return child.get();
    }

    std::string& Required(std::string_view section, std::string_view key)
    {
        auto& child = RequiredSection(section);
        m_key = key;
        auto value = child.find(m_key);
        if(value == child.not_found())
            throw std::runtime_error(std::format("Required setting [{}] {} is missing", m_section, m_key));

        m_value = value->second.data();
        return value->second.data();
    }

    void Track(std::string_view section, std::string_view key, std::string_view value)
    {
        m_section = section;
        m_key = key;
        m_value = value;
    }

    void TrackSection(std::string_view section)
    {
        m_section = section;
        m_key.clear();
        m_value.clear();
    }

    std::string Context() const
    {
        if(m_section.empty())
            return "settings initialization";
        if(m_key.empty())
            return std::format("section [{}]", m_section);

        constexpr size_t max_value_length = 160;
        std::string displayed_value = m_value;
        if(displayed_value.size() > max_value_length)
        {
            displayed_value.resize(max_value_length);
            displayed_value += "...";
        }
        return std::format("setting [{}] {} (value: '{}')", m_section, m_key, displayed_value);
    }

private:
    boost::property_tree::ptree& m_tree;
    std::string m_section;
    std::string m_key;
    std::string m_value;
};
}

void Settings::LoadFile()
{
    //std::locale::global(std::locale("Hungarian_Hungary.1250"));
    const auto settings_path = AbsoluteSettingsPath();
    std::error_code file_error;
    const bool settings_file_exists = std::filesystem::exists(settings_path, file_error);
    if(file_error)
    {
        LOG(LogLevel::Critical, "Failed to inspect settings file '{}': {}. No settings were loaded.",
            settings_path.generic_string(), file_error.message());
        used_pages.log = 1;
        return;
    }

    if(!settings_file_exists)
    {
        SaveFile(true);
        file_error.clear();
        if(!std::filesystem::exists(settings_path, file_error))
        {
            LOG(LogLevel::Critical,
                "Settings file '{}' was missing and automatic creation failed: {}. No settings were loaded.",
                settings_path.generic_string(), file_error ? file_error.message() : "file was not created");
            used_pages.log = 1;
            return;
        }
        LOG(LogLevel::Normal, "Settings file was missing; created defaults at '{}'", settings_path.generic_string());
    }

    boost::property_tree::ptree pt;
    try
    {
        boost::property_tree::ini_parser::read_ini(SETTINGS_FILE_PATH, pt);
    }
    catch(const boost::property_tree::ini_parser::ini_parser_error& e)
    {
        const std::string line = e.line() == 0 ? std::string{} : std::format(" at line {}", e.line());
        LOG(LogLevel::Critical,
            "Failed to parse settings file '{}'{}: {}. No settings were loaded.",
            settings_path.generic_string(), line, e.message());
        used_pages.log = 1;
        return;
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Critical, "Failed to read settings file '{}': {}. No settings were loaded.",
            settings_path.generic_string(), e.what());
        used_pages.log = 1;
        return;
    }

    SettingsReader reader(pt);
    const auto read_setting = [&reader](std::string_view section, std::string_view key) -> std::string&
    {
        return reader.Required(section, key);
    };

    try
    {
        {
            auto opt_section = pt.get_child_optional("Macro_Config");
            if(opt_section)
            {
                utils::ini::ReadValueIfexists(opt_section, "UsePerApplicationMacros", CustomMacro::Get()->use_per_app_macro);
                utils::ini::ReadValueIfexists(opt_section, "UseAdvancedKeyBinding", CustomMacro::Get()->advanced_key_binding);
                utils::ini::ReadValueIfexists(opt_section, "BringToForegroundKey", CustomMacro::Get()->bring_to_foreground_key);
                utils::ini::ReadValueIfexists(opt_section, "ScriptLauncherKey", wxGetApp().script_launcher->launcher_key);
            }
        }

        CustomMacro::Get()->macros.clear();
        std::unique_ptr<MacroAppProfile> p = std::make_unique<MacroAppProfile>();
        auto& global_child = reader.RequiredSection("Keys_Global");
        for(auto& key : global_child)
        {
            std::string& str = key.second.data();
            reader.Track("Keys_Global", key.first, str);
            CustomMacro::Get()->ParseMacroKeys(0, key.first, str, p, MacroFlags::None);
        }
        p->app_name = "Global";
        CustomMacro::Get()->macros.push_back(std::move(p));

        /* load per-application macros */
        size_t counter = 1;
        size_t cnt = 0;
        while((cnt = pt.count("Keys_Macro" + std::to_string(counter))) == 1)
        {
            std::unique_ptr<MacroAppProfile> p2 = std::make_unique<MacroAppProfile>();
            const std::string section = "Keys_Macro" + std::to_string(counter);
            auto& ch = reader.RequiredSection(section);
            for(auto& key : ch)
            {
                reader.Track(section, key.first, key.second.data());
                if(key.first.data() == std::string("AppName"))
                {
                    p2->app_name = key.second.data();
                    continue;
                }
                std::string& str = key.second.data();
                CustomMacro::Get()->ParseMacroKeys(counter, key.first, str, p2, MacroFlags::None);
            }
            counter++;
            CustomMacro::Get()->macros.push_back(std::move(p2));
        }

        SerialPort::Get()->SetEnabled(utils::stob(read_setting("COM_Backend", "Enable")));
        SerialPort::Get()->SetComPort(utils::stoi<uint16_t>(read_setting("COM_Backend", "COM")));
        SerialPort::Get()->SetForwardToTcp(utils::stob(read_setting("COM_Backend", "ForwardViaTcp")));
        SerialPort::Get()->SetRemoteTcpIp(read_setting("COM_Backend", "RemoteTcpIp"));
        SerialPort::Get()->SetRemoteTcpPort(utils::stoi<uint16_t>(read_setting("COM_Backend", "RemoteTcpPort")));

        SerialTcpBackend::Get()->is_enabled = utils::stob(read_setting("COM_TcpBackend", "Enable"));
        SerialTcpBackend::Get()->bind_ip = read_setting("COM_TcpBackend", "ListeningIp");
        SerialTcpBackend::Get()->tcp_port = utils::stoi<uint16_t>(read_setting("COM_TcpBackend", "ListeningPort"));

        Server::Get()->SetEnabled(utils::stob(read_setting("Sensors", "Enable")));
        Server::Get()->SetPort(utils::stoi<uint16_t>(read_setting("Sensors", "TCP_Port")));
        Sensors::Get()->SetGraphGenerationInterval(utils::stoi<uint16_t>(read_setting("Sensors", "GraphGenerationInterval")));
        Sensors::Get()->SetGraphResolution(utils::stoi<uint16_t>(read_setting("Sensors", "GraphResolution")));
        Sensors::Get()->SetIntegrationTime(utils::stoi<uint16_t>(read_setting("Sensors", "IntegrationTime")));
        Server::Get()->SetForwardIpAddress(read_setting("Sensors", "MeasurementForward"));
        Server::Get()->SetForwardIpAddress2(read_setting("Sensors", "MeasurementForward2"));

        std::unique_ptr<CanEntryHandler>& can_handler = wxGetApp().can_entry;
        CanSerialPort::Get()->SetEnabled(utils::stob(read_setting("CANSender", "Enable")));
        CanSerialPort::Get()->SetComPort(utils::stoi<uint16_t>(read_setting("CANSender", "COM")));
        CanSerialPort::Get()->SetDeviceType(static_cast<CanDeviceType>(utils::stoi<uint8_t>(read_setting("CANSender", "DeviceType"))));
        can_handler->ToggleAutoSend(utils::stob(read_setting("CANSender", "AutoSend")));
        can_handler->ToggleAutoRecord(utils::stob(read_setting("CANSender", "AutoRecord")));
        can_handler->SetRecordingLogLevel(utils::stoi<uint8_t>(read_setting("CANSender", "DefaultRecordingLogLevel")));
        can_handler->SetFavouriteLevel(utils::stoi<uint8_t>(read_setting("CANSender", "DefaultFavouriteLevel")));
        can_handler->SetDefaultEcuId(static_cast<uint32_t>(std::strtol(read_setting("CANSender", "DefaultEcuId").c_str(), nullptr, 16)));
        can_handler->default_tx_list = read_setting("CANSender", "DefaultTxList");
        can_handler->default_rx_list = read_setting("CANSender", "DefaultRxList");
        can_handler->default_mapping = read_setting("CANSender", "DefaultMapping");

        std::unique_ptr<ModbusEntryHandler>& modbus_handler = wxGetApp().modbus_handler;
        modbus_handler->SetEnabled(utils::stob(read_setting("ModbusMaster", "Enable")));
        modbus_handler->GetSerial().SetTcp(read_setting("ModbusMaster", "ConnectionType") == "TCP");
        modbus_handler->GetSerial().SetTcpIp(read_setting("ModbusMaster", "TcpIp"));
        modbus_handler->GetSerial().SetTcpPort(utils::stoi<uint16_t>(read_setting("ModbusMaster", "TcpPort")));
        modbus_handler->GetSerial().SetComPort(utils::stoi<uint16_t>(read_setting("ModbusMaster", "COM")));
        modbus_handler->SetPollingRate(utils::stoi<uint16_t>(read_setting("ModbusMaster", "PollingRate")));
        modbus_handler->GetSerial().m_ResponseTimeout = utils::stoi<uint16_t>(read_setting("ModbusMaster", "ResponseTimeout"));
        modbus_handler->SetDefaultConfigName(read_setting("ModbusMaster", "DefaultModbusConfig"));
        modbus_handler->ToggleAutoSend(utils::stob(read_setting("ModbusMaster", "AutoSend")));
        modbus_handler->ToggleAutoRecord(utils::stob(read_setting("ModbusMaster", "AutoRecord")));
        modbus_handler->SetMaxRecordedEntries(utils::stoi<size_t>(read_setting("ModbusMaster", "MaxRecordedEntries")));
        modbus_handler->SetDefaultBranch(read_setting("ModbusMaster", "Branch"));
        if(const auto modbus_settings = pt.get_child_optional("ModbusMaster"))
            if(const auto device = modbus_settings->get_optional<std::string>("Device"))
                wxGetApp().modbus_entry_loader.SelectDevice(*device);

        minimize_on_exit = utils::stob(read_setting("App", "MinimizeOnExit"));
        minimize_on_startup = utils::stob(read_setting("App", "MinimizeOnStartup"));
        Logger::Get()->SetLogLevelAsString(read_setting("App", "DefaultLogLevel"));
        Logger::Get()->SetLogFilters(read_setting("App", "LogFilters"));
        default_page = utils::stoi<decltype(default_page)>(read_setting("App", "DefaultPage"));
        remember_window_size = utils::stoi<decltype(remember_window_size)>(read_setting("App", "RememberWindowSize"));
        if(remember_window_size)
        {
            const auto& last_window_size = read_setting("App", "LastWindowSize");
            if(sscanf(last_window_size.c_str(), "%d,%d", &window_size.x, &window_size.y) != 2)
            {
                LOG(LogLevel::Error,
                    "Invalid setting [App] LastWindowSize in '{}': expected 'width,height', got '{}'",
                    settings_path.generic_string(), last_window_size);
            }

            if(window_size.x < WINDOW_SIZE_X)
                window_size.x = WINDOW_SIZE_X;
            if(window_size.y < WINDOW_SIZE_Y)
                window_size.y = WINDOW_SIZE_Y;
        }
        always_on_numlock = utils::stob(read_setting("App", "AlwaysOnNumLock"));
        const auto& shared_drive = read_setting("App", "SharedDriveLetter");
        if(shared_drive.empty())
            throw std::runtime_error("SharedDriveLetter must contain at least one character");
        shared_drive_letter = shared_drive[0];
        crypto_price_update = utils::stoi<uint16_t>(read_setting("App", "CryptoPriceUpdate"));

        CorsairHid::Get()->SetEnabled(utils::stob(read_setting("CorsairHid", "Enable")));
        CorsairHid::Get()->SetDebouncingInterval(utils::stoi<uint16_t>(read_setting("CorsairHid", "DebouncingInterval")));

        PrintScreenSaver::Get()->screenshot_key = read_setting("Screenshot", "ScreenshotKey");
        PrintScreenSaver::Get()->timestamp_format = read_setting("Screenshot", "ScreenshotDateFormat");
        PrintScreenSaver::Get()->screenshot_path = read_setting("Screenshot", "ScreenshotPath");
        PathSeparator::Get()->replace_key = read_setting("PathSeparator", "ReplacePathSeparatorKey");

        std::error_code ec;
        if(!std::filesystem::exists(PrintScreenSaver::Get()->screenshot_path))
            std::filesystem::create_directory(PrintScreenSaver::Get()->screenshot_path, ec);
        if(ec)
            LOG(LogLevel::Error, "Error with create_directory ({}): {}", PrintScreenSaver::Get()->screenshot_path.generic_string(), ec.message());

        TerminalHotkey::Get()->is_enabled = utils::stob(read_setting("TerminalHotkey", "Enable"));
        const std::string& key = read_setting("TerminalHotkey", "Key");
        TerminalHotkey::Get()->SetKey(key);
        TerminalHotkey::Get()->type = static_cast<TerminalType>(utils::stoi<uint8_t>(read_setting("TerminalHotkey", "Type")));

        IdlePowerSaver::Get()->is_enabled = utils::stob(read_setting("IdlePowerSaver", "Enable"));
        IdlePowerSaver::Get()->timeout = utils::stoi<uint32_t>(read_setting("IdlePowerSaver", "Timeout"));
        IdlePowerSaver::Get()->reduced_power_percent = utils::stoi<uint8_t>(read_setting("IdlePowerSaver", "ReducedPowerPercent"));
        IdlePowerSaver::Get()->min_load_threshold = utils::stoi<uint8_t>(read_setting("IdlePowerSaver", "MinLoadThreshold"));
        IdlePowerSaver::Get()->max_load_threshold = utils::stoi<uint8_t>(read_setting("IdlePowerSaver", "MaxLoadThreshold"));

        DirectoryBackup::Get()->SetBackupTimeFormat(read_setting("BackupSettings", "BackupFileFormat"));

        /* load backup configs */
        DirectoryBackup::Get()->Clear();
        size_t backup_counter = 1;
        size_t cnt_ = 0;
        while((cnt_ = pt.count("Backup_" + std::to_string(backup_counter))) == 1)
        {
            const std::string section = "Backup_" + std::to_string(backup_counter);
            const std::string from = read_setting(section, "From");
            const std::string to = read_setting(section, "To");
            const std::string ignore = read_setting(section, "Ignore");
            const size_t max_backups = utils::stoi<size_t>(read_setting(section, "MaxBackups"));
            const bool compress = utils::stob(read_setting(section, "Compress"));
            const bool calculate_hash = utils::stob(read_setting(section, "CalculateHash"));
            const bool buffer_size = utils::stob(read_setting(section, "BufferSize"));
            reader.TrackSection(section);
            DirectoryBackup::Get()->LoadEntry(from, to, ignore, max_backups, compress, calculate_hash, buffer_size);
            backup_counter++;
        }

        uint32_t val1 = utils::stoi<decltype(val1)>(read_setting("Graph", "Graph1HoursBack"));
        DatabaseLogic::Get()->SetGraphHours(0, val1);
        uint32_t val2 = utils::stoi<decltype(val1)>(read_setting("Graph", "Graph2HoursBack"));
        DatabaseLogic::Get()->SetGraphHours(1, val2);

        std::unique_ptr<TimeTracker>& time_tracker = wxGetApp().time_tracker;
        time_tracker->SetHourlyRate(utils::stoi<int>(read_setting("TimeTracker", "HourlyRate")));
        time_tracker->SetToggleKey(read_setting("TimeTracker", "WorktimeCounterKey"));

        const std::string& pages_str = read_setting("App", "UsedPages");
        used_pages = ParseUsedPagesFromString(pages_str);
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Critical,
            "Failed to load {} from settings file '{}': {}. Loading stopped; later settings retain their defaults.",
            reader.Context(), settings_path.generic_string(), e.what());
    }
    catch(...)
    {
        LOG(LogLevel::Critical,
            "Failed to load {} from settings file '{}': unknown exception. Loading stopped; later settings retain their defaults.",
            reader.Context(), settings_path.generic_string());
    }

    if(used_pages.pages == 0)  /* Enable at least the Log panel if everything is disabled */
    {
        used_pages.log = 1;
        LOG(LogLevel::Error, "Enabling log panel, because every panel is disabled");
    }
}

void Settings::SaveFile(bool write_default_macros) /* tried boost::ptree ini writer but it doesn't support comments... sticking to plain file functions */
{
    if(write_default_macros)
        used_pages.pages = 0xFFFF;

    std::unique_ptr<CanEntryHandler>& can_handler = wxGetApp().can_entry;
    std::unique_ptr<ModbusEntryHandler>& modbus_handler = wxGetApp().modbus_handler;
    std::unique_ptr<TimeTracker>& time_tracker = wxGetApp().time_tracker;
    errno = 0;
    std::ofstream out(SETTINGS_FILE_PATH, std::ofstream::binary);
    if(!out.is_open())
    {
        LOG(LogLevel::Critical, "Failed to open settings file '{}' for writing: {}",
            AbsoluteSettingsPath().generic_string(), LastStreamError());
        return;
    }

    out << "# Possible macro keywords: \n";
    out << "# BIND_NAME[binding name] = Set the name if macro. Should be used as first\n";
    out << "# KEY_TYPE[text] = Press & release given keys in sequence to type a text\n";
    out << "# KEY_SEQ[CTRL+C] = Press all given keys after each other and release it when each was pressed - ideal for key shortcats\n";
    out << "# DELAY[time in ms] = Waits for given milliseconds\n";
    out << "# DELAY[min ms - max ms] = Waits randomly between min ms and max ms\n";
    out << "# MOUSE_MOVE[x,y] = Move mouse to given coordinates\n";
    out << "# MOUSE_INTERPOLATE[x,y] = Move mouse with interpolation to given coordinates\n";
    out << "# MOUSE_PRESS[key] = Press given mouse key\n";
    out << "# MOUSE_RELEASE[key] = Release given mouse key\n";
    out << "# MOUSE_CLICK[key] = Click (press and release) with mouse\n";
    out << "# BASH[key] = Execute specified command(s) with command line and keeps terminal shown\n";
    out << "# CMD[key] = Execute specified command(s) with command line without terminal\n";
    out << "# CMD_XML[PageName+CommandName] = Execute predefined command from Cmds.xml\n";
    out << "# CMD_FG[app_name.exe,Window title name] = Bring specified app with given title to the foreground\n";
    out << "# CMD_IMG[path_to_image,offset x,offset y] = Scan for given image on screen and clicks on it if found\n";
    out << "\n";
    out << "[Macro_Config]\n";
    out << "# Use per-application macros. AppName is searched in active window title, so window name must contain AppName\n";
    out << "UsePerApplicationMacros = " << CustomMacro::Get()->use_per_app_macro << "\n";
    out << "\n";
    out << "# If enabled, you can bind multiple key combinations with special keys like RSHIFT + 1, but can't bind SHIFT, CTRL and other special keys alone\n";
    out << "UseAdvancedKeyBinding = " << CustomMacro::Get()->advanced_key_binding << "\n";
    out << "\n";
    out << "# If set to valid key, pressing this key will bring this application to foreground or minimize it to the tray\n";
    out << "BringToForegroundKey = " << CustomMacro::Get()->bring_to_foreground_key << "\n";
    out << "# Key to launch (.py, .js) scripts from file explorer\n";
    out << "ScriptLauncherKey = " << wxGetApp().script_launcher->launcher_key << "\n";
    out << "\n";

    if(!write_default_macros)  /* True if settings.ini file doesn't exists - write a few macro lines here as example */
    {
        int cnt = 0;
        std::string key;
        auto& m = CustomMacro::Get()->GetMacros();
        for(auto& i : m)
        {
            if(!cnt)
                out << "[Keys_Global]\n";
            else
            {
                out << std::format("\n[Keys_Macro{}]\n", cnt);
                out << std::format("AppName = {}\n", i->app_name);
            }
            cnt++;
            for(auto& x : i->key_vec)
            {
                key = std::format("{} = BIND_NAME[{}]", x.first, i->bind_name[x.first]);

                for(auto& k : x.second)
                {
                    IKey* p = k.get();
                    key += p->GenerateText(TextFormat::Ini);
                }
                out << key << '\n';
                key.clear();
            }
        }
    }
    else
    {
        out << "[Keys_Global]\n";
        out << "NUM_0 = BIND_NAME[global macro 1] KEY_SEQ[A+B+C]\n";
        out << "NUM_1 = BIND_NAME[global macro 2] KEY_TYPE[global macro 1]\n";
        out << "\n";
        out << "[Keys_Macro1]\n";
        out << "AppName = Notepad\n";
        out << "NUM_1 = BIND_NAME[close notepad++] KEY_TYPE[test string from WindowsHelper.exe] DELAY[100] KEY_TYPE[Closing window...] DELAY[100-3000] KEY_SEQ[LALT+F4] DELAY[100] KEY_SEQ[RIGHT] KEY_SEQ[ENTER]\n";
    }

    out << "\n";
    out << "[Sensors]\n";
    out << "Enable = " << Server::Get()->IsEnabled() << " # Toggle TCP server" << "\n";
    out << "TCP_Port = " << Server::Get()->GetPort() << " # TCP Port for receiving measurements from sensors\n";
    out << "GraphGenerationInterval = " << Sensors::Get()->GetGraphGenerationInterval() << " # Minutes\n";
    out << "GraphResolution = " << Sensors::Get()->GetGraphResolution() << " # Number of different measurement points in generated graph\n";
    out << "IntegrationTime = " << Sensors::Get()->GetIntegrationTime() << " # Seconds\n";
    out << "MeasurementForward = " << Server::Get()->GetForwardIpAddress() << "\n";
    out << "MeasurementForward2 = " << Server::Get()->GetForwardIpAddress2() << "\n";
    out << "\n";
    out << "[COM_Backend]\n";
    out << "Enable = " << SerialPort::Get()->IsEnabled() << "\n";
    out << "COM = " << SerialPort::Get()->GetComPort() << " # Com port for UART where the data is received from STM32\n";
    out << "ForwardViaTcp = " << SerialPort::Get()->IsForwardToTcp() << " # Is data have to be forwarded to remote TCP server\n";
    out << "RemoteTcpIp = " << SerialPort::Get()->GetRemoteTcpIp() << "\n";
    out << "RemoteTcpPort = " << SerialPort::Get()->GetRemoteTcpPort() << "\n";
    out << "\n";
    out << "[COM_TcpBackend]\n";
    out << "Enable = " << SerialTcpBackend::Get()->is_enabled << " # Listening port from second instance where the TCP Forwarder forwards data received from COM port\n";
    out << "ListeningIp = " << SerialTcpBackend::Get()->bind_ip << "\n";
    out << "ListeningPort = " << SerialTcpBackend::Get()->tcp_port << "\n";
    out << "\n";
    out << "[CANSender]\n";
    out << "Enable = " << CanSerialPort::Get()->IsEnabled() << "\n";
    out << "COM = " << CanSerialPort::Get()->GetComPort() << " # Com port for CAN UART where data is received/sent from/to STM32\n";
    out << "DeviceType = " << static_cast<int>(CanSerialPort::Get()->GetDeviceType()) << " # 0 = STM32, 1 = LAWICEL\n";
    out << "AutoSend = " << can_handler->IsAutoSend() << "\n";
    out << "AutoRecord = " << can_handler->IsAutoRecord() << "\n";
    out << "DefaultRecordingLogLevel = " << static_cast<int>(can_handler->GetRecordingLogLevel()) << "\n";
    out << "DefaultFavouriteLevel = " << static_cast<int>(can_handler->GetFavouriteLevel()) << "\n";
    out << "DefaultEcuId = " << std::format("{:X}", can_handler->GetDefaultEcuId()) << "\n";
    out << "DefaultTxList = " << can_handler->default_tx_list.generic_string() << "\n";
    out << "DefaultRxList = " << can_handler->default_rx_list.generic_string() << "\n";
    out << "DefaultMapping = " << can_handler->default_mapping.generic_string() << "\n";
    out << "\n";
    out << "[ModbusMaster]\n";
    out << "Enable = " << modbus_handler->IsEnabled() << "\n";
    out << "ConnectionType = " << (modbus_handler->GetSerial().IsTcp() ? "TCP" : "RTU") << "\n";
    out << "TcpIp = " << modbus_handler->GetSerial().GetTcpIp() << "\n";
    out << "TcpPort = " << modbus_handler->GetSerial().GetTcpPort() << "\n";
    out << "COM = " << modbus_handler->GetSerial().GetComPort() << " # Com port for Modbus Master UART where data is received/sent from/to Modbus\n";
    out << "PollingRate = " << modbus_handler->GetPollingRate() << "\n";
    out << "ResponseTimeout = " << modbus_handler->GetSerial().m_ResponseTimeout << "\n";
    out << "DefaultModbusConfig = " << modbus_handler->GetDefaultConfigName() << "\n";
    out << "AutoSend = " << modbus_handler->IsAutoSend() << "\n";
    out << "AutoRecord = " << modbus_handler->IsAutoRecord() << "\n";
    out << "MaxRecordedEntries = " << modbus_handler->GetMaxRecordedEntries() << "\n";
    out << "Branch = " << modbus_handler->GetDefaultBranch() << "\n";
    if(!modbus_handler->GetSelectedDevice().empty())
        out << "Device = " << modbus_handler->GetSelectedDevice() << "\n";
    out << "\n";
    out << "[App]\n";
    out << "MinimizeOnExit = " << minimize_on_exit << "\n";
    out << "MinimizeOnStartup = " << minimize_on_startup<< "\n";
    out << "DefaultLogLevel = " << Logger::Get()->GetLogLevelAsString() << "\n";
    out << "LogFilters = " << Logger::Get()->GetLogFilters() << "\n";
    out << "DefaultPage = " << static_cast<uint16_t>(default_page) << "\n";
    out << "UsedPages = " << ParseUsedPagesToString(used_pages) << "\n";
    out << "RememberWindowSize = " << remember_window_size << "\n";
    if(remember_window_size)  /* get frame size when click on Save - not on exit, this is not a bug */
    {
        MyFrame* frame = ((MyFrame*)(wxGetApp().GetTopWindow()));
        window_size = frame->GetSize();
    }
    out << "LastWindowSize = " << std::format("{}, {}", window_size.x, window_size.y) << "\n";
    out << "AlwaysOnNumLock = " << always_on_numlock << "\n";
    out << "SharedDriveLetter = " << shared_drive_letter << "\n";
    out << "CryptoPriceUpdate = " << crypto_price_update << " # Unit: Seconds, 0 = disabled\n";
    out << "\n";
    out << "[CorsairHid]\n";
    out << "Enable = " << CorsairHid::Get()->IsEnabled() << "\n";
    out << "DebouncingInterval = " << CorsairHid::Get()->GetDebouncingInterval() << "\n";
    out << "\n";
    out << "[Screenshot]\n";
    out << "ScreenshotKey = " << PrintScreenSaver::Get()->screenshot_key << "\n";
    out << "ScreenshotDateFormat = " << PrintScreenSaver::Get()->timestamp_format << "\n";
    out << "ScreenshotPath = " << PrintScreenSaver::Get()->screenshot_path.generic_string() << "\n";
    out << "\n";
    out << "[PathSeparator]\n";
    out << "ReplacePathSeparatorKey = " << PathSeparator::Get()->replace_key << "\n";
    out << "\n";
    out << "[TerminalHotkey]\n";
    out << "Enable = " << TerminalHotkey::Get()->is_enabled << "\n";
    out << "Key = " << TerminalHotkey::Get()->GetKey() << "\n";
    out << "Type = " << static_cast<uint32_t>(TerminalHotkey::Get()->type) << " # 0 = WINDOWS_TERMINAL, 1 = cmd.exe, 2 = POWER_SHELL, 3 = BASH_TERMINAL" << "\n";
    out << "\n";
    out << "[IdlePowerSaver]\n";
    out << "Enable = " << IdlePowerSaver::Get()->is_enabled << "\n";
    out << "Timeout = " << IdlePowerSaver::Get()->timeout << "\n";
    out << "ReducedPowerPercent = " << static_cast<int>(IdlePowerSaver::Get()->reduced_power_percent) << "\n";
    out << "MinLoadThreshold = " << static_cast<int>(IdlePowerSaver::Get()->min_load_threshold) << "\n";
    out << "MaxLoadThreshold = " << static_cast<int>(IdlePowerSaver::Get()->max_load_threshold) << "\n";
    out << "\n";
    out << "[BackupSettings]\n";
    out << "BackupFileFormat = " << DirectoryBackup::Get()->GetBackupTimeFormat() << "\n";
    if(!write_default_macros)
    {
        int cnt = 1;
        std::wstring key;
        for(const auto& i : DirectoryBackup::Get()->GetEntries())
        {
            out << std::format("\n[Backup_{}]\n", cnt++);
            out << "From = " << i.from.generic_string() << '\n';
            key.clear();
            for(const auto& x : i.to)
            {
                key += x.generic_wstring() + L'|';
            }
            if(!key.empty() && key.back() == '|')
                key.pop_back();
            out << "To = " << std::string(key.begin(), key.end()) << '\n';
            key.clear();
            for(const auto& x : i.ignore_list)
            {
                key += x + L'|';
            }
            if(!key.empty() && key.back() == '|')
                key.pop_back();

            std::string ignore_list;
            utils::WStringToMBString(key, ignore_list);
            out << "Ignore = " << ignore_list << '\n';
            out << "MaxBackups = " << i.max_backups << '\n';
            out << "Compress = " << i.m_Compress << '\n';
            out << "CalculateHash = " << i.calculate_hash << '\n';
            out << "BufferSize = " << i.hash_buf_size << " # Buffer size for file operations - determines how much data is read once, Unit: Megabytes" << '\n';
        }
    }
    else
    {
        out << "\n[Backup_1]\n";
        out << "From = C:\\Users\\Ati\\Desktop\\folder_from_backup\n";
        out << "To = C:\\Users\\Ati\\Desktop\\folder_where_to_backup|F:\\Backup\\folder_where_to_backup\n";
        out << "Ignore = git/COMMIT_EDITMSG|.git|.vs|Debug|Release|Screenshots|x64|Graphs/Line Chart|Graphs/Temperature.html|Graphs/Humidity.html|Graphs/CO2.html|Graphs/Lux.html|Graphs/VOC.html|Graphs/CCT.html|Graphs/PM10.html|Graphs/PM25.html\n";
        out << "MaxBackups = 5\n";
        out << "Compress = 0\n";
        out << "CalculateHash = 1\n";
        out << "BufferSize = 2\n";
    }
    out << "\n";
    out << "[Graph]\n";
    out << "Graph1HoursBack = " << DatabaseLogic::Get()->GetGraphHours(0) << " # One day\n";
    out << "Graph2HoursBack = " << DatabaseLogic::Get()->GetGraphHours(1) << " # One week\n";
    out << "[TimeTracker]\n";
    out << "HourlyRate = " << time_tracker->GetHourlyRate() << "\n";
    out << "WorktimeCounterKey = " << time_tracker->GetToggleKey() << "\n";
    errno = 0;
    out.flush();
    if(!out)
    {
        LOG(LogLevel::Critical, "Failed while writing settings file '{}': {}",
            AbsoluteSettingsPath().generic_string(), LastStreamError());
    }
}

void Settings::Init()
{
    LoadFile();
}

UsedPages Settings::ParseUsedPagesFromString(const std::string& in)
{
    UsedPages pages;
    pages.pages = 0;
    if(boost::icontains(in, "Main"))
        pages.main = 1;
    if(boost::icontains(in, "StringEscaper"))
        pages.escaper = 1;
    if(boost::icontains(in, "Debug"))
        pages.debug = 1;
    if(boost::icontains(in, "FileBrowser"))
        pages.file_browser = 1;
    if(boost::icontains(in, "CmdExecutor"))
        pages.cmd_executor = 1;
    if(boost::icontains(in, "CanSender"))
        pages.can = 1;
    if(boost::icontains(in, "Did"))
        pages.did = 1;
    if(boost::icontains(in, "ModbusMaster"))
        pages.modbus_master = 1;
    if(boost::icontains(in, "AlarmPanel"))
        pages.alarm_panel = 1;
    if(boost::icontains(in, "TimeTracker"))
        pages.time_tracker = 1;
    if(boost::icontains(in, "Log"))
        pages.log = 1;
    return pages;
}

std::string Settings::ParseUsedPagesToString(UsedPages& in)
{
    std::string pages;
    if(in.main)
        pages += "Main, ";
    if(in.escaper)
        pages += "StringEscaper, ";
    if(in.debug)
        pages += "Debug, ";
    if(in.file_browser)
        pages += "FileBrowser, ";
    if(in.cmd_executor)
        pages += "CmdExecutor, ";
    if(in.can)
        pages += "CanSender, ";
    if(in.did)
        pages += "Did, ";
    if(in.modbus_master)
        pages += "ModbusMaster, ";
    if(in.alarm_panel)
        pages += "AlarmPanel, ";    
    if(in.time_tracker)
        pages += "TimeTracker, ";
    if(in.log)
        pages += "Log, ";
    if(pages.size() > 2)
        pages.pop_back(), pages.pop_back();
    return pages;
}
