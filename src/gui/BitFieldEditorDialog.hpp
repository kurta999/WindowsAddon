#pragma once

#include "interface/IBasicGuiCustomization.hpp"

#include <wx/wx.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace gui
{
// !\brief The most rows the editor can show.
//
// The label/input pairs are created once in the constructor and shown or hidden
// per invocation, so this is a hard ceiling rather than a starting size. It was
// the MAX_BITEDITOR_FIELDS macro, which BitEditorDialog.hpp defined and the
// other two editors picked up by transitive include.
inline constexpr std::size_t kMaxBitFieldRows = 32;

// !\brief One editable row: a label, the value in it, and how the label looks.
struct BitFieldRow
{
    std::string label;
    std::string value;
    std::string tooltip;

    // !\brief Absent leaves the label at the dialog's default appearance.
    // CanMap and ModbusMap both carry a TextStyle; a command's parameter list
    // has no presentation of its own.
    std::optional<TextStyle> style;
};

// !\brief Which button ended the dialog.
enum class BitFieldEditorResult
{
    None,
    Ok,
    Close,
    Apply
};

// !\brief A modal list of labelled values, optionally re-readable as decimal,
// hexadecimal or binary.
//
// This replaced three classes - BitEditorDialog (CAN), ModbusBitEditorDialog
// and CmdExecutorParamDialog - whose implementations were between 90% and 100%
// identical: the same 32 pre-created label/input pairs, the same base radio
// buttons, the same four button handlers, the same output conversion. They
// differed only in their caption, whether the base radio buttons were shown,
// and where a row came from. The first two are Options; the third is the row
// vector the caller builds.
class BitFieldEditorDialog : public wxDialog
{
public:
    struct Options
    {
        // !\brief Caption, when ShowDialog is not given a per-call one.
        wxString title = "Bit editor";

        // !\brief Caption of the box the rows sit in.
        wxString group_label = "&Bit editor";

        // !\brief Offer Decimal/Hex/Binary. The command parameter editor holds
        // free text, so re-reading a parameter as a number would destroy it -
        // which is why that dialog had the radio buttons commented out rather
        // than absent.
        bool allow_base_change = true;
    };

    BitFieldEditorDialog(wxWindow* parent, Options options);

    // !\brief Shows `rows` modally. `title` overrides Options::title when it is
    // not empty, for the CAN editor's per-frame caption.
    //
    // Rows beyond kMaxBitFieldRows are dropped, and the caller is told how many
    // through the return value so it can report which mapping was truncated.
    // Both CAN and Modbus silently resized the caller's vector to do this.
    std::size_t ShowDialog(std::vector<BitFieldRow> rows, const wxString& title = {});

    // !\brief The edited values in row order, converted back to decimal from
    // whichever base is currently displayed.
    [[nodiscard]] std::vector<std::string> GetOutput() const;

    [[nodiscard]] BitFieldEditorResult GetResult() const { return m_result; }

    // !\brief Whether the user asked for the edit to be applied, by either
    // Apply or OK. Both call sites spelled this comparison out.
    [[nodiscard]] bool IsAccepted() const
    {
        return m_result == BitFieldEditorResult::Apply || m_result == BitFieldEditorResult::Ok;
    }

private:
    enum class NumberBase
    {
        Decimal,
        Hex,
        Binary
    };

    void OnApply(wxCommandEvent& event);
    void OnOk(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);
    void OnBaseChanged(wxCommandEvent& event);

    // !\brief Rewrites every input in `base`, from the decimal values the rows
    // were shown with.
    void RedisplayIn(NumberBase base);

    Options m_options;

    wxRadioButton* m_isDecimal = nullptr;
    wxRadioButton* m_isHex = nullptr;
    wxRadioButton* m_isBinary = nullptr;
    wxStaticText* m_labels[kMaxBitFieldRows] = {};
    wxTextCtrl* m_inputs[kMaxBitFieldRows] = {};
    wxSizer* m_sizerTop = nullptr;
    wxSizer* m_sizerRows = nullptr;

    std::vector<BitFieldRow> m_rows;
    NumberBase m_base = NumberBase::Decimal;
    BitFieldEditorResult m_result = BitFieldEditorResult::None;

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_NO_COPY_CLASS(BitFieldEditorDialog);
};
}
