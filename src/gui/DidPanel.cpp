#include "pch.hpp"
#include "utils/HexBytes.hpp"
#include "GridBuilder.hpp"
#include "MainFrameAccess.hpp"

wxBEGIN_EVENT_TABLE(DidPanel, wxPanel)
EVT_SIZE(DidPanel::OnSize)
EVT_GRID_CELL_CHANGED(DidPanel::OnCellValueChanged)
EVT_GRID_EDITOR_SHOWN(DidPanel::OnCellEditorShown)
EVT_CHAR_HOOK(DidPanel::OnKeyDown)
wxEND_EVENT_TABLE()

DidGrid::DidGrid(wxWindow* parent)
{
    /* In DidGridCol order. */
    static constexpr gui::GridColumn kColumns[]{
        { "DID", 50 },        // Did_ID
        { "Type", 100 },      // Did_Type
        { "Name", 200 },      // Did_Name
        { "Value", 250 },     // Did_Value
        { "Len" },            // Did_Len
        { "Min" },            // Did_MinVal
        { "Max" },            // Did_MaxVal
        { "Timestamp", 135 }, // Did_Timestamp
    };
    static_assert(std::size(kColumns) == DidGridCol::Did_Max);

    m_grid = gui::BuildGrid(parent, gui::GridSpec{
        .size = wxSize(1024, 600),
        .initial_rows = 1,
        .columns = kColumns,
        .selection_mode = wxGrid::wxGridSelectRows,
        .hide_row_labels = true,
    });
}

void DidGrid::AddRow(std::unique_ptr<DidEntry>& entry)
{
    gui::EnsureRow(*m_grid, cnt);

    m_grid->SetCellValue(wxGridCellCoords(cnt, DidGridCol::Did_ID), wxString::Format("%X", entry->id));
    m_grid->SetCellValue(wxGridCellCoords(cnt, DidGridCol::Did_Type), wxString::Format("%s", XmlDidLoader::GetStringFromType(entry->type)));
    m_grid->SetCellValue(wxGridCellCoords(cnt, DidGridCol::Did_Name), entry->name);
    m_grid->SetCellValue(wxGridCellCoords(cnt, DidGridCol::Did_Value), entry->value_str);
    m_grid->SetCellValue(wxGridCellCoords(cnt, DidGridCol::Did_Len), wxString::Format("%lld", entry->len));
    m_grid->SetCellValue(wxGridCellCoords(cnt, DidGridCol::Did_MinVal), wxString::Format("%s", entry->min));
    m_grid->SetCellValue(wxGridCellCoords(cnt, DidGridCol::Did_MaxVal), wxString::Format("%s", entry->max));

    wxString last_update_str;
    if(!entry->last_update.is_not_a_date_time())
    {
        last_update_str = boost::posix_time::to_iso_extended_string(entry->last_update);
    }

    if(!last_update_str.empty())
    {
        if(entry->nrc != 0)  /* TODO: create a function for this, because it's a duplicate */
        {
            switch(entry->nrc)
            {
                case 0x78:
                {
                    m_grid->SetCellValue(wxGridCellCoords(cnt, DidGridCol::Did_Value), "Pending... NRC 78");
                    m_grid->SetCellBackgroundColour(cnt, DidGridCol::Did_Value, *wxBLUE);
                    break;
                }
                default:
                {
                    m_grid->SetCellValue(wxGridCellCoords(cnt, DidGridCol::Did_Value), wxString::Format("NRC %X", entry->nrc));
                    m_grid->SetCellBackgroundColour(cnt, DidGridCol::Did_Value, *wxRED);

                    wxFont cell_font = m_grid->GetCellFont(cnt, Did_Name);
                    cell_font.SetWeight(wxFONTWEIGHT_NORMAL);
                    m_grid->SetCellFont(cnt, Did_Name, cell_font);
                    break;
                }
            }
        }
        else
        {
            m_grid->SetCellValue(wxGridCellCoords(cnt, DidGridCol::Did_Value), entry->value_str);
            wxFont cell_font = m_grid->GetCellFont(cnt, Did_Name);
            cell_font.SetWeight(wxFONTWEIGHT_BOLD);
            m_grid->SetCellFont(cnt, Did_Name, cell_font);

            gui::ApplyRowShading(*m_grid, static_cast<int>(cnt), DidGridCol::Did_Max);
        }
    }
    m_grid->SetCellValue(wxGridCellCoords(cnt, DidGridCol::Did_Timestamp), last_update_str);
    m_grid->SetReadOnly(cnt, DidGridCol::Did_Timestamp);
    
    grid_to_entry[cnt] = entry.get();
    did_to_row[entry->id] = cnt;
    cnt++;
}

