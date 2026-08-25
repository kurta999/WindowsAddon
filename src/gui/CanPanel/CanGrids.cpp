#include "pch.hpp"
#include "../MainFrameAccess.hpp"
#include "../GridBuilder.hpp"
#include "../MenuCommand.hpp"

// Declared in CanSenderPanel.hpp, next to the grids whose ID column it reads.
[[nodiscard]] std::optional<uint32_t> FrameIdAt(wxGrid* grid, int row)
{
    if(grid == nullptr || row < 0 || row >= grid->GetNumberRows())
        return std::nullopt;

    std::string text = grid->GetCellValue(row, CanSenderGridCol::Sender_Id).ToStdString();
    boost::algorithm::trim(text);

    std::string_view digits{ text };
    if(digits.starts_with("0x") || digits.starts_with("0X"))
        digits.remove_prefix(2);

    return utils::TryParse<uint32_t>(digits, utils::ParseMode::Whole, 16);
}

CanGrid::CanGrid(wxWindow* parent)
{
    /* In CanSenderGridCol order; the static_assert below catches a column
       added to the enum without a heading here. */
    static constexpr gui::GridColumn kColumns[]{
        { "ID" },            // Sender_Id
        { "Size" },          // Sender_DataSize
        { "Data", 200 },     // Sender_Data
        { "Period" },        // Sender_Period
        { "Count" },         // Sender_Count
        { "Log", 35 },       // Sender_LogLevel
        { "Fav", 35 },       // Sender_FavouriteLevel
        { "Comment", 160 },  // Sender_Comment
    };
    static_assert(std::size(kColumns) == CanSenderGridCol::Sender_Max);

    m_grid = gui::BuildGrid(parent, gui::GridSpec{
        .size = wxSize(800, 250),
        .initial_rows = 1,
        .columns = kColumns,
        .selection_mode = wxGrid::wxGridSelectRows,
        .hide_row_labels = true,
    });

    m_grid->GetGridWindow()->Bind(wxEVT_MIDDLE_DOWN, [this](wxMouseEvent& event)
        {
            DBG("middle down\n");
        });
}

void CanGrid::AddRow(wxString id, wxString dlc, wxString data, wxString period, wxString count, wxString loglevel, wxString comment)
{
    gui::EnsureRow(*m_grid, cnt);

    m_grid->SetCellValue(wxGridCellCoords(cnt, CanSenderGridCol::Sender_Id), id);
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanSenderGridCol::Sender_DataSize), dlc);
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanSenderGridCol::Sender_Data), data);
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanSenderGridCol::Sender_Period), period);
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanSenderGridCol::Sender_Count), count);
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanSenderGridCol::Sender_LogLevel), loglevel);
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanSenderGridCol::Sender_Comment), comment);

    m_grid->SetCellEditor(cnt, CanSenderGridCol::Sender_DataSize, new wxGridCellNumberEditor);
    m_grid->SetCellEditor(cnt, CanSenderGridCol::Sender_Period, new wxGridCellNumberEditor);
    m_grid->SetCellEditor(cnt, CanSenderGridCol::Sender_LogLevel, new wxGridCellNumberEditor);
    m_grid->SetCellEditor(cnt, CanSenderGridCol::Sender_FavouriteLevel, new wxGridCellNumberEditor);

    cnt++;
}

void CanGrid::AddRow(std::unique_ptr<CanTxEntry>& e)
{
    gui::EnsureRow(*m_grid, cnt);

    m_grid->SetCellValue(wxGridCellCoords(cnt, CanSenderGridCol::Sender_Id), wxString::Format("%X", e->id));
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanSenderGridCol::Sender_DataSize), wxString::Format("%lld", e->data.size()));

    std::string hex;
    utils::ConvertHexBufferToString(e->data, hex);
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanSenderGridCol::Sender_Data), hex);

    m_grid->SetCellValue(wxGridCellCoords(cnt, CanSenderGridCol::Sender_Period), wxString::Format("%d", e->period));
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanSenderGridCol::Sender_Count), "0");
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanSenderGridCol::Sender_LogLevel), wxString::Format("%d", e->log_level));
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanSenderGridCol::Sender_FavouriteLevel), wxString::Format("%d", e->favourite_level));
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanSenderGridCol::Sender_Comment), e->comment);

    m_grid->SetCellEditor(cnt, CanSenderGridCol::Sender_DataSize, new wxGridCellNumberEditor);
    m_grid->SetCellEditor(cnt, CanSenderGridCol::Sender_Period, new wxGridCellNumberEditor);
    m_grid->SetCellEditor(cnt, CanSenderGridCol::Sender_LogLevel, new wxGridCellNumberEditor);
    m_grid->SetCellEditor(cnt, CanSenderGridCol::Sender_FavouriteLevel, new wxGridCellNumberEditor);

    m_grid->SetReadOnly(cnt, CanSenderGridCol::Sender_Count, true);

    gui::ApplyEntryStyle(*m_grid, static_cast<int>(cnt), CanSenderGridCol::Sender_Max, gui::StyleOf(*e));

    grid_to_entry[cnt] = e.get();
    cnt++;
}

