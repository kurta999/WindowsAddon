#pragma once

#include "AppNotification.hpp"

#include <wx/wx.h>
#include <wx/notifmsg.h>
#include <wx/generic/notifmsg.h>

namespace gui
{
// !\brief Turns an application notification into a toast, and gives the toast
// its click action.
//
// This catalogue - nine handlers and the Show helper, MainFrame's largest
// single block at ~190 lines - lived inside the frame, so the frame owned
// every notification text and every ShellExecute a click performs. The
// frame keeps what is genuinely its own: the queue, its mutex, and the
// marshal to the UI thread (ADR-0002); this class owns what happens once a
// notification is on that thread.
class NotificationPresenter
{
public:
    // !\brief `owner` parents the toast windows; it must outlive the presenter.
    explicit NotificationPresenter(wxWindow* owner) : m_Owner(owner) {}

    // !\brief Show one notification. Must be called on the UI thread.
    void Present(const AppNotification& notification);

private:
    void Present(const SimpleNotification& notification);
    void Present(const FileSavedNotification& notification);
    void Present(const PathSeparatorsReplacedNotification& notification);
    void Present(const BackupCompletedNotification& notification);
    void Present(const BackupFailedNotification& notification);
    void Present(const AlarmSetupNotification& notification);
    void Present(const AlarmTriggeredNotification& notification);
    void Present(const WorktimeToggledNotification& notification);

    // !\brief Was MyFrame::ShowNotificaiton, typo and all.
    template<typename T> void Show(const wxString& title, const wxString& message,
        int timeout, int flags, T&& fptr);

    wxWindow* m_Owner;
};
}