DidPanel::DidPanel(wxFrame* parent, CanEntryHandler& can_handler, DidHandler& did_handler)
    : wxPanel(parent, wxID_ANY), m_canHandler(can_handler), m_didHandler(did_handler)
{
    wxBoxSizer* bSizer1 = new wxBoxSizer(wxVERTICAL);

    static_box_grid = new wxStaticBoxSizer(wxHORIZONTAL, this, "&DID Managment");
    static_box_grid->GetStaticBox()->SetFont(static_box_grid->GetStaticBox()->GetFont().Bold());
    static_box_grid->GetStaticBox()->SetForegroundColour(*wxRED);

    did_grid = new DidGrid(this);
    
    wxBoxSizer* h_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_RefreshSelected = new wxButton(this, wxID_ANY, "Refresh selected", wxDefaultPosition, wxDefaultSize);
    m_RefreshSelected->SetToolTip("Start refreshing process for selected DIDs)");
    m_RefreshSelected->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
        {
            wxGrid* m_grid = did_grid->m_grid;

            wxArrayInt rows = m_grid->GetSelectedRows();
            if(rows.empty()) return;

            for(auto& i : rows)
            {
                const std::optional<uint16_t> did = utils::TryParse<uint16_t>(
                    did_grid->m_grid->GetCellValue(wxGridCellCoords(i, DidGridCol::Did_ID)).ToStdString(),
                    utils::ParseMode::Whole, 16);
                if(!did)
                    continue;

                /* AddDidToReadQueue takes the handler's mutex itself. Taking it
                   here as well deadlocked on the first selected row, because
                   that mutex is not recursive. */
                m_didHandler.AddDidToReadQueue(*did);
            }
            m_didHandler.NotifyDidUpdate();
        });
    h_sizer->Add(m_RefreshSelected);

    m_Abort = new wxButton(this, wxID_ANY, "Abort", wxDefaultPosition, wxDefaultSize);
    m_Abort->SetToolTip("Abort DID update)");
    m_Abort->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
        {
            wxGrid* m_grid = did_grid->m_grid;

            m_didHandler.AbortDidUpdate();
        });
    h_sizer->Add(m_Abort);

    m_ClearDids = new wxButton(this, wxID_ANY, "Clear", wxDefaultPosition, wxDefaultSize);
    m_ClearDids->SetToolTip("Clear refreshed DID values)");
    m_ClearDids->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
        {
            int num_rows = did_grid->m_grid->GetNumberRows();
            for(int i = 0; i != num_rows; i++)
            {
                //wxFont cell_font = did_grid->m_grid->GetCellFont(i, Did_Name);
                wxFont cell_font;
                cell_font.SetWeight(wxFONTWEIGHT_NORMAL);
                did_grid->m_grid->SetCellFont(i, Did_Name, cell_font);

                did_grid->m_grid->SetCellValue(i, Did_Value, "");
                did_grid->m_grid->SetCellBackgroundColour(i, DidGridCol::Did_Value, wxNullColour);

                gui::ApplyRowShading(*did_grid->m_grid, static_cast<int>(i), DidGridCol::Did_Max);
            }
        });
    h_sizer->Add(m_ClearDids);

    wxBoxSizer* h_sizer_2 = new wxBoxSizer(wxHORIZONTAL);
    m_SaveCache = new wxButton(this, wxID_ANY, "Save cache", wxDefaultPosition, wxDefaultSize);
    m_SaveCache->SetToolTip("Save cached values for DIDs)");
    m_SaveCache->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
        {
            std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
            
            bool ret = m_didHandler.SaveCache();
            if(ret)
            {
                std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
                int64_t dif = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();

                char work_dir[1024] = {};
#ifdef _WIN32
                GetCurrentDirectoryA(sizeof(work_dir) - 1, work_dir);
#endif
                PostAppNotification(FileSavedNotification{SavedFileKind::DidCache, dif,
                    std::string(work_dir) + "\\" + DID_CACHE_FILENAME});
            }
        });
    h_sizer_2->Add(m_SaveCache);

    static_box_grid->Add(did_grid->m_grid, 0, wxALL, 5);
    bSizer1->Add(static_box_grid, wxSizerFlags(0).Top());
    bSizer1->Add(h_sizer);
    bSizer1->Add(h_sizer_2);

    SetSizer(bSizer1);
    Layout();
}

