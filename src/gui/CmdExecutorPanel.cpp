#include "pch.hpp"
#include "MenuCommand.hpp"
#include "Prompts.hpp"

namespace
{
wxSize ToWxSize(LogicalSize size)
{
    return size.IsDefault() ? wxDefaultSize : wxSize(size.width, size.height);
}

LogicalSize ToLogicalSize(const wxSize& size)
{
    return {size.x, size.y};
}
}
#include <wx/bmpcbox.h>

wxBEGIN_EVENT_TABLE(CmdExecutorPanelBase, wxPanel)
EVT_SIZE(CmdExecutorPanelBase::OnSize)
EVT_MIDDLE_DOWN(CmdExecutorPanelBase::OnMiddleClick)
wxEND_EVENT_TABLE()

wxBEGIN_EVENT_TABLE(CmdExecutorPanelPage, wxPanel)
EVT_SIZE(CmdExecutorPanelPage::OnSize)
//EVT_PAINT(CmdExecutorPanelPage::OnPaint)
wxEND_EVENT_TABLE()

constexpr size_t MAX_CMD_LEN_FOR_BUTTON = 16;

CmdExecutorEditDialog::CmdExecutorEditDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, "Command editor", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxSizer* const sizerTop = new wxBoxSizer(wxVERTICAL);

    wxSizer* const sizerMsgs = new wxStaticBoxSizer(wxVERTICAL, this, "&Command properties");
    {
        sizerMsgs->Add(new wxStaticText(this, wxID_ANY, "&Name:"));
        m_commandName = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(350, 25), 0);
        sizerMsgs->Add(m_commandName);
    }
    
    {
        sizerMsgs->Add(new wxStaticText(this, wxID_ANY, "&Command line:"));
        m_cmdToExecute = new wxTextCtrl(this, wxID_ANY, "a", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
        sizerMsgs->Add(m_cmdToExecute, wxSizerFlags(1).Expand().Border(wxBOTTOM));
    }
    
    {
        sizerMsgs->Add(new wxStaticText(this, wxID_ANY, "&Hide console?"));
        m_isHidden = new wxCheckBox(this, wxID_ANY, "");
        sizerMsgs->Add(m_isHidden);
    }

    /* A command's colours have no "unset" state, so the panel is built without
       its per-colour checkboxes and always reports a value. */
    m_style = new gui::TextStylePanel(this, { .optional_colors = false });
    sizerMsgs->Add(m_style, wxSizerFlags(1).Expand());

    {
        sizerMsgs->Add(new wxStaticText(this, wxID_ANY, "&Min Size:"));
        m_minSize = new wxTextCtrl(this, wxID_ANY, "a", wxDefaultPosition, wxDefaultSize);
        sizerMsgs->Add(m_minSize);
    }

    {
        sizerMsgs->Add(new wxStaticText(this, wxID_ANY, "&Is Sizer Base?"));
        m_isSizerBase = new wxCheckBox(this, wxID_ANY, "");
        sizerMsgs->Add(m_isSizerBase);
    }

    {
        sizerMsgs->Add(new wxStaticText(this, wxID_ANY, "&Add to prev sizer?"));
        m_isAddToPrevSizer = new wxCheckBox(this, wxID_ANY, "");
        sizerMsgs->Add(m_isAddToPrevSizer);
    }
    
    sizerTop->Add(sizerMsgs, wxSizerFlags(1).Expand().Border());
    
    // finally buttons to show the resulting message box and close this dialog
    sizerTop->Add(CreateStdDialogButtonSizer(wxAPPLY | wxCLOSE), wxSizerFlags().Right().Border()); /* wxOK */

    SetSizerAndFit(sizerTop);
    CentreOnScreen();
}

void CmdExecutorEditDialog::ShowDialog(const wxString& cmd_name, const wxString& cmd_to_execute, bool hide_console, 
    uint32_t color, uint32_t bg_color, bool is_bold, const wxString& font_face, float scale, wxSize size, bool is_sizer_base, bool add_to_prev_sizer)
{
    m_commandName->SetLabel(cmd_name);
    m_cmdToExecute->SetLabel(cmd_to_execute);
    m_isHidden->SetValue(hide_console);
    m_style->SetValue({ color, bg_color, is_bold, font_face.ToStdString(), scale });
    m_minSize->SetLabelText(wxString::Format("%d,%d", size.x, size.y));
    m_isSizerBase->SetValue(is_sizer_base);
    m_isAddToPrevSizer->SetValue(add_to_prev_sizer);

    m_IsApplyClicked = false;
    ShowModal();
    DBG("isapply: %d", IsApplyClicked());
}

