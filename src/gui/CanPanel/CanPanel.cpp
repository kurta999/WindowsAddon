#include "pch.hpp"

#include <wx/headerctrl.h>

wxBEGIN_EVENT_TABLE(CanPanel, wxPanel)
EVT_SIZE(CanPanel::OnSize)
wxEND_EVENT_TABLE()

CanPanel::CanPanel(wxWindow* parent, CanEntryHandler& handler, CanSerialPort& port, const wxSize& notebook_size)
	: wxPanel(parent, wxID_ANY), m_handler(handler)
{
    wxSize client_size = GetClientSize();

    m_mgr.SetManagedWindow(this);

    m_notebook = new wxAuiNotebook(this, wxID_ANY, wxPoint(0, 0), notebook_size, wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_MIDDLE_CLICK_CLOSE | wxAUI_NB_TAB_EXTERNAL_MOVE | wxNO_BORDER);
    sender = new CanSenderPanel(this, m_handler, port);
    log = new CanLogPanel(this, m_handler);
    script = new CanScriptPanel(this, m_handler);
    m_notebook->Freeze();
    m_notebook->AddPage(sender, "Sender", false, wxArtProvider::GetBitmap(wxART_HELP_BOOK, wxART_OTHER, FromDIP(wxSize(16, 16))));
    m_notebook->AddPage(log, "Log", false, wxArtProvider::GetBitmap(wxART_HELP_SETTINGS, wxART_OTHER, FromDIP(wxSize(16, 16))));
    m_notebook->AddPage(script, "Script", false, wxArtProvider::GetBitmap(wxART_PLUS, wxART_OTHER, FromDIP(wxSize(16, 16))));
    m_notebook->Connect(wxEVT_COMMAND_AUINOTEBOOK_PAGE_CHANGED, wxAuiNotebookEventHandler(CanPanel::Changeing), NULL, this);
    
    /* size: 1640x1080 */
    m_notebook->Split(0, wxLEFT);
    
    m_notebook->Thaw();
    m_notebook->SetAutoLayout(true);
    m_notebook->Layout();
    m_notebook->SetSize(m_notebook->GetSize());
    m_notebook->SetSelection(0);
}

CanPanel::~CanPanel()
{
    m_mgr.UnInit();  /* deinitialize the frame manager */
}

void CanPanel::RefreshSubpanels()
{
    sender->RefreshSubpanels();
}

void CanPanel::LoadTxList()
{
    sender->LoadTxList();
}

void CanPanel::SaveTxList()
{
    sender->SaveTxList();
}

void CanPanel::LoadRxList()
{
    sender->LoadRxList();
}

void CanPanel::SaveRxList()
{
    sender->SaveRxList();
}

void CanPanel::LoadMapping()
{
    sender->LoadMapping();
}

void CanPanel::SaveMapping()
{
    sender->SaveMapping();
}

void CanPanel::On10MsTimer()
{
    /* This used to hold the CAN model lock across both sub-panel ticks, so the
       receive thread was blocked for the whole repaint. Each panel now takes
       the lock only while it copies out what it is about to draw. */
    sender->On10MsTimer();
    log->On10MsTimer();
}

void CanPanel::Changeing(wxAuiNotebookEvent& event)
{
    /*
    int sel = event.GetSelection();
    if(sel == 0)
    {
        comtcp_panel->Update();
    }
    */
}

void CanPanel::OnFrameResized(const wxSize& size)
{
	SetSize(size);
	if(m_notebook)
		m_notebook->SetSize(size);
	if(sender)
		sender->SetSize(size);
	if(log)
		log->SetSize(size);
	if(script)
		script->SetSize(size);
	/* Unguarded in the frame's copy too: the layout ran whenever the panel
	   existed, and the notebook always does by then. */
	m_notebook->Layout();
}

void CanPanel::OnSize(wxSizeEvent& evt)
{
    evt.Skip(true);
}