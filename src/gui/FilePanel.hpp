#pragma once

#include <inttypes.h>
#include <wx/wx.h>
#include <wx/treelist.h>

#include <map>

class MyComparator : public wxTreeListItemComparator
{
public:
	virtual int Compare(wxTreeListCtrl* treelist, unsigned column, wxTreeListItem item1, wxTreeListItem item2) override;

private:
	int64_t GetSizeFromText(const wxString& text) const;
};

class FilePanel : public wxPanel
{
public:
	FilePanel(wxFrame* parent);
	~FilePanel();

	void OnSize(wxSizeEvent& evt);
	void OnItemActivated(wxTreeListEvent& evt);
	void GenerateTree();
	void ClearTree();

private:
	wxButton* m_OkButton = nullptr;
	wxTextCtrl* m_DirText = nullptr;
	wxStaticText* m_ProcessInfo = nullptr;
	wxButton* m_Generate = nullptr;
	wxButton* m_Clear = nullptr;
	wxTreeListCtrl* tree = nullptr;
	MyComparator m_comparator;

	bool m_IsAborted = false;
private:

	/* Tree items only, keyed by DirectoryUsage::KeyOf. Sizes and counts,
	   which used to share this map inside a DirItems struct, live in a
	   DirectoryUsage local to the scan. */
	std::map<size_t, wxTreeListItem> dir_map;
	wxDECLARE_EVENT_TABLE();
};