void CmdExecutorEditDialog::OnApply(wxCommandEvent& WXUNUSED(event))
{
    Close();
    m_IsApplyClicked = true;
}

wxBEGIN_EVENT_TABLE(CmdExecutorEditDialog, wxDialog)
EVT_BUTTON(wxID_APPLY, CmdExecutorEditDialog::OnApply)
wxEND_EVENT_TABLE()

CmdExecutorPanelBase::CmdExecutorPanelBase(wxFrame* parent, CmdExecutor& executor, const wxSize& notebook_size)
    : wxPanel(parent, wxID_ANY), m_executor(executor)
{
    m_notebook = new wxAuiNotebook(this, wxID_ANY, wxPoint(0, 0), notebook_size, wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_MIDDLE_CLICK_CLOSE | wxAUI_NB_TAB_EXTERNAL_MOVE | wxNO_BORDER);
    m_notebook->Connect(wxEVT_COMMAND_AUINOTEBOOK_TAB_RIGHT_DOWN, wxAuiNotebookEventHandler(CmdExecutorPanelBase::OnAuiRightClick), NULL, this);

    m_executor.SetMediator(this);
    ReloadCommands();
    m_notebook->Layout();
    Show();

    Bind(wxEVT_RIGHT_DOWN, &CmdExecutorPanelBase::OnPanelRightClick, this);
}

CmdExecutorPanelBase::~CmdExecutorPanelBase()
{
    m_executor.SetMediator(nullptr);
}


void CmdExecutorPanelBase::OnPanelRightClick(wxMouseEvent& event)
{
    DBG("click");
    event.Skip();
}

void CmdExecutorPanelBase::ReloadCommands()
{
    m_executor.ReloadCommandsFromFile();
}

void CmdExecutorPanelBase::OnSize(wxSizeEvent& evt)
{
    wxSize new_size = evt.GetSize();
    if(m_notebook)
        m_notebook->SetSize(new_size);
    evt.Skip(true);
}

void CmdExecutorPanelBase::RenamePage(int page_id)
{
    CommandPageNames& page_names = m_executor.GetPageNames();

    wxTextEntryDialog d(this, "Type new page name here", "Rename");
    d.SetValue(page_names[page_id]);
    if(d.ShowModal() != wxID_OK)
        return;

    m_notebook->Freeze();
    m_notebook->SetPageText(page_id, d.GetValue());
    m_notebook->Thaw();

    page_names[page_id] = d.GetValue().ToStdString();
}

void CmdExecutorPanelBase::ChangePageIcon(int page_id)
{
    CommandPageIcons& page_icons = m_executor.GetPageIcons();

    IconSelectionDialog d(this);
    d.SelectIconByName(page_icons[page_id]);
    if(d.ShowModal() != wxID_OK)
        return;

    const wxString icon_name = d.GetSelectedIcon();
    m_notebook->Freeze();
    m_notebook->SetPageBitmap(page_id, wxArtProvider::GetBitmap(icon_name, wxART_OTHER, FromDIP(wxSize(16, 16))));
    m_notebook->Thaw();

    page_icons[page_id] = icon_name.ToStdString();
}

