#pragma once

// The live value graph opened from the Modbus master panel.

#include <wx/wx.h>
#include <wx/checklst.h>
#include <deque>
#include <string>
#include <vector>

#include "ModbusHandler.hpp"

class ModbusGraphFrame;

struct ModbusGraphPoint { double seconds = 0.0; double value = 0.0; };

/* A watched register used to be held as a raw ModbusItem*, sampled with no
   lock and no check that it still existed - so switching device left the graph
   reading freed memory. A table plus an index survives a reload, and the
   handler reports when the index has gone. */
struct ModbusGraphSeries
{
	ModbusEntryHandler::Table table = ModbusEntryHandler::Table::Holding;
	size_t index = 0;
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
	bool AddItem(ModbusEntryHandler::Table table, size_t index, const wxString& name);
	void RemoveItem(ModbusEntryHandler::Table table, size_t index);

	// !\brief Take one sample per watched register. A register whose index no
	// longer exists is skipped rather than dereferenced.
	void RecordSamples();
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
