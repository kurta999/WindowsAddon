#pragma once

#include "SettingsIniDocument.hpp"

#include <wx/dialog.h>

#include <string>
#include <vector>

class wxPropertyGrid;
class wxPGProperty;

class SettingsDialog final : public wxDialog
{
public:
    explicit SettingsDialog(wxWindow* parent);

    bool IsReady() const { return m_ready; }

private:
    enum class PropertyType
    {
        Boolean,
        Integer,
        Text,
    };

    struct PropertyBinding
    {
        std::size_t entry_index = 0;
        PropertyType type = PropertyType::Text;
        wxPGProperty* property = nullptr;
    };

    void PopulateGrid();
    void OnSave(wxCommandEvent& event);
    bool CopyValuesFromGrid(std::string& error);
    bool WriteSettings(std::string& error) const;

    SettingsIniDocument m_document;
    wxPropertyGrid* m_grid = nullptr;
    std::vector<PropertyBinding> m_bindings;
    bool m_ready = false;
};
