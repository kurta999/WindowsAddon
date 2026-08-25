#include "pch.hpp"

#include "ModbusDialogs.hpp"
#include "ModbusMasterPanel.hpp"
#include "MainFrameAccess.hpp"

ModbusDataEditDialog::ModbusDataEditDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, "Modbus Style editor", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxSizer* const sizerTop = new wxBoxSizer(wxVERTICAL);

    /* This box was labelled "&CAN style properties" - the label came along with
       the rest of the copy from CanSenderEditDialog. */
    wxSizer* const sizerMsgs = new wxStaticBoxSizer(wxVERTICAL, this, "&Modbus style properties");

    m_style = new gui::TextStylePanel(this);
    sizerMsgs->Add(m_style, wxSizerFlags(1).Expand());

    m_floatPrecisionLabel = new wxStaticText(this, wxID_ANY, "Floating-point precision:");
    m_floatPrecision = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
        wxDefaultSize, wxSP_ARROW_KEYS, 0, 9, 3);
    sizerMsgs->Add(m_floatPrecisionLabel);
    sizerMsgs->Add(m_floatPrecision);

    sizerTop->Add(sizerMsgs, wxSizerFlags(1).Expand().Border());
    sizerTop->Add(CreateStdDialogButtonSizer(wxAPPLY | wxCLOSE), wxSizerFlags().Right().Border());

    SetSizerAndFit(sizerTop);
    CentreOnScreen();
}

void ModbusDataEditDialog::ShowDialog(const gui::TextStyleEdit& style, ModbusBitfieldType type,
    uint8_t float_precision)
{
    m_style->SetValue(style);

    const bool floating_point = type == MBT_FLOAT || type == MBT_DOUBLE;
    m_floatPrecisionLabel->Show(floating_point);
    m_floatPrecision->Show(floating_point);
    m_floatPrecision->SetValue(float_precision);
    Layout();
    Fit();

    m_IsApplyClicked = false;
    ShowModal();
}

void ModbusDataEditDialog::OnApply(wxCommandEvent& WXUNUSED(event))
{
    Close();
    m_IsApplyClicked = true;
}


ModbusConditionalColorsDialog::ModbusConditionalColorsDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, "Conditional colors", wxDefaultPosition, wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    auto* root = new wxBoxSizer(wxVERTICAL);
    for(size_t index = 0; index < m_controls.size(); ++index)
        CreateRuleControls(root, index);
    root->Add(CreateStdDialogButtonSizer(wxAPPLY | wxCLOSE), 0, wxALIGN_RIGHT | wxALL, 8);
    SetSizerAndFit(root);
}

void ModbusConditionalColorsDialog::CreateRuleControls(wxSizer* parent, size_t index)
{
    auto* box = new wxStaticBoxSizer(wxVERTICAL, this, wxString::Format("Rule %zu", index + 1));
    const wxArrayString comparisons{ "Not used", "Equal to", "Greater than", "Less than",
        "Greater than or equal", "Less than or equal" };
    auto& controls = m_controls[index];
    controls.comparison = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, comparisons);
    controls.value = new wxSpinCtrlDouble(this, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize,
        wxSP_ARROW_KEYS, -1.0e12, 1.0e12, 0, 0.1);
    controls.useColor = new wxCheckBox(this, wxID_ANY, "Text color");
    controls.color = new wxColourPickerCtrl(this, wxID_ANY);
    controls.useBackgroundColor = new wxCheckBox(this, wxID_ANY, "Background color");
    controls.backgroundColor = new wxColourPickerCtrl(this, wxID_ANY);
    box->Add(controls.comparison, 0, wxEXPAND | wxALL, 3);
    box->Add(controls.value, 0, wxEXPAND | wxALL, 3);
    auto* colors = new wxBoxSizer(wxHORIZONTAL);
    colors->Add(controls.useColor, 0, wxALIGN_CENTER_VERTICAL | wxALL, 3);
    colors->Add(controls.color, 0, wxALL, 3);
    colors->Add(controls.useBackgroundColor, 0, wxALIGN_CENTER_VERTICAL | wxALL, 3);
    colors->Add(controls.backgroundColor, 0, wxALL, 3);
    box->Add(colors);
    parent->Add(box, 0, wxEXPAND | wxALL, 5);
}

void ModbusConditionalColorsDialog::ShowDialog(const std::array<ModbusConditionalColorRule, 2>& rules)
{
    m_rules = rules;
    for(size_t index = 0; index < rules.size(); ++index)
    {
        const auto& rule = rules[index];
        auto& controls = m_controls[index];
        controls.comparison->SetSelection(ComparisonToChoice(rule.comparison));
        controls.value->SetValue(rule.value);
        controls.useColor->SetValue(rule.color.has_value());
        controls.color->SetColour(RGB_TO_WXCOLOR(rule.color.value_or(0)));
        controls.useBackgroundColor->SetValue(rule.background_color.has_value());
        controls.backgroundColor->SetColour(RGB_TO_WXCOLOR(rule.background_color.value_or(0xFFFFFF)));
    }
    m_IsApplyClicked = false;
    ShowModal();
}

