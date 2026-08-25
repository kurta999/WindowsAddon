#include "pch.hpp"
#include "WxClipboard.hpp"

wxBEGIN_EVENT_TABLE(LogPanel, wxPanel)
wxEND_EVENT_TABLE()

LogPanel::~LogPanel() = default;

LogPanel::LogPanel(wxFrame* parent, Logger& logger)
	: wxPanel(parent, wxID_ANY), m_Logger(logger)
{
	/* The frame registers this panel as the logger's view once it exists,
	   and clears it before the panel is torn down. Registering itself into
	   a global from its own constructor is what ILogHelper was meant to
	   stop. */
	wxBoxSizer* bSizer1 = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* v_sizer = new wxBoxSizer(wxHORIZONTAL);

	wxArrayString filters;
	filters.Add("All");
	filters.Add("Debug");
	filters.Add("Verbose");
	filters.Add("Normal");
	filters.Add("Notification");
	filters.Add("Warning");
	filters.Add("Error");
	filters.Add("Critical");
	m_FilterLevel = new wxComboBox(this, wxID_ANY, "All", wxDefaultPosition, wxDefaultSize, filters, wxTE_PROCESS_ENTER | wxTE_READONLY);
	v_sizer->Add(m_FilterLevel, 0, wxALL, 5);
	m_FilterLevel->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event)
		{
			if(event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER)
			{
				ExecuteSearchInLogfile();
			}
			event.Skip();
		});

	m_FilterText = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	v_sizer->Add(m_FilterText, 0, wxALL, 5);
	m_FilterText->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event)
		{
			if(event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER)
			{
				ExecuteSearchInLogfile();
			}
			event.Skip();
		});
	m_ApplyFilter = new wxButton(this, wxID_ANY, wxT("Filter"), wxDefaultPosition, wxDefaultSize, 0);
	m_ApplyFilter->SetToolTip("Filter log messages from logfile");
	v_sizer->Add(m_ApplyFilter, 0, wxALL, 5);

	m_ApplyFilter->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
		{
			ExecuteSearchInLogfile();
		});

	m_AutoScrollBtn = new wxButton(this, wxID_ANY, wxT("Toggle auto-scroll"), wxDefaultPosition, wxDefaultSize, 0);
	m_AutoScrollBtn->SetToolTip("Toggle auto-scroll");
	v_sizer->Add(m_AutoScrollBtn, 0, wxALL, 5);
	bSizer1->Add(v_sizer, 0, wxALL, 5);

	wxBoxSizer* v_sizer_2 = new wxBoxSizer(wxHORIZONTAL);
	v_sizer_2->Add(new wxStaticText(this, wxID_ANY, "Default log level:"));
	v_sizer_2->AddSpacer(10);

	wxArrayString default_filters;
	default_filters.Add("Debug");
	default_filters.Add("Verbose");
	default_filters.Add("Normal");
	default_filters.Add("Notification");
	default_filters.Add("Warning");
	default_filters.Add("Error");
	default_filters.Add("Critical");
	m_DefaultLogLevel = new wxComboBox(this, wxID_ANY, "All", wxDefaultPosition, wxDefaultSize, default_filters, wxTE_PROCESS_ENTER | wxTE_READONLY);
	v_sizer_2->AddSpacer(10);

	int log_level = static_cast<int>(m_Logger.GetDefaultLogLevel());
	m_DefaultLogLevel->SetSelection(log_level);
	v_sizer_2->Add(m_DefaultLogLevel);

	m_ApplyButton = new wxButton(this, wxID_ANY, wxT("Apply"), wxDefaultPosition, wxDefaultSize, 0);
	m_ApplyButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
		{
			int log_level = m_DefaultLogLevel->GetSelection();
			m_Logger.SetDefaultLogLevel(static_cast<LogLevel>(log_level));
		});
	v_sizer_2->Add(m_ApplyButton);
	v_sizer_2->AddSpacer(15);

	m_FilterList = new wxButton(this, wxID_ANY, wxT("Filters"), wxDefaultPosition, wxDefaultSize, 0);
	m_FilterList->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
		{
			std::string filters = m_Logger.GetLogFilters();
			boost::algorithm::replace_all(filters, "|", "\n");

			wxTextEntryDialog d(this, "Enter below destiantion list where backup(s) will be placed", "Enter filters", filters, wxOK | wxCANCEL | wxTE_MULTILINE);
			int ret_code = d.ShowModal();
			if(ret_code == wxID_OK)  /* OK */
			{
				std::string result = d.GetValue().ToStdString();
				boost::algorithm::replace_all(result, "\n", "|");
				m_Logger.SetLogFilters(result);
			}
		});
	v_sizer_2->Add(m_FilterList);

	bSizer1->Add(v_sizer_2, 0, wxALL, 5);

	m_AutoScrollBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
		{
			m_AutoScroll ^= 1;
			if(m_AutoScroll)
				m_AutoScrollBtn->SetBackgroundColour(wxNullColour);
			else
				m_AutoScrollBtn->SetBackgroundColour(*wxRED);
		});

	m_Log = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, 0, wxLB_SINGLE | wxLB_HSCROLL | wxLB_NEEDED_SB);
	m_Log->Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent& event)
		{
			const int selection = m_Log->GetSelection();
			if(selection == wxNOT_FOUND)
				return;

			gui::CopyTextToClipboard(m_Log->GetString(selection));
		});
	bSizer1->Add(m_Log, wxSizerFlags(1).Left().Expand());

	m_ClearButton = new wxButton(this, wxID_ANY, wxT("Clear"), wxDefaultPosition, wxDefaultSize, 0);
	m_ClearButton->SetToolTip("Clear log box");
	bSizer1->Add(m_ClearButton, 0, wxALL, 5);

	m_ClearButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
		{
			m_Log->Clear();
		});

	//m_LogFilters.push_back({ "IdlePowerSaver" });

	this->SetSizerAndFit(bSizer1);
	this->Layout();
}

void LogPanel::ExecuteSearchInLogfile()
{
	std::string filter = m_FilterText->GetValue().ToStdString();
	std::string log_level = m_FilterLevel->GetValue().ToStdString();
	if(log_level == "All")
		log_level.clear();

	bool ret = m_Logger.SearchInLogFile(filter, log_level);
	if(ret)
		m_Log->ScrollLines(m_Log->GetCount());
}
/*
template<typename T> bool LogPanel::IsFilterered(const T& file)
{
	for(auto& i : m_LogFilters)
	{
		if(i.empty()) continue;
		if(std::search(file.begin(), file.end(), i.begin(), i.end()) != file.end())
		{
			return true;
		}
	}
	return false;
}
*/
void LogPanel::ClearEntries()
{
	m_Log->Clear();
}

void LogPanel::AppendLog(const std::string& file, const std::string& line, bool scroll_to_end)
{
	m_Log->Append(wxString(line));
	if(scroll_to_end && m_AutoScroll)
		m_Log->ScrollLines(m_Log->GetCount());
}

void LogPanel::AppendLog(const std::wstring& file, const std::wstring& line, bool scroll_to_end)
{
	m_Log->Append(wxString(line));
	if(scroll_to_end && m_AutoScroll)
		m_Log->ScrollLines(m_Log->GetCount());
}