void DidPanel::UpdateDidList()
{
    if(did_grid->m_grid->GetNumberRows())
        did_grid->m_grid->DeleteRows(0, did_grid->m_grid->GetNumberRows());
    did_grid->cnt = 0;

    /* Rebuilt on user action, not per tick, so the worker is held off for the
       rebuild rather than the list being read while it writes. */
    m_didHandler.WithModel([this](DidHandler::Model& model)
    {
        model.updated_dids.clear();
        for(auto& i : model.did_list)
        {
            bool add_row = false;
            if(search_pattern.empty())
                add_row = true;
            else
            {
                if(boost::icontains(i.second->name, search_pattern))
                    add_row = true;
            }

            if(add_row)
            {
                did_grid->AddRow(i.second);
                model.updated_dids.push_back(i.first);
            }
        }
    });
}

void DidPanel::On100msTimer()
{

    if(!is_dids_initialized)
    {
        /* Runs once, so the worker is held off for the initial fill rather
           than the entries being read while it writes them. */
        m_didHandler.WithModel([this](DidHandler::Model& model)
        {
            for(auto& i : model.did_list)
            {
                did_grid->AddRow(i.second);
            }
        });
        is_dids_initialized = true;
    }

    /* The UDS worker fills the update list and the entries it points at, so what
       is rendered is copied out under the handler's lock first. Rendering used
       to read the live entries with the lock commented out, and holding it
       across this many wxGrid calls would stall the worker instead. */
    struct DidUpdate
    {
        uint16_t row;
        uint16_t nrc;
        std::string value_str;
        boost::posix_time::ptime last_update;
    };
    const std::vector<DidUpdate> updates = m_didHandler.WithModel(
        [this](DidHandler::Model& model)
    {
        std::vector<DidUpdate> collected;
        collected.reserve(model.updated_dids.size());
        for(uint16_t did : model.updated_dids)
        {
            /* find rather than operator[]: the latter inserts a null entry for
               a DID that is not in the list, and a row 0 for one that has no
               grid row yet. */
            const auto entry = model.did_list.find(did);
            const auto row = did_grid->did_to_row.find(did);
            if(entry == model.did_list.end() || !entry->second || row == did_grid->did_to_row.end())
                continue;

            collected.push_back({ row->second, entry->second->nrc, entry->second->value_str,
                entry->second->last_update });
        }
        model.updated_dids.clear();
        return collected;
    });

    for(const DidUpdate& update : updates)
    {
        const uint16_t did_row = update.row;
        if(update.nrc != 0)
        {
            switch(update.nrc)
            {
                case 0x78:
                {
                    did_grid->m_grid->SetCellValue(wxGridCellCoords(did_row, DidGridCol::Did_Value), "Pending... NRC 78");
                    did_grid->m_grid->SetCellBackgroundColour(did_row, DidGridCol::Did_Value, *wxBLUE);
                    break;
                }
                default:
                {
                    did_grid->m_grid->SetCellValue(wxGridCellCoords(did_row, DidGridCol::Did_Value), wxString::Format("NRC %X", update.nrc));
                    did_grid->m_grid->SetCellBackgroundColour(did_row, DidGridCol::Did_Value, *wxRED);

                    wxFont cell_font = did_grid->m_grid->GetCellFont(did_row, Did_Name);
                    cell_font.SetWeight(wxFONTWEIGHT_NORMAL);
                    did_grid->m_grid->SetCellFont(did_row, Did_Name, cell_font);
                    break;
                }
            }
        }
        else
        {
            did_grid->m_grid->SetCellValue(wxGridCellCoords(did_row, DidGridCol::Did_Value), update.value_str);
            wxFont cell_font = did_grid->m_grid->GetCellFont(did_row, Did_Name);
            cell_font.SetWeight(wxFONTWEIGHT_BOLD);
            did_grid->m_grid->SetCellFont(did_row, Did_Name, cell_font);
            did_grid->m_grid->SetCellBackgroundColour(did_row, DidGridCol::Did_Value, gui::RowShade(did_row));
        }

        if(!update.last_update.is_not_a_date_time())
        {
            wxString last_update_str = boost::posix_time::to_iso_extended_string(update.last_update);
            did_grid->m_grid->SetCellValue(wxGridCellCoords(did_row, DidGridCol::Did_Timestamp), last_update_str);
        }
    }
}

