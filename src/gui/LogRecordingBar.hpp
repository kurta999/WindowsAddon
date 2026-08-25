#pragma once

#include <wx/wx.h>

#include <filesystem>
#include <functional>
#include <string>

namespace gui
{
// !\brief The Record / Pause / Stop / Clear / auto-scroll / Save controls that
// every frame-log panel carries.
//
// CanLogPanel, ModbusLogPanel and ModbusSpecialRegisterPanel each built this
// row for themselves, line for line - thirty-odd lines apiece differing only
// by the word "CAN" or "Modbus" in a tooltip, which directory the timestamped
// save lands in, and which handler method each button calls. The copies had
// already drifted: one panel's auto-scroll defaulted off while the others
// defaulted on, and none of them painted the button to say so until the first
// click.
class LogRecordingBar
{
public:
    struct Spec
    {
        // !\brief What is being recorded, as it reads in a tooltip -
        // "CAN frames", "Modbus frames".
        std::string subject;

        std::function<void()> start;
        std::function<void()> pause;
        // !\brief Stop also clears the recording in every handler; the panel
        // adds its own grid reset in `clear`.
        std::function<void()> stop;
        std::function<void()> clear;

        // !\brief Where the timestamped CSV lands and what its name starts
        // with; the directory is created when missing.
        std::string save_directory;
        std::string save_prefix;
        std::string save_tooltip = "Save recording to file";
        std::function<void(std::filesystem::path)> save;

        bool auto_scroll = true;
    };

    // !\brief Create the buttons as children of `parent` and lay the
    // Record..auto-scroll run into `row`. The save button is created but not
    // placed - where it sits is the one thing the three panels legitimately
    // did differently.
    LogRecordingBar(wxWindow* parent, wxSizer* row, Spec spec);

    [[nodiscard]] wxButton* SaveButton() const { return m_Save; }

    // !\brief Whether the panel should keep the newest row in view.
    [[nodiscard]] bool IsAutoScroll() const { return m_AutoScroll; }

private:
    void PaintAutoScrollButton();

    Spec m_Spec;
    wxButton* m_AutoScrollBtn = nullptr;
    wxButton* m_Save = nullptr;
    bool m_AutoScroll = true;
};
}
