#include "pch.hpp"

#include "BitFieldEditorDialog.hpp"

namespace gui
{
wxBEGIN_EVENT_TABLE(BitFieldEditorDialog, wxDialog)
EVT_BUTTON(wxID_APPLY, BitFieldEditorDialog::OnApply)
EVT_BUTTON(wxID_OK, BitFieldEditorDialog::OnOk)
EVT_BUTTON(wxID_CLOSE, BitFieldEditorDialog::OnCancel)
EVT_CLOSE(BitFieldEditorDialog::OnClose)
wxEND_EVENT_TABLE()

namespace
{
// !\brief Applies a row's presentation to its label, or leaves the label at the
// dialog's default when the row has none.
void ApplyStyle(wxStaticText* label, const std::optional<TextStyle>& style)
{
    if(!style)
        return;

    /* input for red: 0x00FF0000, expected input for wxColor 0x0000FF */
    label->SetForegroundColour(RGB_TO_WXCOLOR(style->m_color));
    label->SetBackgroundColour(RGB_TO_WXCOLOR(style->m_bg_color));

    wxFont font;
    font.SetWeight(style->m_is_bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
    font.Scale(1.0f);  /* Scale has to be set to default first */
    label->SetFont(font);
    font.Scale(style->m_scale);
    font.SetFaceName(style->m_font_face.empty() ? wxString("Segoe UI") : wxString(style->m_font_face));
    label->SetFont(font);
}
}

BitFieldEditorDialog::BitFieldEditorDialog(wxWindow* parent, Options options)
    : wxDialog(parent, wxID_ANY, options.title, wxDefaultPosition, wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_options(std::move(options))
{
    m_sizerTop = new wxBoxSizer(wxVERTICAL);
    m_sizerRows = new wxStaticBoxSizer(wxVERTICAL, this, m_options.group_label);

    if(m_options.allow_base_change)
    {
        wxBoxSizer* h_sizer = new wxBoxSizer(wxHORIZONTAL);

        m_isDecimal = new wxRadioButton(this, wxID_ANY, "Decimal");
        m_isDecimal->Bind(wxEVT_RADIOBUTTON, &BitFieldEditorDialog::OnBaseChanged, this);
        h_sizer->Add(m_isDecimal);
        m_isHex = new wxRadioButton(this, wxID_ANY, "Hex");
        m_isHex->Bind(wxEVT_RADIOBUTTON, &BitFieldEditorDialog::OnBaseChanged, this);
        h_sizer->Add(m_isHex);
        m_isBinary = new wxRadioButton(this, wxID_ANY, "Binary");
        m_isBinary->Bind(wxEVT_RADIOBUTTON, &BitFieldEditorDialog::OnBaseChanged, this);
        h_sizer->Add(m_isBinary);

        m_sizerRows->Add(h_sizer);
    }

    m_sizerRows->AddSpacer(20);

    for(std::size_t i = 0; i != kMaxBitFieldRows; i++)
    {
        m_labels[i] = new wxStaticText(this, wxID_ANY, "_");
        m_sizerRows->Add(m_labels[i], 1, wxLEFT | wxEXPAND, 0);
        m_inputs[i] = new wxTextCtrl(this, wxID_ANY, "_", wxDefaultPosition, wxSize(250, 25), 0);
        m_sizerRows->Add(m_inputs[i], 1, wxLEFT | wxEXPAND, 0);
    }

    m_sizerTop->Add(m_sizerRows, wxSizerFlags(1).Expand().Border());
    m_sizerTop->Add(CreateStdDialogButtonSizer(wxAPPLY | wxCLOSE | wxOK), wxSizerFlags().Right().Border());

    m_sizerTop->SetMinSize(wxSize(200, 200));
    SetAutoLayout(true);
    SetSizer(m_sizerTop);
    m_sizerTop->Fit(this);
    CentreOnScreen();
}

std::size_t BitFieldEditorDialog::ShowDialog(std::vector<BitFieldRow> rows, const wxString& title)
{
    const std::size_t requested = rows.size();
    if(rows.size() > kMaxBitFieldRows)
        rows.resize(kMaxBitFieldRows);

    m_rows = std::move(rows);

    for(std::size_t i = 0; i != m_rows.size(); i++)
    {
        m_labels[i]->SetLabelText(m_rows[i].label);
        ApplyStyle(m_labels[i], m_rows[i].style);
        m_labels[i]->SetToolTip(m_rows[i].tooltip);
        m_labels[i]->Show();

        m_inputs[i]->SetValue(m_rows[i].value);
        m_inputs[i]->Show();
    }

    for(std::size_t i = m_rows.size(); i != kMaxBitFieldRows; i++)
    {
        m_labels[i]->Hide();
        m_labels[i]->SetToolTip("");
        m_inputs[i]->Hide();
    }

    SetTitle(title.IsEmpty() ? m_options.title : title);

    m_base = NumberBase::Decimal;
    if(m_options.allow_base_change)
    {
        m_isDecimal->SetValue(true);
        m_isHex->SetValue(false);
        m_isBinary->SetValue(false);
    }

    m_sizerTop->Layout();
    m_sizerTop->Fit(this);

    m_result = BitFieldEditorResult::None;
    ShowModal();

    return requested;
}

std::vector<std::string> BitFieldEditorDialog::GetOutput() const
{
    std::vector<std::string> ret;
    ret.reserve(m_rows.size());

    for(std::size_t i = 0; i != m_rows.size(); i++)
    {
        std::string text = m_inputs[i]->GetValue().ToStdString();

        /* The caller always wants decimal. When the user switched the display
           to hex or binary, what is in the box is in that base. */
        if(m_base != NumberBase::Decimal)
        {
            const int base = m_base == NumberBase::Hex ? 16 : 2;
            if(const auto parsed = utils::TryParse<int64_t>(text, utils::ParseMode::Whole, base))
                text = std::to_string(*parsed);
            else
                LOG(LogLevel::Error, "Bit field '{}' is not a valid base-{} number: '{}'",
                    m_rows[i].label, base, text);
        }

        ret.push_back(std::move(text));
    }
    return ret;
}

void BitFieldEditorDialog::RedisplayIn(NumberBase base)
{
    for(std::size_t i = 0; i != m_rows.size(); i++)
    {
        if(base == NumberBase::Decimal)
        {
            m_inputs[i]->SetValue(m_rows[i].value);
            continue;
        }

        /* Both predecessors advanced their input index only when the value
           parsed, so one non-numeric field shifted every later field's value
           up into the wrong box. The index is the row's, not a counter. */
        const auto decimal = utils::TryParse<int64_t>(m_rows[i].value);
        if(!decimal)
            continue;

        /* std::to_chars gives lower case letters, I don't like it :/ */
        m_inputs[i]->SetValue(base == NumberBase::Hex ? std::format("{:X}", *decimal)
                                                      : std::format("{:b}", *decimal));
    }

    m_base = base;
}

void BitFieldEditorDialog::OnBaseChanged(wxCommandEvent& event)
{
    const wxObject* source = event.GetEventObject();
    if(source == static_cast<wxObject*>(m_isDecimal))
        RedisplayIn(NumberBase::Decimal);
    else if(source == static_cast<wxObject*>(m_isHex))
        RedisplayIn(NumberBase::Hex);
    else if(source == static_cast<wxObject*>(m_isBinary))
        RedisplayIn(NumberBase::Binary);
}

void BitFieldEditorDialog::OnApply(wxCommandEvent& WXUNUSED(event))
{
    m_result = BitFieldEditorResult::Apply;
    EndModal(wxID_APPLY);
}

void BitFieldEditorDialog::OnOk(wxCommandEvent& event)
{
    if(m_result == BitFieldEditorResult::Close)
        return;

    m_result = BitFieldEditorResult::Ok;
    EndModal(wxID_OK);
    event.Skip();
}

void BitFieldEditorDialog::OnCancel(wxCommandEvent& WXUNUSED(event))
{
    m_result = BitFieldEditorResult::Close;
    EndModal(wxID_CLOSE);
}

void BitFieldEditorDialog::OnClose(wxCloseEvent& WXUNUSED(event))
{
    m_result = BitFieldEditorResult::Close;
    EndModal(wxID_CLOSE);
}
}