void DidPanel::WriteDid(uint16_t did, uint8_t* data_to_write, uint16_t size)
{
    m_didHandler.WriteDid(did, data_to_write, size);
    m_didHandler.NotifyDidUpdate();

    PostAppNotification(SimpleNotification{SimpleNotificationKind::DidUpdated});

}
void DidPanel::OnSize(wxSizeEvent& evt)
{
    evt.Skip(true);
}

void DidPanel::OnCellValueChanged(wxGridEvent& ev)
{
    int row = ev.GetRow(), col = ev.GetCol();
    if(ev.GetEventObject() == static_cast<wxObject*>(did_grid->m_grid))
    {

        const wxString did_str = did_grid->m_grid->GetCellValue(row, DidGridCol::Did_ID);
        const std::optional<uint32_t> did_id = utils::TryParse<uint32_t>(
            did_str.ToStdString(), utils::ParseMode::Whole, 16);
        if(!did_id)
            return;

        /* The three fields the edit needs, read under the lock. This was the
           one model read in this panel that took no lock at all, while the UDS
           worker writes the same entry's value, nrc and timestamp. WriteDid
           below takes that same lock, so it cannot be called from inside
           WithModel - hence a snapshot rather than a reference.

           find rather than operator[]: the latter inserts a null unique_ptr for
           a DID that is not in the list and then dereferences it. */
        struct DidTarget
        {
            uint16_t id;
            DidEntryType type;
            size_t len;
        };

        const std::optional<DidTarget> did_it = m_didHandler.WithModel(
            [did_id](DidHandler::Model& model) -> std::optional<DidTarget>
        {
            const auto entry = model.did_list.find(*did_id);
            if(entry == model.did_list.end() || !entry->second)
                return std::nullopt;
            return DidTarget{ entry->second->id, entry->second->type, entry->second->len };
        });
        if(!did_it)
            return;

        /* Not const: the DET_STRING and DET_BYTEARRAY branches pad and trim it
           to the DID's declared length in place. */
        std::string hex_str = did_grid->m_grid->GetCellValue(row, DidGridCol::Did_Value).ToStdString();
        uint64_t hex_val = 0;
        bool is_ok = true;
        if(did_types::IsInteger(did_it->type))
        {
            /* std::stoi also capped this at int width, so a 64-bit DID value
               threw out_of_range instead of being written. */
            const std::optional<uint64_t> parsed = utils::TryParse<uint64_t>(
                hex_str, utils::ParseMode::Whole, 16);
            if(parsed)
                hex_val = *parsed;
            else
            {
                LOG(LogLevel::Error, "Rejecting DID value '{}': not a hexadecimal number", hex_str);
                is_ok = false;
            }
        }

        if(is_ok)
        {
            switch(did_it->type)
            {
                case DET_UI8:
                {
                    uint8_t val_to_write = static_cast<uint8_t>(hex_val);
                    WriteDid(did_it->id, &val_to_write, sizeof(val_to_write));
                    break;
                }
                case DET_UI16:
                {
                    /* This narrowed to uint8_t before widening back out, so
                       every 16-bit DID was written with its high byte zeroed. */
                    uint16_t val_to_write = static_cast<uint16_t>(hex_val);
                    WriteDid(did_it->id, (uint8_t*)&val_to_write, sizeof(val_to_write));
                    break;
                }
                case DET_UI32:
                {
                    /* Same truncation through uint8_t as DET_UI16 above. */
                    uint32_t val_to_write = static_cast<uint32_t>(hex_val);
                    WriteDid(did_it->id, (uint8_t*)&val_to_write, sizeof(val_to_write));
                    break;
                }
                case DET_STRING:
                {
                    hex_str = did::FitToDeclaredLength(std::move(hex_str), did_it->len);
                    uint8_t* byte_array = (uint8_t*)const_cast<const char*>(hex_str.c_str());

                    WriteDid(did_it->id, byte_array, hex_str.length());
                    break;
                }
                case DET_BYTEARRAY:
                {

                    hex_str = utils::StripHexSeparators(hex_str);
                    if(hex_str.empty())
                    {
                        LOG(LogLevel::Warning, "Skipping DID {:X}: input length is zero", did_it->id);
                        break;
                    }

                    hex_str = did::FitToDeclaredLength(std::move(hex_str), did_it->len);

                    auto bytes = utils::ParseHexBytes(hex_str, MAX_ISOTP_FRAME_LEN);
                    if(!bytes)
                    {
                        LOG(LogLevel::Error, "Skipping DID {:X}: '{}' is not valid hex", did_it->id, hex_str);
                        break;
                    }

                    WriteDid(did_it->id, bytes->data(), static_cast<uint16_t>(bytes->size()));
                    break;
                }
            }
        }
        /* The "Invalid DID" branch that used to sit here covered two different
           failures - a DID missing from the list and a value that would not
           parse - and reported both as the first. Each is now rejected where it
           is detected, with a message that says which one happened. */
    }
}

void DidPanel::OnCellEditorShown(wxGridEvent& ev)
{
    // Editing is handled by the explicit DID edit action. Allow the grid's
    // normal event processing without retaining an unreachable alternate UI.
    ev.Skip();
}

void DidPanel::OnKeyDown(wxKeyEvent& evt)
{
    if(evt.ControlDown())
    {
        switch(evt.GetKeyCode())
        {
            case 'F':
            {
                wxTextEntryDialog d(this, "Enter DID name for what you want to filter", "Search for DID name");
                int ret = d.ShowModal();
                if(ret == wxID_OK)
                {
                    search_pattern = d.GetValue().ToStdString();
                    if(search_pattern.empty())
                        static_box_grid->GetStaticBox()->SetLabelText("DID Management");
                    else
                        static_box_grid->GetStaticBox()->SetLabelText(wxString::Format("DID Management - Search filter: %s", search_pattern));

                    UpdateDidList();
                }
                return;
            }
        }
    }
    evt.Skip();
}
