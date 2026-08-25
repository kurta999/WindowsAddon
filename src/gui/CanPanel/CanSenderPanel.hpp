#pragma once

#include "../BitFieldEditorDialog.hpp"
#include "../TextStylePanel.hpp"

#include <wx/wx.h>
#include <wx/grid.h>
#include <wx/clrpicker.h>
#include <wx/fontpicker.h>

#include <map>
#include <optional>

class CanEntryHandler;
class CanTxEntry;
class CanRxData;
class CanByteEditorDialog;
class CanLogForFrameDialog;
class CanUdsRawDialog;
class CanSenderEditDialog;
class CanMap;
class IResultPanel;

enum CanSenderGridCol : int
{
    Sender_Id,
    Sender_DataSize,
    Sender_Data,
    Sender_Period,
    Sender_Count,
    Sender_LogLevel,
    Sender_FavouriteLevel,
    Sender_Comment,
    Sender_Max
};

// !\brief The CAN frame ID held by a grid row, or nothing when that cell does
// not contain a hexadecimal number.
//
// This replaced `std::stoi(grid->GetCellValue(row, Sender_Id).ToStdString(),
// nullptr, 16)` at thirteen call sites. The ID column is user-editable and
// std::stoi throws on input it cannot parse, so each of those could take a
// std::invalid_argument straight out of a wxWidgets event handler. It also
// stopped at the first bad character, so "12G" silently became 0x12 and the
// operation ran against the wrong frame.
//
// It is declared here rather than in two anonymous namespaces: splitting the
// grid classes out into CanGrids.cpp copied the definition along with them,
// leaving one live copy and one dead one to drift apart.
[[nodiscard]] std::optional<uint32_t> FrameIdAt(wxGrid* grid, int row);

class CanGrid
{
public:
    CanGrid(wxWindow* parent);

    void AddRow(wxString id, wxString dlc, wxString data, wxString period, wxString count, wxString loglevel, wxString comment);
    void AddRow(std::unique_ptr<CanTxEntry>& e);
    void RemoveLastRow();
    void UpdateTxCounter(uint32_t frame_id, size_t count);
    wxGrid* m_grid = nullptr;

    std::map<uint16_t, CanTxEntry*> grid_to_entry;  /* Helper map for storing an additional ID to CanTxEntry */

    size_t cnt = 0;
};

class CanGridRx
{
public:
    CanGridRx(wxWindow* parent);

    // !\brief One received frame as the grid needs it: an owning copy taken
    // under the handler's lock, so nothing here points into the live model.
    struct RxRow
    {
        uint32_t frame_id = 0;
        std::vector<uint8_t> data;
        uint32_t period = 0;
        size_t count = 0;
        uint8_t log_level = 0;
        uint8_t favourite_level = 0;
        std::string comment;
    };

    void AddRow(const RxRow& row);
    void UpdateRow(int num_row, const RxRow& row);
    void ClearGrid();

    wxGrid* m_grid = nullptr;

    /* Grid row -> CAN frame ID. This was a map to CanRxData*, so every lookup
       compared pointers into a map the receive thread could rehash, and a
       cleared RX list left every entry dangling. */
    std::map<uint16_t, uint32_t> rx_grid_to_entry;

    size_t cnt = 0;
};

class CanSerialPort;

class CanSenderPanel : public wxPanel
{
public:
    CanSenderPanel(wxWindow* parent, CanEntryHandler& handler, CanSerialPort& port);
    void UpdatePanel();

    void On10MsTimer();
    void RefreshSubpanels();

    void LoadTxList();
    void SaveTxList();
    void LoadRxList();
    void SaveRxList();
    void LoadMapping();
    void SaveMapping();
    void OnKeyDown(wxKeyEvent& evt);
    void UpdateGridForTxFrame(uint32_t frame_id, std::span<const uint8_t> buffer);

    CanGrid* can_grid_tx = nullptr;
    CanGridRx* can_grid_rx = nullptr;

private:
    void RefreshTx();
    void RefreshRx();
    void RefreshGuiIconsBasedOnSettings();

    void OnCellValueChanged(wxGridEvent& ev);
    /*
    void OnCellLeftClick(wxGridEvent& ev);
    void OnCellLeftDoubleClick(wxGridEvent& ev);
    */
    void OnCellRightClick(wxGridEvent& ev);

    /* One named action per context-menu entry. These were the bodies of a
       167-line switch. */
    void ShowRxFrameBits(uint32_t frame_id);
    void EditTxFrameBits(uint32_t frame_id, int row);
    void ShowLogForFrame(uint32_t frame_id, bool is_rx);
    void EditTxFrameStyle(uint32_t frame_id);
    void RemoveRxFrame(uint32_t frame_id);
    void OnGridLabelRightClick(wxGridEvent& ev);
    void OnSize(wxSizeEvent& evt);

    wxStaticBoxSizer* static_box_tx = nullptr;
    wxStaticBoxSizer* static_box_rx = nullptr;

    wxButton* m_SingleShot = nullptr;
    wxButton* m_SendSelected = nullptr;
    wxButton* m_StopSelected = nullptr;
    wxButton* m_SendAll = nullptr;
    wxButton* m_StopAll = nullptr;
    wxButton* m_Add = nullptr;
    wxButton* m_Copy = nullptr;
    wxButton* m_MoveUp = nullptr;
    wxButton* m_MoveDown = nullptr;
    wxButton* m_Delete = nullptr;
    wxButton* m_Edit = nullptr;
    wxButton* m_SendDataFrame = nullptr;
    wxButton* m_SendIsoTp = nullptr;
    wxButton* m_ClearRx = nullptr;

    std::string m_LastDataInput;
    std::string m_LastIsoTpwResponseIDInput;

    gui::BitFieldEditorDialog* m_BitfieldEditor = nullptr;
    CanLogForFrameDialog* m_LogForFrame = nullptr;
    CanUdsRawDialog* m_UdsRawDialog = nullptr;
    CanSenderEditDialog* m_StyleEditDialog = nullptr;

    wxString file_path_tx;
    wxString file_path_rx;
    wxString file_path_mapping;

    std::string search_pattern_tx;
    std::string search_pattern_rx;


    /* Handed in rather than fetched from wxGetApp() on every use.
       docs/code-style.md: "A class gets its collaborators through its
       constructor. It does not fetch them." */
    CanEntryHandler& m_handler;

    /* Only to grey out the three send buttons when CAN is switched off. */
    CanSerialPort& m_Port;

    wxDECLARE_EVENT_TABLE();
};

// !\brief Restyles one TX frame's row.
//
// The five appearance controls are gui::TextStylePanel; this dialog is now the
// frame around them. Its Modbus twin was identical apart from an extra spinner.
class CanSenderEditDialog : public wxDialog
{
public:
    CanSenderEditDialog(wxWindow* parent);

    void ShowDialog(const gui::TextStyleEdit& style);

    [[nodiscard]] gui::TextStyleEdit GetStyle() const { return m_style->GetValue(); }

    bool IsApplyClicked() const { return m_IsApplyClicked; }

protected:
    void OnApply(wxCommandEvent& event);

private:
    gui::TextStylePanel* m_style = nullptr;
    bool m_IsApplyClicked = false;

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_NO_COPY_CLASS(CanSenderEditDialog);
};
