#pragma once

#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <wx/stc/stc.h>
#include <wx/treelist.h>

template <class T>
class wxIntClientData : public wxClientData
{
public:
	wxIntClientData(T val) : value(val) {}
	~wxIntClientData() {}

	T GetValue() { return value; }
private:
	T value;
};

class BackupPanel : public wxPanel
{
public:
	BackupPanel(wxWindow* parent);
	void UpdateMainTree();

private:
	void OnItemContextMenu(wxTreeListEvent& evt);
	void OnItemActivated(wxTreeListEvent& evt);

	wxButton* m_Ok = nullptr;
	wxTreeListCtrl* tree = nullptr;

	wxDECLARE_EVENT_TABLE();
};
