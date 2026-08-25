#include "pch.hpp"

#include "ModbusGraphView.hpp"
#include <wx/dcbuffer.h>
#include <cmath>

namespace
{
// !\brief The palette a newly added series takes its colour from.
wxColour GraphColor(size_t index)
{
    static const std::array colors = { wxColour(30,117,219), wxColour(219,88,30), wxColour(28,143,75),
        wxColour(173,61,184), wxColour(201,151,27), wxColour(24,156,166), wxColour(190,45,80),
        wxColour(81,93,191), wxColour(95,126,42), wxColour(94,94,94) };
    return colors[index % colors.size()];
}
}
#include "ModbusMasterPanel.hpp"

ModbusGraphCanvas::ModbusGraphCanvas(ModbusGraphFrame* parent) : wxPanel(parent), m_owner(parent)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &ModbusGraphCanvas::OnPaint, this);
}

void ModbusGraphCanvas::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(*wxWHITE_BRUSH);
    dc.Clear();
    const auto series = m_owner->GetSeriesSnapshot();
    if(series.empty())
        return;
    const wxRect area = GetClientRect().Deflate(45, 25);
    dc.SetPen(*wxLIGHT_GREY_PEN);
    dc.DrawRectangle(area);
    double min_value = std::numeric_limits<double>::max();
    double max_value = std::numeric_limits<double>::lowest();
    double max_time = 1.0;
    for(const auto& item : series)
        for(const auto& point : item.points)
        {
            min_value = std::min(min_value, point.value);
            max_value = std::max(max_value, point.value);
            max_time = std::max(max_time, point.seconds);
        }
    if(min_value == std::numeric_limits<double>::max())
        return;
    if(std::abs(max_value - min_value) < 0.000001)
    {
        min_value -= 1.0;
        max_value += 1.0;
    }
    for(const auto& item : series)
    {
        dc.SetPen(wxPen(item.color, 2));
        wxPoint previous;
        bool have_previous = false;
        for(const auto& point : item.points)
        {
            const int x = area.x + static_cast<int>((point.seconds / max_time) * area.width);
            const int y = area.GetBottom() - static_cast<int>(((point.value - min_value) / (max_value - min_value)) * area.height);
            const wxPoint current(x, y);
            if(have_previous)
                dc.DrawLine(previous, current);
            previous = current;
            have_previous = true;
        }
    }
    dc.SetTextForeground(*wxBLACK);
    dc.DrawText(wxString::Format("%.3g", max_value), 2, area.y);
    dc.DrawText(wxString::Format("%.3g", min_value), 2, area.GetBottom() - 15);
}

ModbusGraphFrame::ModbusGraphFrame(wxWindow* parent)
    : wxFrame(parent, wxID_ANY, "Modbus live graph", wxDefaultPosition, wxSize(900, 500)),
      m_startTime(std::chrono::steady_clock::now())
{
    auto* root = new wxBoxSizer(wxHORIZONTAL);
    auto* side = new wxBoxSizer(wxVERTICAL);
    m_items = new wxListBox(this, wxID_ANY);
    m_removeButton = new wxButton(this, wxID_ANY, "Remove");
    m_removeButton->Bind(wxEVT_BUTTON, &ModbusGraphFrame::OnRemoveSelected, this);
    side->Add(m_items, 1, wxEXPAND | wxALL, 5);
    side->Add(m_removeButton, 0, wxEXPAND | wxALL, 5);
    root->Add(side, 0, wxEXPAND);
    m_canvas = new ModbusGraphCanvas(this);
    root->Add(m_canvas, 1, wxEXPAND | wxALL, 5);
    SetSizer(root);
    Bind(wxEVT_CLOSE_WINDOW, &ModbusGraphFrame::OnClose, this);
}

bool ModbusGraphFrame::AddItem(ModbusEntryHandler::Table table, size_t index, const wxString& name)
{
    std::scoped_lock lock(m_seriesMutex);
    const bool already_watched = std::ranges::any_of(m_series,
        [table, index](const auto& s) { return s.table == table && s.index == index; });
    if(m_series.size() >= MaxItems || already_watched)
        return false;
    m_series.push_back({ table, index, name, GraphColor(m_series.size()), {} });
    RefreshWatchedItems();
    return true;
}

void ModbusGraphFrame::RemoveItem(ModbusEntryHandler::Table table, size_t index)
{
    std::scoped_lock lock(m_seriesMutex);
    std::erase_if(m_series,
        [table, index](const auto& series) { return series.table == table && series.index == index; });
    RefreshWatchedItems();
}

void ModbusGraphFrame::RecordSamples()
{
    std::unique_ptr<ModbusEntryHandler>& modbus_handler = wxGetApp().modbus_handler;
    if(!modbus_handler)
        return;

    std::scoped_lock lock(m_seriesMutex);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_startTime).count();
    for(auto& series : m_series)
    {
        /* The previous version took the table as a parameter, ignored it, and
           dereferenced a stored ModbusItem* that a device change could already
           have freed. Asking the handler both takes its lock and reports a
           register that is no longer there. */
        const auto value = modbus_handler->SampleValue(series.table, series.index);
        if(!value)
            continue;

        series.points.push_back({ seconds, *value });
        if(series.points.size() > MaxSamplesPerItem)
            series.points.pop_front();
    }
    m_canvas->Refresh(false);
}

std::vector<ModbusGraphSeries> ModbusGraphFrame::GetSeriesSnapshot() const
{
    std::scoped_lock lock(m_seriesMutex);
    return m_series;
}

void ModbusGraphFrame::RefreshWatchedItems()
{
    m_items->Clear();
    for(const auto& series : m_series)
        m_items->Append(series.name);
}

void ModbusGraphFrame::OnRemoveSelected(wxCommandEvent&)
{
    const int selected = m_items->GetSelection();
    if(selected == wxNOT_FOUND)
        return;
    std::scoped_lock lock(m_seriesMutex);
    m_series.erase(m_series.begin() + selected);
    RefreshWatchedItems();
}

void ModbusGraphFrame::OnClose(wxCloseEvent& event)
{
    Hide();
    event.Veto();
}
