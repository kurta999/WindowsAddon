#include "pch.hpp"

#include "BackupProgressDialog.hpp"

#include "DirectoryBackup.hpp"
#include "Logger.hpp"

namespace
{
constexpr const char* kWaitMessage =
    "Please wait while files being backed up\nIt can take a few minutes...Be patient";
}

namespace gui
{
void BackupProgressDialog::Tick()
{
    DirectoryBackup* backup = &m_backups;

    const bool wanted = IsWanted();

    if(wanted && m_dialog == nullptr && !backup->IsCancelled())
    {
        m_dialog = new wxProgressDialog("Backing up files", kWaitMessage, 100, m_parent,
            wxPD_CAN_ABORT | wxPD_ELAPSED_TIME | wxPD_SMOOTH);
        m_dialog->Show();
    }

    if(m_dialog != nullptr)
    {
        try
        {
            const std::string current_file = backup->GetCurrentFile();
            if(!current_file.empty())
                m_dialog->Pulse(wxString::Format("%s\nCurrent file: %s", kWaitMessage, current_file));

            if(m_dialog->WasCancelled())
            {
                Destroy();
                backup->RequestCancel();
                return;
            }
        }
        catch(const std::exception& e)
        {
            LOG(LogLevel::Error, "Exception: {}", e.what());
        }
    }

    if(!wanted && m_dialog != nullptr)
        Destroy();
}

void BackupProgressDialog::Destroy()
{
    if(m_dialog == nullptr)
        return;

    m_dialog->Destroy();
    m_dialog = nullptr;
}
}