void CanGrid::RemoveLastRow()
{
    int num_rows = m_grid->GetNumberRows();
    if(num_rows < 1)
        return;
    m_grid->DeleteRows(m_grid->GetNumberRows() - 1, 1);
    cnt--;
    grid_to_entry.erase(cnt);
}

void CanGrid::UpdateTxCounter(uint32_t frame_id, size_t count)
{
    for(auto& i : grid_to_entry)
    {
        if(i.second->id == frame_id)
        {
            int max_rows = m_grid->GetNumberRows();
            if(i.first < max_rows)
                m_grid->SetCellValue(wxGridCellCoords(i.first, CanSenderGridCol::Sender_Count), wxString::Format("%lld", count));
            else
                DBG("invalid column");
        }
    }
}

CanGridRx::CanGridRx(wxWindow* parent)
{
    /* In CanSenderGridCol order. The RX grid shows the same columns as TX but
       sizes three of them differently. */
    static constexpr gui::GridColumn kColumns[]{
        { "ID" },            // Sender_Id
        { "Size" },          // Sender_DataSize
        { "Data", 200 },     // Sender_Data
        { "Period" },        // Sender_Period
        { "Count" },         // Sender_Count
        { "Log", 30 },       // Sender_LogLevel
        { "Fav", 30 },       // Sender_FavouriteLevel
        { "Comment", 160 },  // Sender_Comment
    };
    static_assert(std::size(kColumns) == CanSenderGridCol::Sender_Max);

    m_grid = gui::BuildGrid(parent, gui::GridSpec{
        .size = wxSize(800, 250),
        .initial_rows = 1,
        .columns = kColumns,
        .selection_mode = wxGrid::wxGridSelectRows,
        .hide_row_labels = true,
    });

    m_grid->SetCellEditor(cnt, CanSenderGridCol::Sender_LogLevel, new wxGridCellNumberEditor);
    m_grid->SetCellEditor(cnt, CanSenderGridCol::Sender_FavouriteLevel, new wxGridCellNumberEditor);
}

void CanGridRx::AddRow(const RxRow& row)
{
    const int num_row = gui::AppendRow(*m_grid);
    m_grid->SetCellValue(wxGridCellCoords(num_row, CanSenderGridCol::Sender_Period), "0");
    m_grid->SetCellValue(wxGridCellCoords(num_row, CanSenderGridCol::Sender_Count), "1");
    rx_grid_to_entry[static_cast<uint16_t>(num_row)] = row.frame_id;

    gui::ApplyRowShading(*m_grid, static_cast<int>(num_row), CanSenderGridCol::Sender_Max);

    m_grid->SetReadOnly(num_row, CanSenderGridCol::Sender_Id);
    m_grid->SetReadOnly(num_row, CanSenderGridCol::Sender_DataSize);
    m_grid->SetReadOnly(num_row, CanSenderGridCol::Sender_Data);
    m_grid->SetReadOnly(num_row, CanSenderGridCol::Sender_Period);
    m_grid->SetReadOnly(num_row, CanSenderGridCol::Sender_Count);
}

void CanGridRx::UpdateRow(int num_row, const RxRow& row)
{
    m_grid->SetCellValue(wxGridCellCoords(num_row, CanSenderGridCol::Sender_Id), wxString::Format("%X", row.frame_id));
    m_grid->SetCellValue(wxGridCellCoords(num_row, CanSenderGridCol::Sender_DataSize), wxString::Format("%lld", row.data.size()));

    std::string hex;
    utils::ConvertHexBufferToString(row.data, hex);
    m_grid->SetCellValue(wxGridCellCoords(num_row, CanSenderGridCol::Sender_Data), hex);
    m_grid->SetCellValue(wxGridCellCoords(num_row, CanSenderGridCol::Sender_Period), wxString::Format("%d", row.period));
    m_grid->SetCellValue(wxGridCellCoords(num_row, CanSenderGridCol::Sender_Count), wxString::Format("%lld", row.count));
    m_grid->SetCellValue(wxGridCellCoords(num_row, CanSenderGridCol::Sender_LogLevel), wxString::Format("%d", row.log_level));
    m_grid->SetCellValue(wxGridCellCoords(num_row, CanSenderGridCol::Sender_FavouriteLevel), wxString::Format("%d", row.favourite_level));
    m_grid->SetCellValue(wxGridCellCoords(num_row, CanSenderGridCol::Sender_Comment), row.comment);
}

void CanGridRx::ClearGrid()
{
    rx_grid_to_entry.clear();  /* Clear entrie RX grid */
    cnt = 0;
    if(m_grid->GetNumberRows())
        m_grid->DeleteRows(0, m_grid->GetNumberRows());
}
