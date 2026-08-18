#pragma once

#include "IModbusEntry.hpp"
#include "PendingModbusRowUpdates.hpp"
#include "ModbusRegisterEditor.hpp"
#include <deque>
#include <wx/tipwin.h>

enum ModbusGridCol : int
{
    Modbus_Name,
    Modbus_Value,
	Modbus_Max
};

enum ModbusLogGridCol : int
{
	ModbusLog_Time,
	ModbusLog_Direction,
	ModbusLog_FCode,
	ModbusLog_DataSize,
	ModbusLog_ErrorType,
	ModbusLog_Data,
	ModbusLog_Max
};

enum ModbusSpecialRegisterCol : int
{
	ModbusSpec_Time,
	ModbusSpec_DataHex,
	ModbusSpec_Data,
	ModbusSpec_Max
};

class ModbusMap;
class ModbusConditionalColorsDialog;
class ModbusScalingDialog;
class ModbusGraphFrame;
using ModbusBitfieldInfo = std::vector<std::tuple<std::string, std::string, ModbusMap*>>;

class ModbusBitEditorDialog : public wxDialog
{
public:
	ModbusBitEditorDialog(wxWindow* parent);

	// [label] = value
	void ShowDialog(ModbusBitfieldInfo& values);
	std::vector<std::string> GetOutput();

	enum class BitSelection
	{
		Decimal,
		Hex,
		Binary,
	};

	enum class ClickType
	{
		None,
		Ok,
		Close,
		Apply,
	};

	ClickType GetClickType() { return m_ClickType; }

protected:
	void OnApply(wxCommandEvent& event);
	void OnOk(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);
	void OnRadioButtonClicked(wxCommandEvent& event);
private:
	int m_Id = 0;
	uint8_t m_DataFormat = 0;
	wxRadioButton* m_IsDecimal = {};
	wxRadioButton* m_IsHex = {};
	wxRadioButton* m_IsBinary = {};
	wxStaticText* m_InputLabel[MAX_BITEDITOR_FIELDS] = {};
	wxTextCtrl* m_Input[MAX_BITEDITOR_FIELDS] = {};
	wxRadioButton* m_InputBit[MAX_BITEDITOR_FIELDS] = {};
	wxSizer* sizerTop = {};
	wxSizer* sizerMsgs = {};

	ClickType m_ClickType = ClickType::None;

	ModbusBitfieldInfo m_BitfieldInfo;
	BitSelection bit_sel = BitSelection::Decimal;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ModbusBitEditorDialog);
};

class ModbusDataEditDialog : public wxDialog
{
public:
	ModbusDataEditDialog(wxWindow* parent);

	void ShowDialog(std::optional<uint32_t> color, std::optional<uint32_t> bg_color, bool is_bold,
		const wxString& font_face, float scale, ModbusBitfieldType type, uint8_t float_precision);

	std::optional<uint32_t> GetTextColor();
	std::optional<uint32_t> GetBgColor();

	bool IsBold() { return m_isBold->GetValue(); }
	wxString GetFontFace() { return m_fontFace->GetSelectedFont().GetFaceName(); }
	float GetScale() { return static_cast<float>(m_scale->GetValue()); }
	uint8_t GetFloatPrecision() const { return static_cast<uint8_t>(m_floatPrecision->GetValue()); }

	bool IsApplyClicked() { return m_IsApplyClicked; }
protected:
	void OnApply(wxCommandEvent& event);
	//void OnTimer(wxTimerEvent& event);
private:
	wxCheckBox* m_useCustomColor = nullptr;
	wxColourPickerCtrl* m_color = nullptr;
	wxCheckBox* m_useCustomBackgroundColor = nullptr;
	wxColourPickerCtrl* m_backgroundColor = nullptr;
	wxCheckBox* m_isBold = nullptr;
	wxFontPickerCtrl* m_fontFace = nullptr;
	wxSpinCtrlDouble* m_scale = nullptr;
	wxStaticText* m_floatPrecisionLabel = nullptr;
	wxSpinCtrl* m_floatPrecision = nullptr;

	wxStaticText* m_labelResult = nullptr;
	bool m_IsApplyClicked = false;
	wxTimer* m_timer = nullptr;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ModbusDataEditDialog);
};