void CmdExecutorPanelBase::OnAuiRightClick(wxAuiNotebookEvent& evt)
{
    const int page_id = static_cast<uint8_t>(evt.GetSelection());

    const gui::MenuEntry entries[]{
        gui::MenuCommand{ "&Rename", [this, page_id] { RenamePage(page_id); }, wxART_CDROM },
        gui::MenuCommand{ "&Change icon", [this, page_id] { ChangePageIcon(page_id); }, wxART_FIND },
        gui::MenuCommand{ "&Add", [this, page_id]
            {
                m_executor.AddPage(page_id, page_id + 1);
                m_executor.SaveToTempAndReload();
            }, wxART_ADD_BOOKMARK },
        gui::MenuCommand{ "&Delete", [this, page_id]
            {
                wxMessageDialog d(this, "Are you sure want to delete this page?", "Deleting", wxOK | wxCANCEL);
                if(d.ShowModal() != wxID_OK)
                    return;

                m_executor.DeletePage(page_id);
                m_executor.SaveToTempAndReload();
            }, wxART_DELETE },
        gui::MenuCommand{ "&Duplicate Before", [this, page_id]
            {
                m_executor.CopyPage(page_id, page_id);
                m_executor.SaveToTempAndReload();
            }, wxART_COPY },
        gui::MenuCommand{ "&Duplicate After", [this, page_id]
            {
                m_executor.CopyPage(page_id, page_id + 1);
                m_executor.SaveToTempAndReload();
            }, wxART_COPY },
    };
    gui::RunContextMenu(this, entries);
}

void CmdExecutorPanelBase::OnPreReload(uint8_t page)
{
    //DBG("OnPreReload pages: %d\n", page);

    m_notebook->DeleteAllPages();
    m_Pages.clear();

    m_notebook->Freeze();
    for(uint8_t i = 0; i != page; i++)
    {
        CmdExecutorPanelPage* p = new CmdExecutorPanelPage(m_notebook, i + 1, 0, m_executor);
        m_notebook->AddPage(p, std::format("Page {}", i + 1), false, wxArtProvider::GetBitmap(wxART_HARDDISK, wxART_OTHER, FromDIP(wxSize(16, 16))));
        m_Pages.push_back(p);
    }
    m_notebook->Thaw();
}

void CmdExecutorPanelBase::OnPreReloadColumns(uint8_t pages, uint8_t cols)
{
    m_Pages[pages - 1]->OnPreload(cols);
    //DBG("OnPreReloadColumns pages: %d, cols: %d\n", pages, cols);
}

void CmdExecutorPanelBase::OnCommandLoaded(uint8_t page, uint8_t col, CommandTypes cmd)
{
    m_Pages[page - 1]->OnCommandLoaded(col, cmd);
}

void CmdExecutorPanelBase::OnPostReload(uint8_t page, uint8_t cols, CommandPageNames& names, CommandPageIcons& icons)
{
    m_Pages[page - 1]->OnPostReloadUpdate();

    m_notebook->Freeze();
    m_notebook->SetPageText(page - 1, names.back());
    if(!icons.back().empty())
        m_notebook->SetPageBitmap(page - 1, wxArtProvider::GetBitmap(icons.back(), wxART_OTHER, FromDIP(wxSize(16, 16))));
    m_notebook->Thaw();
}

CmdExecutorPanelPage::CmdExecutorPanelPage(wxWindow* parent, uint8_t id, uint8_t cols, CmdExecutor& executor)
    : wxPanel(parent, wxID_ANY), m_Id(id), m_executor(executor)
{
    edit_dlg = new CmdExecutorEditDialog(this);

    /* Parameters are free text: re-reading them as hex or binary would destroy
       them, which is why this dialog's base radio buttons had been commented
       out rather than removed. */
    param_dlg = new gui::BitFieldEditorDialog(this,
        { .title = "Param editor", .group_label = "&Params", .allow_base_change = false });

    //DBG("CmdExecutorPanelPage constructor %d, %d\n", id, cols);
    Bind(wxEVT_RIGHT_DOWN, &CmdExecutorPanelPage::OnPanelRightClick, this);
}

void CmdExecutorPanelPage::OnSize(wxSizeEvent& evt)
{
    evt.Skip(true);
}

void CmdExecutorPanelPage::OnPaint(wxPaintEvent& evt)
{
    evt.Skip(true);
}

void CmdExecutorPanelPage::ToggleAllButtonClickability(bool toggle)
{
    for(auto& i : m_ButtonMap)
    {
        std::visit([toggle](auto& btn)
            {
                using T = std::decay_t<decltype(btn)>;
                if constexpr(std::is_same_v<T, wxButton*>)
                {
                    if(btn)
                        btn->Enable(toggle);
                }
            }, i.second);
    }
}

