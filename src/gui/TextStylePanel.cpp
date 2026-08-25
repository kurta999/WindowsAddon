#include "pch.hpp"

#include "TextStylePanel.hpp"

namespace gui
{
namespace
{
// !\brief What a colour picker shows when no colour is configured.
constexpr uint32_t kUnsetColor = 0xFFFFFF;
}

TextStylePanel::TextStylePanel(wxWindow* parent, Options options)
    : wxPanel(parent, wxID_ANY)
    , m_options(options)
{
    wxSizer* const sizer = new wxBoxSizer(wxVERTICAL);

    if(m_options.optional_colors)
    {
        m_useCustomColor = new wxCheckBox(this, wxID_ANY, "Use custom color?");
        sizer->Add(m_useCustomColor);
    }
    sizer->Add(new wxStaticText(this, wxID_ANY, "&Color:"));
    m_color = new wxColourPickerCtrl(this, wxID_ANY);
    sizer->Add(m_color);

    if(m_options.optional_colors)
    {
        m_useCustomBackgroundColor = new wxCheckBox(this, wxID_ANY, "Use custom background color?");
        sizer->Add(m_useCustomBackgroundColor);
    }
    sizer->Add(new wxStaticText(this, wxID_ANY, "&Background color:"));
    m_backgroundColor = new wxColourPickerCtrl(this, wxID_ANY);
    sizer->Add(m_backgroundColor);

    sizer->Add(new wxStaticText(this, wxID_ANY, "&Bold?"));
    m_isBold = new wxCheckBox(this, wxID_ANY, "");
    sizer->Add(m_isBold);

    sizer->Add(new wxStaticText(this, wxID_ANY, "&Font Type:"));
    m_fontFace = new wxFontPickerCtrl(this, wxID_ANY);
    sizer->Add(m_fontFace);

    sizer->Add(new wxStaticText(this, wxID_ANY, "&Scale:"));
    m_scale = new wxSpinCtrlDouble(this, wxID_ANY, "0.0", wxDefaultPosition, wxDefaultSize,
        16384, 0.0, 10.0, 1.0, 0.2);
    sizer->Add(m_scale);

    SetSizer(sizer);
}

void TextStylePanel::SetValue(const TextStyleEdit& value)
{
    m_color->SetColour(RGB_TO_WXCOLOR(value.color.value_or(kUnsetColor)));
    m_backgroundColor->SetColour(RGB_TO_WXCOLOR(value.background_color.value_or(kUnsetColor)));

    if(m_options.optional_colors)
    {
        m_useCustomColor->SetValue(value.color.has_value());
        m_useCustomBackgroundColor->SetValue(value.background_color.has_value());
    }

    m_isBold->SetValue(value.is_bold);

    if(!value.font_face.empty())
    {
        wxFont font;
        font.SetFaceName(wxString(value.font_face));
        m_fontFace->SetSelectedFont(font);
    }

    m_scale->SetValue(static_cast<double>(value.scale));
}

TextStyleEdit TextStylePanel::GetValue() const
{
    TextStyleEdit value;

    /* Without the checkboxes every colour is "set", which is what a caller that
       stores a plain uint32_t expects. */
    if(!m_options.optional_colors || m_useCustomColor->IsChecked())
    {
        const uint32_t color = m_color->GetColour().GetRGB();
        value.color = WXCOLOR_TO_RGB(color);
    }
    if(!m_options.optional_colors || m_useCustomBackgroundColor->IsChecked())
    {
        const uint32_t color = m_backgroundColor->GetColour().GetRGB();
        value.background_color = WXCOLOR_TO_RGB(color);
    }

    value.is_bold = m_isBold->GetValue();
    value.font_face = m_fontFace->GetSelectedFont().GetFaceName().ToStdString();
    value.scale = static_cast<float>(m_scale->GetValue());

    return value;
}
}