void ModbusConditionalColorsDialog::OnApply(wxCommandEvent&)
{
    for(size_t index = 0; index < m_rules.size(); ++index)
    {
        auto& rule = m_rules[index];
        const auto& controls = m_controls[index];
        rule.comparison = ChoiceToComparison(controls.comparison->GetSelection());
        rule.value = controls.value->GetValue();
        rule.color = controls.useColor->GetValue()
            ? std::optional<uint32_t>(WXCOLOR_TO_RGB(controls.color->GetColour().GetRGB())) : std::nullopt;
        rule.background_color = controls.useBackgroundColor->GetValue()
            ? std::optional<uint32_t>(WXCOLOR_TO_RGB(controls.backgroundColor->GetColour().GetRGB())) : std::nullopt;
    }
    m_IsApplyClicked = true;
    Close();
}

ModbusScalingDialog::ModbusScalingDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, "Value scaling", wxDefaultPosition, wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    auto* root = new wxBoxSizer(wxVERTICAL);
    m_enable = new wxCheckBox(this, wxID_ANY, "Enable linear scaling");
    root->Add(m_enable, 0, wxALL, 5);
    auto make_value = [&](const wxString& label, wxSpinCtrlDouble*& control)
    {
        root->Add(new wxStaticText(this, wxID_ANY, label), 0, wxLEFT | wxRIGHT | wxTOP, 5);
        control = new wxSpinCtrlDouble(this, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize,
            wxSP_ARROW_KEYS, -1.0e12, 1.0e12, 0, 0.1);
        control->SetDigits(6);
        root->Add(control, 0, wxEXPAND | wxALL, 5);
    };
    make_value("Raw X1", m_x1); make_value("Display Y1", m_y1);
    make_value("Raw X2", m_x2); make_value("Display Y2", m_y2);
    root->Add(new wxStaticText(this, wxID_ANY, "Display precision"), 0, wxLEFT | wxRIGHT | wxTOP, 5);
    m_precision = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
        wxSP_ARROW_KEYS, 0, 9, 2);
    root->Add(m_precision, 0, wxEXPAND | wxALL, 5);
    m_notice = new wxStaticText(this, wxID_ANY, "Scaling is available for decimal 16/32-bit integer registers.");
    root->Add(m_notice, 0, wxALL, 5);
    m_enable->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { UpdateEnableState(); });
    root->Add(CreateStdDialogButtonSizer(wxAPPLY | wxCLOSE), 0, wxALIGN_RIGHT | wxALL, 8);
    SetSizerAndFit(root);
}

void ModbusScalingDialog::UpdateEnableState()
{
    const bool allowed = IsModbusScalingSupported(m_type) && m_format == MVF_DEC;
    m_enable->Enable(allowed);
    const bool enabled = allowed && m_enable->GetValue();
    const std::array<wxWindow*, 5> controls = { m_x1, m_y1, m_x2, m_y2, m_precision };
    for(wxWindow* control : controls)
        control->Enable(enabled);
    m_notice->Show(!allowed);
    Layout();
}

void ModbusScalingDialog::ShowDialog(const ModbusValueScaling& scaling, ModbusBitfieldType type,
    ModbusValueFormat format)
{
    m_scaling = scaling;
    m_type = type;
    m_format = format;
    m_enable->SetValue(scaling.enabled);
    m_x1->SetValue(scaling.x1); m_y1->SetValue(scaling.y1);
    m_x2->SetValue(scaling.x2); m_y2->SetValue(scaling.y2);
    m_precision->SetValue(scaling.precision);
    m_IsApplyClicked = false;
    UpdateEnableState();
    ShowModal();
}

void ModbusScalingDialog::OnApply(wxCommandEvent&)
{
    if(m_enable->GetValue() && std::abs(m_x1->GetValue() - m_x2->GetValue()) < 0.000001)
    {
        wxMessageBox("X1 and X2 must be different.", "Invalid scaling", wxOK | wxICON_ERROR, this);
        return;
    }
    m_scaling.enabled = m_enable->GetValue() && IsModbusScalingSupported(m_type) && m_format == MVF_DEC;
    m_scaling.x1 = m_x1->GetValue(); m_scaling.y1 = m_y1->GetValue();
    m_scaling.x2 = m_x2->GetValue(); m_scaling.y2 = m_y2->GetValue();
    m_scaling.precision = static_cast<uint8_t>(m_precision->GetValue());
    m_IsApplyClicked = true;
    Close();
}


