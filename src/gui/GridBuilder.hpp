#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include <wx/grid.h>

// !\brief Declarative construction of the wxGrid controls this application uses.
//
// Eight grid constructors carried the same twenty lines - EnableEditing,
// EnableGridLines, EnableDragGridSize, SetMargins, the two drag-column calls,
// both label alignments - copied between them along with the section comments
// and, in six of the eight, an empty "// Label Appearance" heading with nothing
// underneath it. Only the columns and a handful of flags ever differed, so a
// grid now says what it *is* and this says how one is built.
namespace gui
{
// !\brief One column: its heading, and its width when the default is wrong.
struct GridColumn
{
    const char* label = "";

    // !\brief Column width in pixels; negative means leave wxGrid's default.
    int width = -1;
};

// !\brief Everything that actually differed between the eight call sites.
//
// The defaults are the majority behaviour, so a spec only states what is
// unusual about its grid.
struct GridSpec
{
    wxSize size{ 800, 600 };
    int initial_rows = 1;

    // !\brief Column headings, in column-index order. The index into this span
    // is the column number, which is what the enums in the panels count.
    std::span<const GridColumn> columns;

    wxGrid::wxGridSelectionModes selection_mode = wxGrid::wxGridSelectRows;

    // !\brief Whether to pin cell contents to the top left.
    //
    // Six of the eight grids ask for this and two do not. Keeping it explicit
    // preserves that difference rather than quietly unifying grids whose
    // appearance nobody has re-checked.
    bool default_cell_alignment = true;

    // !\brief Hide the row-label gutter entirely.
    bool hide_row_labels = false;

    // !\brief Row-label gutter width; negative means leave wxGrid's default.
    // Ignored when hide_row_labels is set.
    int row_label_width = -1;

    // !\brief Double-buffer the grid window. Worth it for grids that redraw on
    // a timer; pointless for the rest.
    bool double_buffered = false;
};

// !\brief Create a grid on `parent` and apply `spec`.
[[nodiscard]] wxGrid* BuildGrid(wxWindow* parent, const GridSpec& spec);

// !\brief The per-entry presentation a configuration file may carry.
//
// Both the CAN sender grid and the Modbus register grids read these five
// fields off their entries, having loaded them through the same
// utils::xml::ReadOptionalTextStyle.
struct EntryStyle
{
    std::optional<uint32_t> color;
    std::optional<uint32_t> bg_color;
    bool is_bold = false;
    float scale = 1.0f;
    std::string font_face;
};

// !\brief Read the style fields off any entry that carries them.
template <class Entry>
[[nodiscard]] EntryStyle StyleOf(const Entry& e)
{
    return { e.m_color, e.m_bg_color, e.m_is_bold, e.m_scale, e.m_font_face };
}

// !\brief Paint one row: the entry's own colours where it has them, the
// alternating row shading where it does not, and its font where it asks for one.
//
// This was the same thirty lines in CanGrids and ModbusDataPanel, differing
// only in the column-count constant and the name of the row variable - the two
// section comments were copied across as well, typo included. Three further
// grids open-coded just the shading half, and had drifted: one inverted the
// stripe phase and one respelled the same grey as wxColor(230, 230, 230).
//
// !\param column_count How many leading columns the row occupies.
void ApplyEntryStyle(wxGrid& grid, int row, int column_count, const EntryStyle& style);

// !\brief The alternating row shading on its own, for rows with no entry
// behind them to carry a style.
void ApplyRowShading(wxGrid& grid, int row, int column_count);

// !\brief The shade a row alternates to, for the cells that pick their own
// background and only want the default when they have no colour of their own.
[[nodiscard]] wxColour RowShade(int row);

// !\brief Copies the grid's selected rows to the clipboard as tab-separated
// text, one line per row.
//
// !\param column_count How many leading columns to copy. The CAN log grid
//        excludes its last column, so this is not always the grid's width.
// !\return false when nothing was selected or the clipboard refused to open,
//         so the caller can decide whether to tell the user.
//
// The Modbus log, time tracker and CAN log panels each carried this as thirty
// identical lines inside their own Ctrl+C handler, down to the trailing-newline
// trim and the clipboard open/close pair.
bool CopySelectedRowsToClipboard(wxGrid& grid, int column_count);

// !\brief Makes sure `row` exists, appending one row if it does not.
//
// Eight grids are filled by a counter walking down them, growing by a row when
// it reaches the end and writing over what is already there when it has not.
// Each spelled the same three lines, and each compared the grid's int row count
// against a size_t counter to do it.
void EnsureRow(wxGrid& grid, size_t row);

// !\brief Appends a row and returns its index.
//
// The other half of the same idiom: two grids append first and then work out
// where the new row landed.
[[nodiscard]] int AppendRow(wxGrid& grid);
}
