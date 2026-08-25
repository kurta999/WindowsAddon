#pragma once

#include "interface/IBasicGuiCustomization.hpp"

#include <wx/wx.h>
#include <wx/clrpicker.h>
#include <wx/fontpicker.h>
#include <wx/spinctrl.h>

#include <optional>
#include <string>

namespace gui
{
// !\brief The appearance values the panel edits.
//
// This is TextStyle with optional colours: "no colour configured" and
// "configured black" render differently, which is the distinction
// ModbusItemPresentation already makes.
struct TextStyleEdit
{
    std::optional<uint32_t> color;
    std::optional<uint32_t> background_color;
    bool is_bold = false;
    std::string font_face;
    float scale = 1.0f;
};

// !\brief The colour / background / bold / font / scale block, as one widget.
//
// CmdExecutorEditDialog, ModbusDataEditDialog and CanSenderEditDialog each
// built these five controls by hand in the same order with the same labels and
// the same magic wxSpinCtrlDouble arguments, and each read them back through
// its own five accessors. The copy had already drifted: the Modbus dialog's
// group box is labelled "&CAN style properties" because that is what it was
// copied from.
//
// Panels that need more - the Modbus float precision, the command editor's
// name and command line - add their own controls around this one.
class TextStylePanel : public wxPanel
{
public:
    struct Options
    {
        // !\brief Offer a "use custom colour?" checkbox per colour.
        //
        // The command editor stores plain uint32_t colours with no "unset"
        // state, so it turns these off and always gets a value back.
        bool optional_colors = true;
    };

    TextStylePanel(wxWindow* parent, Options options = {});

    void SetValue(const TextStyleEdit& value);
    [[nodiscard]] TextStyleEdit GetValue() const;

private:
    Options m_options;

    wxCheckBox* m_useCustomColor = nullptr;
    wxColourPickerCtrl* m_color = nullptr;
    wxCheckBox* m_useCustomBackgroundColor = nullptr;
    wxColourPickerCtrl* m_backgroundColor = nullptr;
    wxCheckBox* m_isBold = nullptr;
    wxFontPickerCtrl* m_fontFace = nullptr;
    wxSpinCtrlDouble* m_scale = nullptr;

    wxDECLARE_NO_COPY_CLASS(TextStylePanel);
};
}
