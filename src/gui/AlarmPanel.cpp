#include "pch.hpp"
#include "MenuCommand.hpp"
#include "MainFrameAccess.hpp"

wxBEGIN_EVENT_TABLE(AlarmPanel, wxPanel)
wxEND_EVENT_TABLE()

AlarmPanel::AlarmPanel(wxFrame* parent, AlarmEntryHandler& handler) :
    wxPanel(parent, wxID_ANY), m_handler(handler)
{
    wxBoxSizer* bSizer1 = new wxBoxSizer(wxVERTICAL);

    m_handler.WithModel([&](AlarmEntryHandler::Model& model)
    {
    for (auto& i : model.entries)
    {
        wxStaticText* text = new wxStaticText(this, NULL, i->name);
        text->SetFont(wxFont(15, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxEmptyString));
        text->SetClientData((void*)i.get());
        text->Bind(wxEVT_RIGHT_DOWN, &AlarmPanel::OnRightClick, this);

        alarms.push_back(text);
    }
    });

    this->SetSizer(bSizer1);
    this->Layout();
}

void AlarmPanel::ShowAlarmDialogInternal()
{
    MyFrame* frame = TryGetMainFrame();
    wxTextEntryDialog dlg(frame, "Enter execution delay in hh:mm format", "Execution");
    int ret = dlg.ShowModal();
    if(ret == wxID_OK)
    {
        std::string input = dlg.GetValue().ToStdString();
        m_DurationText = input;
    }
    m_alarmSemaphore.release();
}

const std::string& AlarmPanel::GetAlarmTime() const
{
    return m_DurationText;
}

void AlarmPanel::ShowAlarmDialog()
{
	m_showAlarmDialog = true;
}

void AlarmPanel::WaitForAlarmSemaphore()
{
    m_alarmSemaphore.acquire();
}

void AlarmPanel::UpdateAlarmsDisplay()
{
    int pos = 0;
    /* Copied out under the lock; the wx calls run outside it. The worker
       arms and fires entries while this ticks. */
    struct LabelState
    {
        wxString text;
        bool armed;
    };
    std::vector<LabelState> states;
    m_handler.WithModel([&](AlarmEntryHandler::Model& model)
    {
        states.reserve(model.entries.size());
        for(const auto& entry : model.entries)
            states.push_back({ wxString::Format("%s - %s", entry->name,
                utils::SecondsToHms(entry->duration.count())), entry->is_armed });
    });

    for (auto& i : alarms)
    {
        if(pos >= states.size())
            break;
        i->SetLabelText(states[pos].text);
        i->SetForegroundColour(states[pos].armed ? *wxRED : *wxBLACK);
        pos++;
    }
}

void AlarmPanel::On10MsTimer()
{
    if(m_showAlarmDialog)
	{
		m_showAlarmDialog = false;
        ShowAlarmDialogInternal();
	}

    UpdateAlarmsDisplay();
}

void AlarmPanel::OnRightClick(wxMouseEvent& event)
{
    auto obj = event.GetEventObject();

    wxStaticText* text = dynamic_cast<wxStaticText*>(obj);
    if (text == nullptr)
    {
        LOG(LogLevel::Error, "text is nullptr");
        return;
    }

    void* clientdata = text->GetClientData();
    if (clientdata == nullptr)
    {
        LOG(LogLevel::Error, "clientdata is nullptr");
        return;
    }

    AlarmEntry* e = reinterpret_cast<AlarmEntry*>(clientdata);

    const gui::MenuEntry entries[]{
        gui::MenuCommand{ "&Trigger", [this, e] { m_handler.SetupAlarm(e); }, wxART_EDIT },
        gui::MenuCommand{ "&Cancel", [this, e] { m_handler.CancelAlarm(e); }, wxART_DELETE },
    };
    gui::RunContextMenu(this, entries);
    event.Skip();
}