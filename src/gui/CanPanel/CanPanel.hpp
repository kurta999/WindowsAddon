#pragma once

#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <wx/stc/stc.h>
#include <wx/treelist.h>
#include <wx/grid.h>
#include <wx/spinctrl.h>
#include <wx/filepicker.h>

#include <map>

#include "ICanResultPanel.hpp"

#include "CanSenderPanel.hpp"
#include "CanLogPanel.hpp"
#include "CanScriptPanel.hpp"

class CanEntryHandler;


class CanTxEntry;
class CanRxData;
class CanByteEditorDialog;

class CanLogForFrameDialog;
class CanUdsRawDialog;
class CanMap;
class IResultPanel;

class CanSerialPort;

class CanPanel : public wxPanel
{
public:
	CanPanel(wxWindow* parent, CanEntryHandler& handler, CanSerialPort& port, const wxSize& notebook_size);
    ~CanPanel();

    void On10MsTimer();
    void LoadTxList();
    void SaveTxList();
    void LoadRxList();
    void SaveRxList();    
    void LoadMapping();
    void SaveMapping();
    void RefreshSubpanels();

    // !\brief Size this page and its notebook children to the frame's new size.
    //
    // MainFrame::OnSize used to reach through the public members below to
    // do this - the one place outside this class that knew the notebook's
    // structure.
    void OnFrameResized(const wxSize& size);

    CanSenderPanel* sender = nullptr;
    CanLogPanel* log = nullptr;
    CanScriptPanel* script = nullptr;
    wxAuiNotebook* m_notebook = nullptr;

private:
    void OnSize(wxSizeEvent& evt);
    void Changeing(wxAuiNotebookEvent& event);

    // !\brief AUI manager for subwindows
    wxAuiManager m_mgr;


    /* Handed in rather than fetched from wxGetApp() on every use.
       docs/code-style.md: "A class gets its collaborators through its
       constructor. It does not fetch them." */
    CanEntryHandler& m_handler;

	wxDECLARE_EVENT_TABLE();
};

class CanLogForFrameDialog : public wxDialog
{
public:
    CanLogForFrameDialog(wxWindow* parent);

    void ShowDialog(std::vector<std::string>& values);

protected:
    void OnApply(wxCommandEvent& event);
private:

    wxListBox* m_Log = nullptr;
    wxBoxSizer* sizerTop = nullptr;

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_NO_COPY_CLASS(CanLogForFrameDialog);
};