void CmdExecutorPanelPage::OnPanelRightClick(wxMouseEvent& event)
{
    /* Which column the pointer is over decides what every item below acts on,
       so it is worked out before the menu is described rather than after it
       was built - the menu used to be assembled and then thrown away on a
       right-click that landed outside every column. */
    const wxPoint pt = wxGetMousePosition();
    const int mouseX = pt.x - this->GetScreenPosition().x;

    uint8_t col = 1;
    for(auto& i : m_VertialBoxes)
    {
        const wxPoint sizer_pos = i->GetPosition();
        const wxSize sizer_size = i->GetSize();

        if(mouseX > sizer_pos.x && mouseX < (sizer_pos.x + sizer_size.x))
            break;

        col++;
    }

    if(col == m_VertialBoxes.size() + 1)
    {
        DBG("invalid item");
        return;
    }

    const gui::MenuEntry entries[]{
        gui::MenuCommand{ "&Add", [this, col]
            {
                m_executor.AddCommand(m_Id, col,
                    Command(std::format("New cmd {}", utils::random_mt(1, 1000)), "& ping 127.0.0.1 -n 3 > nul", "", false, utils::random_mt(0x0, 0xFFFFFF), 0xFFFFFF, false, "", 1.0f));

                OnPostReloadUpdate();
            }, wxART_CDROM },
        gui::MenuCommand{ "&Add separator", [this, col]
            {
                if(const auto separator_width = gui::PromptForByte(this, "Specify separator width",
                       "Add separator", 10, "separator width"))
                    m_executor.AddSeparator(m_Id, col, Separator(*separator_width));

                OnPostReloadUpdate();
            }, wxART_CDROM },
        gui::MenuCommand{ "&Add col", [this, col]
            {
                /* This page's own 0-based index. The static this replaced defaulted
                   to 0 until a tab event fired, so a right-click before any tab
                   switch edited the wrong page. m_Id is 1-based. */
                m_executor.AddCol(static_cast<uint8_t>(m_Id - 1), col);
                m_executor.SaveToTempAndReload();
            }, wxART_REMOVABLE },
        gui::MenuCommand{ "&Delete col", [this, col]
            {
                wxMessageDialog d(this, "Are you sure want to delete this page?",
                    wxString::Format("Deleting col %d", col), wxOK | wxCANCEL);
                if(d.ShowModal() != wxID_OK)
                    return;

                m_executor.DeleteCol(static_cast<uint8_t>(m_Id - 1), col - 1);
                m_executor.SaveToTempAndReload();
            }, wxART_DELETE },
        gui::MenuCommand{ "&Save", [this]
            {
                m_executor.Save();
                LOG(LogLevel::Notification, "Commands has been saved");
            }, wxART_FLOPPY },
        gui::MenuCommand{ "&Reload", [this]
            {
                m_executor.ReloadCommandsFromFile();
                LOG(LogLevel::Notification, "Commands has been reloaded");
            }, wxART_GO_UP },
    };
    gui::RunContextMenu(this, entries);
}

void CmdExecutorPanelPage::OnClick(wxCommandEvent& event)
{
    auto obj = event.GetEventObject();

    wxButton* btn = dynamic_cast<wxButton*>(obj);
    if(btn == nullptr)
    {
        LOG(LogLevel::Error, "btn is nullptr");
        return;
    }

    void* clientdata = btn->GetClientData();
    if(clientdata == nullptr)
    {
        LOG(LogLevel::Error, "clientdata is nullptr");
        return;
    }

    Command* c = reinterpret_cast<Command*>(clientdata);
    Execute(c);
}

uint8_t CmdExecutorPanelPage::ColumnOfButton(wxButton* btn)
{
    for(auto& [column, element] : m_ButtonMap)
    {
        const bool is_this_button = std::visit([btn](auto& mapped)
            {
                using T = std::decay_t<decltype(mapped)>;
                if constexpr(std::is_same_v<T, wxButton*>)
                    return mapped == btn;
                else
                    return false;
            }, element);

        if(is_this_button)
            return column;
    }

    return 0xFF;
}

