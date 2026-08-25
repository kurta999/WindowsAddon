#include "pch.hpp"

#include "GridBuilder.hpp"
#include "WxClipboard.hpp"

namespace gui
{
wxGrid* BuildGrid(wxWindow* parent, const GridSpec& spec)
{
    auto* grid = new wxGrid(parent, wxID_ANY, wxDefaultPosition, spec.size, 0);

    grid->CreateGrid(spec.initial_rows, static_cast<int>(spec.columns.size()));
    grid->EnableEditing(true);
    grid->EnableGridLines(true);
    grid->EnableDragGridSize(false);
    grid->SetMargins(0, 0);

    for(int column = 0; column < static_cast<int>(spec.columns.size()); ++column)
    {
        const GridColumn& description = spec.columns[static_cast<size_t>(column)];
        grid->SetColLabelValue(column, description.label);
        if(description.width >= 0)
            grid->SetColSize(column, description.width);
    }

    grid->EnableDragColMove(true);
    grid->EnableDragColSize(true);
    grid->SetColLabelAlignment(wxALIGN_CENTER, wxALIGN_CENTER);

    grid->SetSelectionMode(spec.selection_mode);

    grid->EnableDragRowSize(true);
    grid->SetRowLabelAlignment(wxALIGN_CENTER, wxALIGN_CENTER);

    if(spec.default_cell_alignment)
        grid->SetDefaultCellAlignment(wxALIGN_LEFT, wxALIGN_TOP);

    if(spec.hide_row_labels)
        grid->HideRowLabels();
    else if(spec.row_label_width >= 0)
        grid->SetRowLabelSize(spec.row_label_width);

    if(spec.double_buffered)
        grid->GetGridWindow()->SetDoubleBuffered(true);

    return grid;
}

void EnsureRow(wxGrid& grid, size_t row)
{
    if(static_cast<size_t>(grid.GetNumberRows()) <= row)
        grid.AppendRows(1);
}

int AppendRow(wxGrid& grid)
{
    grid.AppendRows(1);
    return grid.GetNumberRows() - 1;
}

bool CopySelectedRowsToClipboard(wxGrid& grid, int column_count)
{
    const wxArrayInt rows = grid.GetSelectedRows();
    if(rows.empty() || column_count <= 0)
        return false;

    wxString text;
    for(const int row : rows)
    {
        for(int col = 0; col != column_count; col++)
        {
            text += grid.GetCellValue(row, col);
            text += '\t';
        }
        text += '\n';
    }

    /* The rows are non-empty, so the string ends in the newline this drops.
       wxString::Last() on an empty string would be undefined. */
    text.RemoveLast();

    return CopyTextToClipboard(text);
}

namespace
{
    /* The two shades the grids alternate between. Odd rows are the grey one. */
    constexpr uint32_t kRowShadeOdd = 0xE6E6E6;
    constexpr uint32_t kRowShadeEven = 0xFFFFFF;
}

wxColour RowShade(int row)
{
    return wxColour((row & 1) ? kRowShadeOdd : kRowShadeEven);
}

void ApplyRowShading(wxGrid& grid, int row, int column_count)
{
    const wxColour shade = RowShade(row);
    for(int col = 0; col != column_count; col++)
        grid.SetCellBackgroundColour(row, col, shade);
}

void ApplyEntryStyle(wxGrid& grid, int row, int column_count, const EntryStyle& style)
{
    if(style.color)
    {
        for(int col = 0; col != column_count; col++)
            grid.SetCellTextColour(row, col, RGB_TO_WXCOLOR(*style.color));
    }

    /* The entry's own background where it has one, the alternating shading
       where it does not. */
    const wxColour background = style.bg_color ? RGB_TO_WXCOLOR(*style.bg_color) : RowShade(row);
    for(int col = 0; col != column_count; col++)
        grid.SetCellBackgroundColour(row, col, background);

    if(!style.is_bold && style.scale == 1.0f && style.font_face.empty())
        return;

    wxFont font;
    font.SetWeight(style.is_bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);

    /* Applied at the default scale first: wxFont::Scale multiplies the size it
       already has, so setting the cells twice is what keeps a re-styled row
       from compounding its own scale. */
    font.Scale(1.0f);
    for(int col = 0; col != column_count; col++)
        grid.SetCellFont(row, col, font);

    font.Scale(style.scale);
    if(!style.font_face.empty())
        font.SetFaceName(style.font_face);

    for(int col = 0; col != column_count; col++)
        grid.SetCellFont(row, col, font);
}
}
