#pragma once

#include "IModbusEntry.hpp"
#include "PendingModbusRowUpdates.hpp"
#include "BitFieldEditorDialog.hpp"
#include "ModbusDialogs.hpp"
#include "ModbusGraphView.hpp"
#include "ModbusRegisterEditor.hpp"
#include "gui/LogRecordingBar.hpp"
#include <memory>
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

class ModbusItemPanel
{
public:
	ModbusItemPanel(wxWindow* parent, ModbusEntryHandler& handler, const wxString& header_name,
		ModbusEntryHandler::Table table, bool is_read_only);

	void AddItem(size_t item_index, const ModbusItem& item);
	void UpdatePanel();
	void QueueChanges(const std::vector<uint8_t>& changed_rows) { m_pendingChanges.Add(changed_rows); }
	void ApplyPendingChanges();
	void RefreshItemValues(bool is_clear);

	// !\brief Paint one row from an already-rendered cell.
	void ShowRender(int num_row, const ModbusCellRender& render);

	// !\brief Blank one row, for when polling is stopped.
	void ClearRow(int num_row);

	// !\brief Which register table this panel shows.
	[[nodiscard]] ModbusEntryHandler::Table TableId() const { return m_table; }

	// !\brief The item index shown in a grid row, if that row holds one.
	[[nodiscard]] std::optional<size_t> ItemIndexForRow(int row) const;

	wxGrid* m_grid = nullptr;
	wxStaticBoxSizer* static_box = nullptr;

	/* Grid row -> index into the handler's table.
	   This was a map to ModbusItem*, which meant every lookup was a linear
	   scan comparing pointers, and every entry dangled the moment the handler
	   reloaded its tables for a different device. An index survives that. */
	std::map<uint16_t, size_t> grid_to_entry;
	std::string search_pattern;
private:
	/* Handed in rather than fetched from wxGetApp() on every use. */
	ModbusEntryHandler& m_handler;
	ModbusEntryHandler::Table m_table;
	bool m_isReadOnly;
	PendingModbusRowUpdates m_pendingChanges;


	//void OnSize(wxSizeEvent& evt);

	//wxDECLARE_EVENT_TABLE();
};

// !\brief What the log panel asks of the data panel: forward freshly changed
// rows, and re-render the writable grids after a write goes out.
//
// The log panel used to reach its sibling through
// dynamic_cast<ModbusMasterPanel*>(GetParent())->data_panel->m_coil... - and
// since the pages moved into a wxAuiNotebook, which reparents them (AddPage
// calls page->Reparent(this)), that cast has been null every time: the
// guarded blocks behind it were unreachable, so queued value changes never
// reached the grids from here and the after-write refresh never ran.
class IModbusRowSink
{
public:
    virtual ~IModbusRowSink() = default;
    virtual void QueueRowChanges(IModbusValueObserver::Table table, std::vector<uint8_t> rows) = 0;
    virtual void RefreshWriteTargets() = 0;
};

class ModbusDataPanel : public wxPanel, public IModbusRowSink
{
public:
	void QueueRowChanges(IModbusValueObserver::Table table, std::vector<uint8_t> rows) override;
	void RefreshWriteTargets() override;

	ModbusDataPanel(wxWindow* parent, ModbusEntryHandler& handler);
	void On10MsTimer();
	bool ChangeDevice(const std::string& device);
	void RefreshDevicePanels();

	wxBoxSizer* m_hSizer = nullptr;

	ModbusDataEditDialog* m_StyleEditDialog = nullptr;
	ModbusConditionalColorsDialog* m_ConditionalColorsDialog = nullptr;
	ModbusScalingDialog* m_ScalingDialog = nullptr;
	gui::BitFieldEditorDialog* m_BitfieldEditor = nullptr;

	ModbusItemPanel* m_coil = nullptr;
	ModbusItemPanel* m_input = nullptr;
	ModbusItemPanel* m_holding = nullptr;
	ModbusItemPanel* m_inputReg = nullptr;

private:
	void OnCellValueChanged(wxGridEvent& ev);
	void OnCellRightClick(wxGridEvent& ev);

	/* The constructor was 249 lines. Each of these is one row of the layout it
	   already had, so the constructor now reads as the layout. */
	void CreateDialogs();
	void BuildTablePanels(wxSizer& parent);
	void BuildConnectionToolbar(wxSizer& parent);
	void BuildPollingToolbar(wxSizer& parent);
	void BuildActionToolbar(wxSizer& parent);
	void BuildStatusBar(wxSizer& parent);

	/* OnCellValueChanged was four near-identical per-grid blocks; these are the
	   two halves that actually differed. */
	[[nodiscard]] ModbusItemPanel* PanelForGrid(const wxObject* grid) const;
	void ApplyCoilEdit(ModbusItemPanel& panel, int row, size_t index, const wxString& text);
	void ApplyHoldingEdit(ModbusItemPanel& panel, int row, size_t index, const wxString& text);