void CmdExecutorPanelPage::EditCommand(Command* c, wxButton* btn)
{
    edit_dlg->ShowDialog(c->GetName(), c->GetCmd(), c->IsConsoleHidden(), c->GetColor(), c->GetBackgroundColor(), c->IsBold(), c->GetFontFace(), c->GetScale(),
        ToWxSize(c->GetMinSize()), c->IsUsingSizer(), c->IsAddToPrevSizer());
    if(!edit_dlg->IsApplyClicked())
        return;

    /* The panel was built with optional_colors off, so both colours are
       always present; a command has no "unset colour" state. */
    const gui::TextStyleEdit style = edit_dlg->GetStyle();

    c->SetName(edit_dlg->GetCmdName().ToStdString()).SetCmd(edit_dlg->GetCmd().ToStdString()).
        SetConsoleHidden(edit_dlg->IsHidden()).
        SetColor(style.color.value_or(0)).SetBackgroundColor(style.background_color.value_or(0xFFFFFF)).
        SetBold(style.is_bold).SetFontFace(style.font_face).SetScale(style.scale).
        SetMinSize(ToLogicalSize(edit_dlg->GetMinSize())).SetUseSizer(edit_dlg->IsUsingSizer()).
        SetAddToPrevSizer(edit_dlg->IsAddToPrevSizer());

    UpdateCommandButon(c, btn, true);
}

void CmdExecutorPanelPage::OnRightClick(wxMouseEvent& event)
{
    wxButton* btn = dynamic_cast<wxButton*>(event.GetEventObject());
    if(btn == nullptr)
    {
        LOG(LogLevel::Error, "btn is nullptr");
        return;
    }

    void* clientdata = btn->GetClientData();
    if(clientdata == nullptr)
    {
        LOG(LogLevel::Error, "clientdata is nullptr");
        return;
    }

    Command* c = reinterpret_cast<Command*>(clientdata);

    const gui::MenuEntry entries[]{
        gui::MenuCommand{ "&Edit", [this, c, btn] { EditCommand(c, btn); }, wxART_EDIT },
        gui::MenuCommand{ "&Change icon", [this, c, btn]
            {
                IconSelectionDialog d(this);
                d.SelectIconByName(c->GetIcon());
                if(d.ShowModal() != wxID_OK)
                    return;

                const std::string icon_name = d.GetSelectedIcon().ToStdString();
                if(c->GetIcon() == icon_name)
                    return;

                c->SetIcon(icon_name);
                UpdateCommandButon(c, btn, true);
            }, wxART_CDROM },
        gui::MenuCommand{ "&Duplicate", [this, c, btn]
            {
                m_executor.AddCommand(m_Id, ColumnOfButton(btn) + 1, Command(*c));
                m_BaseGrid->Layout();
            }, wxART_COPY },
        gui::MenuCommand{ "&Delete", [this, btn]
            {
                wxMessageDialog d(this, "Are you sure want to delete this item?", "Error", wxOK | wxCANCEL);
                if(d.ShowModal() == wxID_OK)
                    DeleteCommandButton(nullptr, btn);
            }, wxART_DELETE },
    };
    gui::RunContextMenu(this, entries);
}

void CmdExecutorPanelPage::OnMiddleClick(wxMouseEvent& event)
{
    auto obj = event.GetEventObject();

    wxButton* btn = dynamic_cast<wxButton*>(obj);
    if(btn == nullptr)
    {
        LOG(LogLevel::Error, "btn is nullptr");
        return;
    }

    void* clientdata = btn->GetClientData();
    if(clientdata == nullptr)
    {
        LOG(LogLevel::Error, "clientdata is nullptr");
        return;
    }

    DBG("middleclick");
    Command* c = reinterpret_cast<Command*>(clientdata);
    if(c->m_params.empty())
    {
        LOG(LogLevel::Error, "Param for command '{}' is empty!", c->GetName());
        return;
    }

    /* A command's parameters are free text with no presentation of their own,
       so the rows carry a generated label and nothing else. */
    std::vector<gui::BitFieldRow> rows;
    rows.reserve(c->m_params.size());
    for(std::size_t i = 0; i != c->m_params.size(); i++)
        rows.push_back({ std::format("Param: {}", i + 1), c->m_params[i], {}, {} });

    param_dlg->ShowDialog(std::move(rows));
    if(param_dlg->GetResult() == gui::BitFieldEditorResult::Ok)
    {
        c->m_params = param_dlg->GetOutput();
        Execute(c);
    }
}

