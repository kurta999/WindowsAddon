#include "pch.hpp"
#include "gui/LogRecordingBar.hpp"

#include <chrono>
#include <format>
#include <utility>

namespace gui
{
LogRecordingBar::LogRecordingBar(wxWindow* parent, wxSizer* row, Spec spec) :
    m_Spec(std::move(spec)), m_AutoScroll(m_Spec.auto_scroll)
{
    const auto button = [&](const char* label, const std::string& tooltip,
        const std::function<void()>& action)
    {
        wxButton* control = new wxButton(parent, wxID_ANY, label, wxDefaultPosition, wxDefaultSize);
        control->SetToolTip(tooltip);
        control->Bind(wxEVT_BUTTON, [action](wxCommandEvent&) { action(); });
        row->Add(control);
        return control;
    };

    button("Record", "Start recording for received & sent " + m_Spec.subject, m_Spec.start);
    button("Pause", "Suspend recording for received & sent " + m_Spec.subject, m_Spec.pause);
    button("Stop", "Suspend recording for received & sent " + m_Spec.subject + ", clear everything",
        m_Spec.stop);
    button("Clear", "Clear recording and reset frame counters", m_Spec.clear);

    m_AutoScrollBtn = new wxButton(parent, wxID_ANY, wxT("Toggle auto-scroll"),
        wxDefaultPosition, wxDefaultSize, 0);
    m_AutoScrollBtn->SetToolTip("Toggle auto-scroll");
    m_AutoScrollBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
        {
            m_AutoScroll = !m_AutoScroll;
            PaintAutoScrollButton();
        });
    row->AddSpacer(35);
    row->Add(m_AutoScrollBtn);
    /* The copies left the button in its default colour whatever the initial
       state, so the one panel that started with auto-scroll off looked
       exactly like the ones that started with it on. */
    PaintAutoScrollButton();

    m_Save = new wxButton(parent, wxID_ANY, "Save log", wxDefaultPosition, wxDefaultSize);
    m_Save->SetToolTip(m_Spec.save_tooltip);
    m_Save->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
        {
#ifdef _WIN32
            const auto now = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());

            if(!std::filesystem::exists(m_Spec.save_directory))
                std::filesystem::create_directory(m_Spec.save_directory);
            m_Spec.save(std::filesystem::path(std::format("{}/{}_{:%Y.%m.%d_%H_%M_%OS}.csv",
                m_Spec.save_directory, m_Spec.save_prefix, now)));
#endif
        });
}

void LogRecordingBar::PaintAutoScrollButton()
{
    m_AutoScrollBtn->SetBackgroundColour(m_AutoScroll ? wxNullColour : *wxRED);
}
}