	/* One named action per context-menu entry. These were the bodies of a
	   203-line switch inside OnCellRightClick. */
	void ShowItemBits(ModbusItemPanel* item_panel, size_t item_index);
	void EditItemStyle(ModbusItemPanel* item_panel, size_t item_index);
	void EditConditionalColors(ModbusItemPanel* item_panel, size_t item_index);
	void ChangeItemType(ModbusItemPanel* item_panel, size_t item_index, ModbusBitfieldType new_type);
	void EditItemScaling(ModbusItemPanel* item_panel, size_t item_index);
	void WatchItemInGraph(ModbusItemPanel* item_panel, size_t item_index);
	void SetItemFormat(ModbusItemPanel* item_panel, size_t item_index, ModbusValueFormat format);
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
	/* Was a function-local static, so its epoch belonged to the first tick
	   of the first instance ever, not to this panel. */
	std::chrono::steady_clock::time_point m_LastGraphSample = std::chrono::steady_clock::now();


	/* The handler is handed in rather than fetched from wxGetApp() on every
	   use. docs/code-style.md: "A class gets its collaborators through its
	   constructor. It does not fetch them." */
	ModbusEntryHandler& m_handler;

	wxDECLARE_EVENT_TABLE();
};

class ModbusLogPanel : public wxPanel, public IModbusValueObserver, public IModbusLogView
{
public:
	ModbusLogPanel(wxWindow* parent, ModbusEntryHandler& handler, IModbusRowSink& row_sink);
	~ModbusLogPanel() override;

	void AppendLog(std::chrono::steady_clock::time_point& t1, uint8_t direction, uint8_t fcode, uint8_t error, const std::vector<uint8_t>& data) override;
	void OnMaxEntriesReached() override;
	void RefreshItems() override;
	void QueueValueChanges(IModbusValueObserver::Table table, const std::vector<uint8_t>& rows) override;
	void ClearRecordingsFromGrid();
	void On10MsTimer();
	void OnKeyDown(wxKeyEvent& evt);

	wxListBox* m_DataLog = nullptr;
	wxStaticBoxSizer* static_box = nullptr;
	wxGrid* m_grid = nullptr;

private:
	void OnSize(wxSizeEvent& event);

	std::unique_ptr<gui::LogRecordingBar> m_RecordingBar;

	/* Change detectors for the counter label. Were function-local statics
	   in the tick - state shared by every instance of this panel. */
	uint64_t m_LastShownTxCount = 0;
	uint64_t m_LastShownRxCount = 0;
	uint64_t m_LastShownErrCount = 0;

	size_t cnt = 0;

	/* How far this grid has consumed the recording. The worker thread owns the
	   buffer, so the panel takes the new entries by value instead of walking
	   it. */
	IModbusRowSink& m_RowSink;
	ModbusLogCursor m_LogCursor;
	std::array<PendingModbusRowUpdates, 4> m_pendingValueChanges;

	/* The handler is handed in rather than fetched from wxGetApp() on every
	   use. docs/code-style.md: "A class gets its collaborators through its
	   constructor. It does not fetch them." */
	ModbusEntryHandler& m_handler;

	wxDECLARE_EVENT_TABLE();
};

class ModbusSpecialRegisterPanel : public wxPanel
{
public:
	ModbusSpecialRegisterPanel(wxWindow* parent, ModbusEntryHandler& handler);
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

	std::unique_ptr<gui::LogRecordingBar> m_RecordingBar;

	size_t cnt = 0;

	/* See ModbusLogPanel::m_LogCursor. */
	ModbusLogCursor m_LogCursor;

	/* The handler is handed in rather than fetched from wxGetApp() on every
	   use. docs/code-style.md: "A class gets its collaborators through its
	   constructor. It does not fetch them." */
	ModbusEntryHandler& m_handler;

	wxDECLARE_EVENT_TABLE();
};

class ModbusMasterPanel : public wxPanel
{
public:
	ModbusMasterPanel(wxWindow* parent, ModbusEntryHandler& handler, const wxSize& notebook_size);
	~ModbusMasterPanel();

	void UpdateSubpanels();
	void On10MsTimer();

	// !\brief Size this page and its notebook to the frame's new size.
	void OnFrameResized(const wxSize& size);

	wxAuiNotebook* m_notebook = nullptr;
	ModbusDataPanel* data_panel = nullptr;
	ModbusLogPanel* log_panel = nullptr;
	ModbusSpecialRegisterPanel* special_panel = nullptr;

private:
	void OnSize(wxSizeEvent& evt);
	void Changeing(wxAuiNotebookEvent& event);

	wxAuiManager m_mgr;

	/* The handler is handed in rather than fetched from wxGetApp() on every
	   use. docs/code-style.md: "A class gets its collaborators through its
	   constructor. It does not fetch them." */
	ModbusEntryHandler& m_handler;

	wxDECLARE_EVENT_TABLE();
};