void CmdExecutorPanelPage::OnCommandLoaded(uint8_t col, CommandTypes cmd)
{
    std::visit([this, col](auto& concrete_cmd)
        {
            using T = std::decay_t<decltype(concrete_cmd)>;
            if constexpr(std::is_same_v<T, std::shared_ptr<Command>>)
            {
                AddCommandElement(col, concrete_cmd.get());
            }
            else if constexpr(std::is_same_v<T, Separator>)
            {
                AddSeparatorElement(col, concrete_cmd);
            }
            else
                static_assert(always_false_v<T>, "CmdExecutorPanel::OnCommandLoaded Bad visitor!");
        }, cmd);
}

void CmdExecutorPanelPage::OnPreload(uint8_t cols)
{
    if(m_BaseGrid != nullptr)
    {
        m_BaseGrid->Clear(true);
        m_VertialBoxes.clear();
        m_ButtonMap.clear();
    }

    m_BaseGrid = new wxGridSizer(cols);
    m_BaseGrid->Layout();
    for(uint8_t i = 0; i != cols; i++)
    {
        wxStaticBoxSizer* box_sizer = new wxStaticBoxSizer(wxVERTICAL, this, wxString::Format("Col: %d", i + 1));
        box_sizer->SetMinSize(wxSize(200, 200));
        m_BaseGrid->Add(box_sizer, wxSizerFlags(5).Expand());
        m_VertialBoxes.push_back(box_sizer);
    }
    m_BaseGrid->Layout();
}

void CmdExecutorPanelPage::OnPostReloadUpdate()
{
    wxSize old_size = GetSize();
    for(auto& i : m_VertialBoxes)
    {
        i->Layout();
    }
    m_BaseGrid->Layout();
    SetSizerAndFit(m_BaseGrid);
    SetSize(old_size);  /* Size has to be set, because if isn't, only the first column will appear in the base grid after reloading */
}

void CmdExecutorPanelPage::AddCommandElement(uint8_t col, Command* c)
{
    wxButton* btn = nullptr;
    if(c->GetIcon().empty())
        btn = new wxButton(this, wxID_ANY, !c->GetName().empty() ? c->GetName() : c->GetCmd().substr(0, MAX_CMD_LEN_FOR_BUTTON), wxDefaultPosition, wxDefaultSize);
    else
    {
        wxBitmap icon = wxArtProvider::GetBitmap(c->GetIcon(), wxART_OTHER, FromDIP(wxSize(50, 50)));
        btn = new wxBitmapButton(this, wxID_ANY, icon);
        
        if(!c->GetMinSize().IsDefault())
            btn->SetMinSize(ToWxSize(c->GetMinSize()));
    }
    UpdateCommandButon(c, btn);

    btn->SetClientData((void*)c);
    btn->Bind(wxEVT_BUTTON, &CmdExecutorPanelPage::OnClick, this);
    btn->Bind(wxEVT_RIGHT_DOWN, &CmdExecutorPanelPage::OnRightClick, this);
    btn->Bind(wxEVT_MIDDLE_DOWN, &CmdExecutorPanelPage::OnMiddleClick, this);

    m_ButtonMap.emplace(col - 1, btn);

    if(c->IsUsingSizer() && !base_sizer)
    {
        gv_sizer = new wxBoxSizer(wxHORIZONTAL);
        base_sizer = true;
    }

    if(base_sizer && (c->IsUsingSizer() || c->IsAddToPrevSizer()))
    {
        gv_sizer->Add(btn);
        return;
    }

    if(!c->IsAddToPrevSizer() && base_sizer)
    {
        base_sizer = false;
        m_VertialBoxes[col - 1]->Add(gv_sizer);
        gv_sizer = nullptr;
    }

    m_VertialBoxes[col - 1]->Add(btn);
}

void CmdExecutorPanelPage::AddSeparatorElement(uint8_t col, Separator s)
{
    wxStaticLine* line = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxSize(210, s.width), wxLI_HORIZONTAL);
    m_ButtonMap.emplace(col - 1, line);
    m_VertialBoxes[col - 1]->Add(line, wxSizerFlags(0).Expand());

    line->Bind(wxEVT_RIGHT_DOWN, &CmdExecutorPanelPage::OnRightClick, this);
}

