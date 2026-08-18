#include "pch.hpp"

#include "SettingsDialog.hpp"

#include <charconv>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

namespace
{
constexpr const char* SETTINGS_PATH = "./settings.ini";

std::string ToUtf8(const wxString& value)
{
    const wxScopedCharBuffer buffer = value.utf8_str();
    return buffer.data() == nullptr ? std::string{} : std::string(buffer.data(), buffer.length());
}

bool IsBooleanSetting(std::string_view section, std::string_view key)
{
    const std::string full_name = std::string(section) + "." + std::string(key);
    static const std::unordered_set<std::string> boolean_settings = {
        "Sensors.Enable",
        "COM_Backend.Enable",
        "COM_Backend.ForwardViaTcp",
        "COM_TcpBackend.Enable",
        "CANSender.Enable",
        "CANSender.AutoSend",
        "CANSender.AutoRecord",
        "ModbusMaster.Enable",
        "ModbusMaster.AutoSend",
        "ModbusMaster.AutoRecord",
        "App.MinimizeOnExit",
        "App.MinimizeOnStartup",
        "App.RememberWindowSize",
        "App.AlwaysOnNumLock",
        "CorsairHid.Enable",
        "TerminalHotkey.Enable",
        "IdlePowerSaver.Enable",
    };
    return boolean_settings.contains(full_name);
}

bool ParseInteger(std::string_view value, long& result)
{
    if(value.empty())
        return false;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto conversion = std::from_chars(begin, end, result);
    return conversion.ec == std::errc{} && conversion.ptr == end;
}

void SetNumericLimits(wxPGProperty* property, std::string_view section, std::string_view key)
{
    property->SetAttribute(wxPG_ATTR_MIN, 0L);

    if(key == "COM" || key.ends_with("Port") || key == "TCP_Port")
        property->SetAttribute(wxPG_ATTR_MAX, 65535L);
    else if(key == "DeviceType")
        property->SetAttribute(wxPG_ATTR_MAX, 1L);
    else if(section == "TerminalHotkey" && key == "Type")
        property->SetAttribute(wxPG_ATTR_MAX, 3L);
    else if(key == "DefaultRecordingLogLevel" || key == "DefaultFavouriteLevel")
        property->SetAttribute(wxPG_ATTR_MAX, 255L);
    else if(key == "ReducedPowerPercent" || key == "MinLoadThreshold" || key == "MaxLoadThreshold")
        property->SetAttribute(wxPG_ATTR_MAX, 100L);
}

bool ValidateTextSetting(const SettingsIniDocument::Entry& entry, std::string& error)
{
    if(entry.value.find_first_of("\r\n") != std::string::npos)
    {
        error = "[" + entry.section + "] " + entry.key + " cannot contain a line break";
        return false;
    }
    if(entry.section == "App" && entry.key == "SharedDriveLetter" && entry.value.empty())
    {
        error = "[App] SharedDriveLetter cannot be empty";
        return false;
    }
    if(entry.section == "App" && entry.key == "LastWindowSize")
    {
        int width = 0;
        int height = 0;
        char trailing = 0;
        if(std::sscanf(entry.value.c_str(), "%d , %d %c", &width, &height, &trailing) != 2 || width <= 0 || height <= 0)
        {
            error = "[App] LastWindowSize must use the format width, height";
            return false;
        }
    }
    return true;
}
}

SettingsDialog::SettingsDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, "Settings", wxDefaultPosition, wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetSize(FromDIP(wxSize(820, 650)));

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);
    auto* description = new wxStaticText(this, wxID_ANY,
        "Edit settings.ini values below. Macro, command-executor, and directory-backup editors are intentionally excluded.\n"
        "Some layout and connection changes take full effect after restarting WindowsHelper.");
    main_sizer->Add(description, 0, wxEXPAND | wxALL, FromDIP(10));

    m_grid = new wxPropertyGrid(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxPG_SPLITTER_AUTO_CENTER | wxPG_BOLD_MODIFIED);
    m_grid->SetColumnProportion(0, 2);
    m_grid->SetColumnProportion(1, 3);
    main_sizer->Add(m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

    auto* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
    main_sizer->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(10));
    SetSizer(main_sizer);
    SetMinSize(FromDIP(wxSize(620, 450)));
    CentreOnParent();

    if(auto* save_button = FindWindow(wxID_OK))
        save_button->SetLabel("&Save");
    Bind(wxEVT_BUTTON, &SettingsDialog::OnSave, this, wxID_OK);

    std::ifstream input(SETTINGS_PATH, std::ios::binary);
    std::string error;
    if(!input.is_open())
    {
        error = "Unable to open settings.ini";
    }
    else
    {
        std::string content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if(m_document.Parse(std::move(content), error))
        {
            PopulateGrid();
            m_ready = true;
            return;
        }
    }

    if(auto* save_button = FindWindow(wxID_OK))
        save_button->Enable(false);
    wxMessageBox(error, "Unable to load settings", wxOK | wxICON_ERROR, this);
}

