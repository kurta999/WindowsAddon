#include "pch.hpp"
#include "MenuCommand.hpp"

wxBEGIN_EVENT_TABLE(BackupPanel, wxPanel)
EVT_TREELIST_ITEM_CONTEXT_MENU(ID_BackupPanel, BackupPanel::OnItemContextMenu)
EVT_TREELIST_ITEM_ACTIVATED(ID_BackupPanel, BackupPanel::OnItemActivated)
wxEND_EVENT_TABLE()

void BackupPanel::OnItemContextMenu(wxTreeListEvent& evt)
{
	const wxTreeListItem item = evt.GetItem();
	const wxTreeListItem root = tree->GetItemParent(item);
	if(root == NULL) return;

	const gui::MenuEntry entries[]{
		gui::MenuCommand{ "&Add new backup", [this, item]
			{
				m_Backups.AddEntry(BackupEntry(L"C:\\folder_non_exists",
					std::vector<std::filesystem::path>{L"C:\\backup"},
					std::vector<std::string>({ ".gitignore", ".txt" }), 2, false, 0, 1));

				UpdateMainTree();
			}, wxART_NEW_DIR },
		gui::MenuCommand{ "&Delete", [this, item, root]
			{
				wxClientData* itemdata = tree->GetItemData(root);
				if(!itemdata)
					itemdata = tree->GetItemData(item);

				wxIntClientData<uint16_t>* dret = dynamic_cast<wxIntClientData<uint16_t>*>(itemdata);
				if(!dret)
					return;

				m_Backups.RemoveEntry(dret->GetValue());
				UpdateMainTree();
			}, wxART_DELETE,
			/* Offered always, but only choosable on an item that has children -
			   the menu keeps its shape whichever row is right-clicked. */
			{}, [this, item] { return tree->GetFirstChild(item).IsOk(); } },
	};
	gui::RunContextMenu(tree, entries);
}

