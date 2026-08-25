#pragma once

#include <wx/progdlg.h>
#include <wx/window.h>

#include <atomic>

class DirectoryBackup;

// !\brief The "Backing up files" progress dialog and its lifetime.
//
// The dialog pointer, the flag that asks for it and the thirty lines that
// reconciled the two lived on MyFrame, which is neither where the state belongs
// nor something a backup needs to know about. The frame drives this on its
// timer like anything else.
namespace gui
{
class BackupProgressDialog
{
public:
    BackupProgressDialog(wxWindow* parent, DirectoryBackup& backups) :
        m_parent(parent), m_backups(backups) {}
    ~BackupProgressDialog() { Destroy(); }

    BackupProgressDialog(const BackupProgressDialog&) = delete;
    BackupProgressDialog& operator=(const BackupProgressDialog&) = delete;

    // !\brief Ask for the dialog to be shown or taken down. Safe to call from
    // the backup worker; the dialog itself is only touched by Tick().
    void SetWanted(bool wanted) { m_wanted.store(wanted, std::memory_order_relaxed); }
    [[nodiscard]] bool IsWanted() const { return m_wanted.load(std::memory_order_relaxed); }

    // !\brief Reconcile the dialog with what was asked for, and pulse it.
    // Called on the UI thread only.
    void Tick();

private:
    void Destroy();

    wxWindow* m_parent = nullptr;
    DirectoryBackup& m_backups;
    wxProgressDialog* m_dialog = nullptr;
    std::atomic<bool> m_wanted{ false };
};
}