void SettingsDialog::PopulateGrid()
{
    std::string current_section;
    const auto& entries = m_document.Entries();
    for(std::size_t index = 0; index < entries.size(); ++index)
    {
        const SettingsIniDocument::Entry& entry = entries[index];
        if(entry.section != current_section)
        {
            current_section = entry.section;
            m_grid->Append(new wxPropertyCategory(wxString::FromUTF8(current_section)));
        }

        const wxString label = wxString::FromUTF8(entry.key);
        const wxString name = wxString::Format("setting_%zu", index);
        wxPGProperty* property = nullptr;
        PropertyType type = PropertyType::Text;

        if(IsBooleanSetting(entry.section, entry.key) && (entry.value == "0" || entry.value == "1"))
        {
            property = new wxBoolProperty(label, name, entry.value == "1");
            property->SetAttribute(wxPG_BOOL_USE_CHECKBOX, true);
            type = PropertyType::Boolean;
        }
        else
        {
            long integer_value = 0;
            if(ParseInteger(entry.value, integer_value))
            {
                property = new wxIntProperty(label, name, integer_value);
                SetNumericLimits(property, entry.section, entry.key);
                type = PropertyType::Integer;
            }
            else
            {
                property = new wxStringProperty(label, name, wxString::FromUTF8(entry.value));
            }
        }

        const std::string location = "[" + entry.section + "] " + entry.key;
        property->SetHelpString(wxString::FromUTF8(entry.comment.empty() ? location : location + " - " + entry.comment));
        m_grid->Append(property);
        m_bindings.push_back({index, type, property});
    }
}

bool SettingsDialog::CopyValuesFromGrid(std::string& error)
{
    if(!m_grid->CommitChangesFromEditor())
    {
        error = "The currently edited value is invalid";
        return false;
    }

    auto& entries = m_document.Entries();
    for(const PropertyBinding& binding : m_bindings)
    {
        SettingsIniDocument::Entry& entry = entries[binding.entry_index];
        switch(binding.type)
        {
            case PropertyType::Boolean:
                entry.value = binding.property->GetValue().GetBool() ? "1" : "0";
                break;
            case PropertyType::Integer:
                entry.value = std::to_string(binding.property->GetValue().GetLong());
                break;
            case PropertyType::Text:
                entry.value = ToUtf8(binding.property->GetValue().GetString());
                if(!ValidateTextSetting(entry, error))
                    return false;
                break;
        }
    }
    return true;
}

bool SettingsDialog::WriteSettings(std::string& error) const
{
    const std::filesystem::path settings_path = std::filesystem::absolute(SETTINGS_PATH).lexically_normal();
    std::filesystem::path temporary_path = settings_path;
    temporary_path += ".tmp";

    std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
    if(!output.is_open())
    {
        error = "Unable to open the temporary settings file for writing";
        return false;
    }
    output << m_document.Render();
    output.flush();
    if(!output)
    {
        error = "Failed while writing the temporary settings file";
        output.close();
        std::error_code remove_error;
        std::filesystem::remove(temporary_path, remove_error);
        return false;
    }
    output.close();

#ifdef _WIN32
    if(!MoveFileExW(temporary_path.c_str(), settings_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        error = "Unable to replace settings.ini (Windows error " + std::to_string(GetLastError()) + ")";
        std::error_code remove_error;
        std::filesystem::remove(temporary_path, remove_error);
        return false;
    }
#else
    std::error_code rename_error;
    std::filesystem::rename(temporary_path, settings_path, rename_error);
    if(rename_error)
    {
        error = "Unable to replace settings.ini: " + rename_error.message();
        std::error_code remove_error;
        std::filesystem::remove(temporary_path, remove_error);
        return false;
    }
#endif
    return true;
}

void SettingsDialog::OnSave(wxCommandEvent& WXUNUSED(event))
{
    std::string error;
    if(!CopyValuesFromGrid(error) || !WriteSettings(error))
    {
        wxMessageBox(wxString::FromUTF8(error), "Unable to save settings", wxOK | wxICON_ERROR, this);
        return;
    }
    EndModal(wxID_OK);
}