void BackupPanel::OnItemActivated(wxTreeListEvent& evt)
{
	wxTreeListItem item = evt.GetItem();
	wxTreeListItem root = tree->GetItemParent(item);
	const wxString& type_str = tree->GetItemText(item, 0);
	if(root)
	{
		wxClientData* itemdata = tree->GetItemData(root);
		if(!itemdata) return; /* To avoid crash */

		wxIntClientData<uint16_t>* dret = dynamic_cast<wxIntClientData<uint16_t>*>(itemdata);
		uint16_t id = dret->GetValue();
		auto entry = m_Backups.GetEntry(id);
		if(!entry)
			return;

		if(type_str == "Source")
		{
			wxDirDialog d(this, "Chose source directory", entry->from.generic_string(), wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
			int ret_code = d.ShowModal();
			if(ret_code == wxID_OK)  /* OK */
			{
				std::wstring str = d.GetPath().ToStdWstring();
				if(std::filesystem::exists(str))
				{
					entry->from = str;
					m_Backups.UpdateEntry(id, std::move(*entry));
					UpdateMainTree();
				}
				else
				{
					wxMessageDialog(this, "Given directory doesn't exists", "Error", wxOK).ShowModal();
				}
			}
		}
		else if(type_str == "Destination")
		{
			wxString str;
			for(auto& i : entry->to)
			{
				str += i.generic_string() + "\n";
			}
			if(!str.empty() && str[str.length() - 1] == '\n')
				str.erase(str.length() - 1, str.length());
			wxTextEntryDialog d(this, "Enter below destiantion list where backup(s) will be placed", "Enter destination(s)", str, wxOK | wxCANCEL | wxTE_MULTILINE);
			int ret_code = d.ShowModal();
			if(ret_code == wxID_OK)  /* OK */
			{
				std::wstring result = d.GetValue().ToStdWstring();
				std::vector<std::filesystem::path> new_destination_list;
				boost::split(new_destination_list, result, boost::is_any_of("\n"));

				entry->to = std::move(new_destination_list);
				m_Backups.UpdateEntry(id, std::move(*entry));
				UpdateMainTree();
			}
		}
		else if(type_str == "Ignore")
		{
			wxString str;
			for(auto& i : entry->ignore_list)
			{
				str += i + "\n";
			}
			if(!str.empty() && str[str.length() - 1] == '\n')
				str.erase(str.length() - 1, str.length());
			wxTextEntryDialog d(this, "Enter below desired folder names which you want to ignore", "Ignore list", str, wxOK | wxCANCEL | wxTE_MULTILINE);
			int ret_code = d.ShowModal();
			if(ret_code == wxID_OK)  /* OK */
			{ 
				std::string result = d.GetValue().ToStdString();
				std::vector<std::string> new_ignore_list;
				boost::split(new_ignore_list, result, boost::is_any_of("\n"));

				entry->ignore_list = std::move(new_ignore_list);
				m_Backups.UpdateEntry(id, std::move(*entry));
				UpdateMainTree();
			}
		}
		else if(type_str == "Max backups")
		{
			wxTextEntryDialog d(this, "Enter maximum number of backups", "Enter max backups", std::to_string(entry->max_backups), wxOK | wxCANCEL);
			d.SetTextValidator(wxFILTER_DIGITS);
			int ret_code = d.ShowModal();
			if(ret_code == wxID_OK)  /* OK */
			{
				try
				{
					entry->max_backups = BackupEntry::ClampMaxBackups(
						utils::stoi<long long>(d.GetValue().ToStdString()));
					m_Backups.UpdateEntry(id, std::move(*entry));
					UpdateMainTree();
				}
				catch(...)
				{
					wxMessageDialog(this, "Given input isn't number", "Error", wxOK).ShowModal();
				}
			}
		}
		else if(type_str == "Compress")
		{
			wxTextEntryDialog d(this, "Enter 0 or 1 to toggle file compressing after backup\n7z has to be installed on the system, to make it work", 
				"Toggle backup compressing", std::to_string(entry->m_Compress), wxOK | wxCANCEL);
			d.SetTextValidator(wxFILTER_DIGITS);
			int ret_code = d.ShowModal();
			if(ret_code == wxID_OK)  /* OK */
			{
				entry->m_Compress = utils::stob(d.GetValue().ToStdString());
				m_Backups.UpdateEntry(id, std::move(*entry));
				UpdateMainTree();
			}
		}
		else if(type_str == "Calculate hash")
		{
			wxTextEntryDialog d(this, "Enter 0 or 1 to toggle hash calculating\nEnabled hash calculation will result in more reliable backup, but takes longer", 
				"Toggle hash calculating ", std::to_string(entry->calculate_hash), wxOK | wxCANCEL);
			d.SetTextValidator(wxFILTER_DIGITS);
			int ret_code = d.ShowModal();
			if(ret_code == wxID_OK)  /* OK */
			{
				entry->calculate_hash = utils::stob(d.GetValue().ToStdString());
				m_Backups.UpdateEntry(id, std::move(*entry));
				UpdateMainTree();
			}
		}
		else if(type_str == "Hash buffer size")
		{
			wxTextEntryDialog d(this, "For folders with bigger files raise, for small files leave the default 1 MB.\nIncreasing buffer can result in improved backup performance", 
				"Hash buffer size ", std::to_string(entry->hash_buf_size), wxOK | wxCANCEL);
			d.SetTextValidator(wxFILTER_DIGITS);
			int ret_code = d.ShowModal();
			if(ret_code == wxID_OK)  /* OK */
			{
				try
				{
					size_t max_backups = utils::stoi<size_t>(d.GetValue().ToStdString());
					entry->hash_buf_size = max_backups;
					m_Backups.UpdateEntry(id, std::move(*entry));
					UpdateMainTree();
				}
				catch(...)
				{
					wxMessageDialog(this, "Given input isn't number", "Error", wxOK).ShowModal();
				}
				UpdateMainTree();
			}
		}
	}
}

void BackupPanel::UpdateMainTree()
{
	tree->DeleteAllItems();
	wxTreeListItem root = tree->GetRootItem();
	tree->DeleteAllItems();
	uint16_t cnt = 0;
	for(const auto& i : m_Backups.GetEntries())
	{
		wxTreeListItem item = tree->AppendItem(root, i.from.filename().generic_string().c_str(), -1, -1, new wxIntClientData(cnt++));
		wxTreeListItem bind_item = tree->AppendItem(item, "Source");
		tree->SetItemText(bind_item, 1, i.from.generic_string());
		bind_item = tree->AppendItem(item, "Destination");
		tree->SetItemText(bind_item, 1, i.to.empty() ? "" : i.to[0].generic_string());
		bind_item = tree->AppendItem(item, "Ignore");

		wxString str_ignore;
		for(const auto& ignored : i.ignore_list)
		{
			str_ignore += ignored + " ";
			if(str_ignore.length() > 80)
				break;
		}
		tree->SetItemText(bind_item, 1, str_ignore);
		bind_item = tree->AppendItem(item, "Max backups");
		tree->SetItemText(bind_item, 1, std::to_string(i.max_backups));
		bind_item = tree->AppendItem(item, "Compress");
		tree->SetItemText(bind_item, 1, i.m_Compress ? "Yes" : "No");
		bind_item = tree->AppendItem(item, "Calculate hash");
		tree->SetItemText(bind_item, 1, i.calculate_hash ? "Yes" : "No");
		bind_item = tree->AppendItem(item, "Hash buffer size");
		tree->SetItemText(bind_item, 1, boost::lexical_cast<std::string>(i.hash_buf_size));

		tree->Expand(item);
	}
}

BackupPanel::BackupPanel(wxWindow* parent, DirectoryBackup& backups)
	: wxPanel(parent, wxID_ANY), m_Backups(backups)
{
	wxBoxSizer* bSizer1 = new wxBoxSizer(wxHORIZONTAL);

	tree = new wxTreeListCtrl(this, ID_BackupPanel, wxDefaultPosition, wxSize(600, 800), wxTL_DEFAULT_STYLE);
	tree->AppendColumn("Backup name", tree->WidthFor("App nameApp nameApp name"), wxALIGN_RIGHT, wxCOL_RESIZABLE | wxCOL_SORTABLE);
	tree->AppendColumn("Data", tree->WidthFor("Key bindingsKey bindings"), wxALIGN_RIGHT, wxCOL_RESIZABLE | wxCOL_SORTABLE);
	UpdateMainTree();

	bSizer1->Add(tree, wxSizerFlags(2).Left().Border(wxRIGHT, 25).Expand());
	SetSizer(bSizer1);
	Show();
}