class ModbusItemPanel
{
public:
	ModbusItemPanel(wxWindow* parent, const wxString& header_name, ModbusItemType& items, bool is_read_only);

	void AddItem(std::unique_ptr<ModbusItem>& e);
	void UpdatePanel();
	void QueueChanges(const std::vector<uint8_t>& changed_rows) { m_pendingChanges.Add(changed_rows); }
	void ApplyPendingChanges();
	void RefreshItemValues(bool is_clear);
	void UpdateItem(int has_value, int num_row, ModbusItem* item);

	wxGrid* m_grid = nullptr;
	wxStaticBoxSizer* static_box = nullptr;
	ModbusItemType& m_items;
	std::map<uint16_t, ModbusItem*> grid_to_entry;  /* Helper map for storing an additional ID to CanRxData */
	std::string search_pattern;
private:
	bool m_isReadOnly;
	PendingModbusRowUpdates m_pendingChanges;


	//void OnSize(wxSizeEvent& evt);

	//wxDECLARE_EVENT_TABLE();
};

class ModbusDataPanel : public wxPanel
{
public:
	ModbusDataPanel(wxWindow* parent);
	void On10MsTimer();
	bool ChangeDevice(const std::string& device);
	void RefreshDevicePanels();

	wxBoxSizer* m_hSizer = nullptr;

	ModbusDataEditDialog* m_StyleEditDialog = nullptr;
	ModbusConditionalColorsDialog* m_ConditionalColorsDialog = nullptr;
	ModbusScalingDialog* m_ScalingDialog = nullptr;
	ModbusBitEditorDialog* m_BitfieldEditor = nullptr;

	ModbusItemPanel* m_coil = nullptr;
	ModbusItemPanel* m_input = nullptr;
	ModbusItemPanel* m_holding = nullptr;
	ModbusItemPanel* m_inputReg = nullptr;

private:
	void OnCellValueChanged(wxGridEvent& ev);
	void OnCellRightClick(wxGridEvent& ev);
	void OnGridLabelLeftClick(wxGridEvent& ev);
	void OnGridLabelRightClick(wxGridEvent& ev);
	void OnKeyDown(wxKeyEvent& evt);
	void HandleRegisterEdit(ModbusRegisterEditAction action);

	void OnSize(wxSizeEvent& event);

	wxButton* m_SaveButton = nullptr;
	wxButton* m_StartButton = nullptr;
	wxButton* m_StopButton = nullptr;
	wxSpinCtrl* m_SlaveId = nullptr;
	wxSpinCtrl* m_PollingRate = nullptr;
	wxSpinCtrl* m_ResponseTimeout = nullptr;
	wxButton* m_Clear = nullptr;
	wxTextCtrl* m_TcpIp = nullptr;
	wxSpinCtrl* m_TcpPort = nullptr;
	wxSpinCtrl* m_ComPort = nullptr;
	wxCheckBox* m_UseTcp = nullptr;
	wxTextCtrl* m_DefaultBranch = nullptr;
	wxChoice* m_DeviceChoice = nullptr;
	wxButton* m_ExportButton = nullptr;
	wxButton* m_ImportButton = nullptr;
	wxButton* m_RegisterMoveUpButton = nullptr;
	wxButton* m_RegisterMoveDownButton = nullptr;
	wxButton* m_RegisterDeleteButton = nullptr;
	wxButton* m_RegisterInsertButton = nullptr;
	wxButton* m_GraphButton = nullptr;
	wxStaticText* m_ConnectionStatus = nullptr;
	wxStaticText* m_LastErrorText = nullptr;
	wxTipWindow* tip = nullptr;
	ModbusGraphFrame* m_GraphFrame = nullptr;


	wxDECLARE_EVENT_TABLE();
};

class ModbusLogPanel : public wxPanel, public IModbusHelper
{
public:
	ModbusLogPanel(wxWindow* parent);
	~ModbusLogPanel() override;

	void AppendLog(std::chrono::steady_clock::time_point& t1, uint8_t direction, uint8_t fcode, uint8_t error, const std::vector<uint8_t>& data) override;
	void OnMaxEntriesReached() override;
	void RefreshItems() override;
	void QueueValueChanges(IModbusHelper::Table table, const std::vector<uint8_t>& rows) override;
	void ClearRecordingsFromGrid();
	void On10MsTimer();
	void OnKeyDown(wxKeyEvent& evt);