void CmdExecutorPanelPage::UpdateCommandButon(Command* c, wxButton* btn, bool force_font_reset)
{
    btn->SetToolTip(c->GetCmd());
    btn->SetForegroundColour(RGB_TO_WXCOLOR(c->GetColor()));  /* input for red: 0x00FF0000, excepted input for wxColor 0x0000FF */
    btn->SetBackgroundColour(RGB_TO_WXCOLOR(c->GetBackgroundColor()));

    wxFont font;
    font.SetWeight(c->IsBold() ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
    font.Scale(1.0f);  /* Scale has to be set to default first */
    btn->SetFont(font);
    font.Scale(c->GetScale());

    if(!c->GetFontFace().empty())
        font.SetFaceName(c->GetFontFace());
    btn->SetFont(font);

    if(force_font_reset)
    {
        if(c->GetIcon().empty())
        {
            std::string new_name = !c->GetName().empty() ? c->GetName() : c->GetCmd().substr(0, MAX_CMD_LEN_FOR_BUTTON);
            btn->SetLabelText(new_name);
        }
        else
        {
            btn->SetBitmapLabel(wxArtProvider::GetBitmap(c->GetIcon(), wxART_OTHER, FromDIP(wxSize(24, 24))));
            btn->SetLabelText("");
        }
    }
    btn->SetMinSize(ToWxSize(c->GetMinSize()));
    m_BaseGrid->Layout();
}

void CmdExecutorPanelPage::DeleteCommandButton(Command* c, wxButton* btn)
{
    auto it = m_ButtonMap.begin();
    while(it != m_ButtonMap.end())
    {
         bool ret = std::visit([btn, &it, this](auto& it_button)
            {
                using T = std::decay_t<decltype(it_button)>;
                if constexpr(std::is_same_v<T, wxButton*>)
                {
                    if(it_button == btn)
                    {
                        it = m_ButtonMap.erase(it);
                        return true;
                    }
                }
                return false;
            }, it->second);

        if(ret)
            break;
        else
            ++it;
    }

    //btn->RemoveChild(this);
    btn->DeletePendingEvents();
    btn->Disconnect(wxEVT_BUTTON);
    btn->Disconnect(wxEVT_RIGHT_DOWN);
    btn->Disconnect(wxEVT_MIDDLE_DOWN);
    btn->Destroy();

    m_BaseGrid->Layout();
}

void CmdExecutorPanelPage::Execute(Command* c)
{
    ToggleAllButtonClickability(false);
    m_executor.Execute(*c);
    ToggleAllButtonClickability(true);
}

IconSelectionDialog::IconSelectionDialog(wxWindow* parent) : wxDialog(parent, wxID_ANY, "Select an icon")
{
    // Create the wxBitmapComboBox.

    wxArrayString empty_str;
    icon_combo_box = new wxBitmapComboBox(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, empty_str, 0);

    // Add all available wxART_* icons to the wxBitmapComboBox.
    for(const auto& art_name : art_names)
    {
        wxBitmap icon = wxArtProvider::GetBitmap(art_name, wxART_OTHER, FromDIP(wxSize(24, 24)));
        if(icon.IsOk()) {
            icon_combo_box->Append(art_name, icon);
        }
    }

    // Set the default icon.
    icon_combo_box->SetSelection(0);

    // Layout the wxBitmapComboBox.
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(icon_combo_box, wxSizerFlags().Expand());
    main_sizer->Add(CreateStdDialogButtonSizer(wxCANCEL | wxOK), wxSizerFlags().Right().Border()); /* wxOK */
    SetSizer(main_sizer);
}

void IconSelectionDialog::SelectIconByName(const wxString& name)
{
    int cnt = 0;
    for(const auto& art_name : art_names)
    {
        if(art_name == name)
        {
            wxBitmap icon = wxArtProvider::GetBitmap(art_name, wxART_OTHER, FromDIP(wxSize(24, 24)));
            if(icon.IsOk()) {
                icon_combo_box->SetSelection(cnt);
            }
        }
        cnt++;
    }
}

wxString IconSelectionDialog::GetSelectedIcon() 
{
    return icon_combo_box->GetStringSelection();
}


