#pragma once

// The modal editors the Modbus master panel opens: the bit editor, the value
// editor, the conditional-colour rules and the two-point scaling.
//
// These lived in ModbusMasterPanel.hpp together with the panels and the graph
// window - thirteen classes in one header, so touching any dialog recompiled
// the whole 2400-line panel translation unit.

#include "IModbusEntry.hpp"
#include "ModbusConditionalColors.hpp"
#include "TextStylePanel.hpp"
#include <wx/wx.h>
#include <wx/grid.h>
#include <wx/clrpicker.h>
#include <vector>
#include <string>
#include <tuple>

class ModbusMap;
using ModbusBitfieldInfo = std::vector<std::tuple<std::string, std::string, ModbusMap*>>;

// The rule comparison as the dialog's choice list numbers it. Shared with the
// data panel, which renders the same rules.
inline int ComparisonToChoice(ModbusConditionalColorComparison comparison)
{
    return comparison == ModbusConditionalColorComparison::EqualTo ? 1 :
        comparison == ModbusConditionalColorComparison::GreaterThan ? 2 :
        comparison == ModbusConditionalColorComparison::LessThan ? 3 :
        comparison == ModbusConditionalColorComparison::GreaterThanOrEqualTo ? 4 :
        comparison == ModbusConditionalColorComparison::LessThanOrEqualTo ? 5 : 0;
}

inline ModbusConditionalColorComparison ChoiceToComparison(int choice)
{
    switch(choice)
    {
        case 1: return ModbusConditionalColorComparison::EqualTo;
        case 2: return ModbusConditionalColorComparison::GreaterThan;
        case 3: return ModbusConditionalColorComparison::LessThan;
        case 4: return ModbusConditionalColorComparison::GreaterThanOrEqualTo;
        case 5: return ModbusConditionalColorComparison::LessThanOrEqualTo;
        default: return ModbusConditionalColorComparison::NotUsed;
    }
}


// !\brief Restyles one register's row.
//
// The five appearance controls are gui::TextStylePanel, shared with the CAN
// style editor this was copied from - along with its "&CAN style properties"
// group label, which is why that box used to be mislabelled here.
class ModbusDataEditDialog : public wxDialog
{
public:
	ModbusDataEditDialog(wxWindow* parent);

	void ShowDialog(const gui::TextStyleEdit& style, ModbusBitfieldType type, uint8_t float_precision);

	[[nodiscard]] gui::TextStyleEdit GetStyle() const { return m_style->GetValue(); }
	[[nodiscard]] uint8_t GetFloatPrecision() const { return static_cast<uint8_t>(m_floatPrecision->GetValue()); }

	bool IsApplyClicked() const { return m_IsApplyClicked; }

protected:
	void OnApply(wxCommandEvent& event);

private:
	gui::TextStylePanel* m_style = nullptr;
	wxStaticText* m_floatPrecisionLabel = nullptr;
	wxSpinCtrl* m_floatPrecision = nullptr;

	bool m_IsApplyClicked = false;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ModbusDataEditDialog);
};


class ModbusConditionalColorsDialog : public wxDialog
{
public:
	explicit ModbusConditionalColorsDialog(wxWindow* parent);
	void ShowDialog(const std::array<ModbusConditionalColorRule, 2>& rules);
	const std::array<ModbusConditionalColorRule, 2>& GetRules() const { return m_rules; }
	bool IsApplyClicked() const { return m_IsApplyClicked; }
private:
	struct RuleControls
	{
		wxChoice* comparison = nullptr;
		wxSpinCtrlDouble* value = nullptr;
		wxCheckBox* useColor = nullptr;
		wxColourPickerCtrl* color = nullptr;
		wxCheckBox* useBackgroundColor = nullptr;
		wxColourPickerCtrl* backgroundColor = nullptr;
	};
	void OnApply(wxCommandEvent& event);
	void CreateRuleControls(wxSizer* parent, size_t index);
	std::array<RuleControls, 2> m_controls;
	std::array<ModbusConditionalColorRule, 2> m_rules;
	bool m_IsApplyClicked = false;
	wxDECLARE_EVENT_TABLE();
};

class ModbusScalingDialog : public wxDialog
{
public:
	explicit ModbusScalingDialog(wxWindow* parent);
	void ShowDialog(const ModbusValueScaling& scaling, ModbusBitfieldType type, ModbusValueFormat format);
	const ModbusValueScaling& GetScaling() const { return m_scaling; }
	bool IsApplyClicked() const { return m_IsApplyClicked; }
private:
	void OnApply(wxCommandEvent& event);
	void UpdateEnableState();
	wxSpinCtrlDouble* m_x1 = nullptr;
	wxSpinCtrlDouble* m_y1 = nullptr;
	wxSpinCtrlDouble* m_x2 = nullptr;
	wxSpinCtrlDouble* m_y2 = nullptr;
	wxSpinCtrl* m_precision = nullptr;
	wxCheckBox* m_enable = nullptr;
	wxStaticText* m_notice = nullptr;
	ModbusValueScaling m_scaling;
	ModbusBitfieldType m_type = MBT_INVALID;
	ModbusValueFormat m_format = MVF_DEC;
	bool m_IsApplyClicked = false;
	wxDECLARE_EVENT_TABLE();
};