	wxListBox* m_DataLog = nullptr;
	wxStaticBoxSizer* static_box = nullptr;
	wxGrid* m_grid = nullptr;

private:
	void OnSize(wxSizeEvent& event);

	wxButton* m_RecordingStart = nullptr;
	wxButton* m_RecordingPause = nullptr;
	wxButton* m_RecordingStop = nullptr;
	wxButton* m_RecordingClear = nullptr;
	wxButton* m_AutoScrollBtn = nullptr;
	wxButton* m_RecordingSave = nullptr;

	bool m_AutoScroll = false;
	size_t cnt = 0;
	bool is_something_inserted = false;
	std::size_t inserted_until = 0;
	std::array<PendingModbusRowUpdates, 4> m_pendingValueChanges;

	wxDECLARE_EVENT_TABLE();
};

class ModbusSpecialRegisterPanel : public wxPanel
{
public:
	ModbusSpecialRegisterPanel(wxWindow* parent);
	~ModbusSpecialRegisterPanel() = default;

	void On10MsTimer();
	void OnKeyDown(wxKeyEvent& evt);
	void AppendLog(std::chrono::steady_clock::time_point t1, const std::vector<uint16_t>& data);
	void ClearRecordingsFromGrid();

	wxListBox* m_DataLog = nullptr;
	wxStaticBoxSizer* static_box = nullptr;
	wxGrid* m_grid = nullptr;

private:
	void OnSize(wxSizeEvent& event);

	wxButton* m_RecordingStart = nullptr;
	wxButton* m_RecordingPause = nullptr;
	wxButton* m_RecordingStop = nullptr;
	wxButton* m_RecordingClear = nullptr;
	wxButton* m_AutoScrollBtn = nullptr;
	wxButton* m_RecordingSave = nullptr;

	bool m_AutoScroll = true;
	size_t cnt = 0;
	bool is_something_inserted = false;
	std::size_t inserted_until = 0;

	wxDECLARE_EVENT_TABLE();
};

class ModbusMasterPanel : public wxPanel
{
public:
	ModbusMasterPanel(wxWindow* parent);
	~ModbusMasterPanel();

	void UpdateSubpanels();
	void On10MsTimer();

	wxAuiNotebook* m_notebook = nullptr;
	ModbusDataPanel* data_panel = nullptr;
	ModbusLogPanel* log_panel = nullptr;
	ModbusSpecialRegisterPanel* special_panel = nullptr;

private:
	void OnSize(wxSizeEvent& evt);
	void Changeing(wxAuiNotebookEvent& event);

	wxAuiManager m_mgr;

	wxDECLARE_EVENT_TABLE();
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

struct ModbusGraphPoint { double seconds = 0.0; double value = 0.0; };
struct ModbusGraphSeries
{
	ModbusItem* item = nullptr;
	wxString name;
	wxColour color;
	std::deque<ModbusGraphPoint> points;
};
class ModbusGraphCanvas : public wxPanel
{
public:
	explicit ModbusGraphCanvas(ModbusGraphFrame* parent);
private:
	void OnPaint(wxPaintEvent& event);
	ModbusGraphFrame* m_owner = nullptr;
};
class ModbusGraphFrame : public wxFrame
{
public:
	static constexpr size_t MaxItems = 10;
	static constexpr size_t MaxSamplesPerItem = 300;
	explicit ModbusGraphFrame(wxWindow* parent);
	bool AddItem(ModbusItem* item);
	void RemoveItem(ModbusItem* item);
	void RecordSamples(const ModbusItemType& items);
	std::vector<ModbusGraphSeries> GetSeriesSnapshot() const;
private:
	void OnRemoveSelected(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);
	void RefreshWatchedItems();
	ModbusGraphCanvas* m_canvas = nullptr;
	wxListBox* m_items = nullptr;
	wxButton* m_removeButton = nullptr;
	std::vector<ModbusGraphSeries> m_series;
	std::chrono::steady_clock::time_point m_startTime;
	mutable std::mutex m_seriesMutex;
};
