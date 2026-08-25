#include "pch.hpp"
#include "gui/NotificationPresenter.hpp"

#include "Utils.hpp"

#ifdef _WIN32
#include <shlwapi.h>
#endif

namespace gui
{
void NotificationPresenter::Present(const AppNotification& notification)
{
	std::visit([this](const auto& value) { Present(value); }, notification);
}

void NotificationPresenter::Present(const SimpleNotification& notification)
{
	wxString title;
	wxString message;
	int icon = wxICON_INFORMATION;
	switch(notification.kind)
	{
		case SimpleNotificationKind::ScreenshotSaveFailed:
			title = "Failed to save the screenshot!";
			message = "An error occurred while saving the screenshot.";
			icon = wxICON_ERROR;
			break;
		case SimpleNotificationKind::SettingsSaved:
			title = "Settings saved";
			message = "Settings have been successfully saved";
			break;
		case SimpleNotificationKind::StringEscaped:
			title = "String escaped";
			message = "String has been escaped and placed on the clipboard";
			break;
		case SimpleNotificationKind::TxListLoaded: title = message = "TX List Loaded"; break;
		case SimpleNotificationKind::TxListSaved: title = message = "TX List Saved"; break;
		case SimpleNotificationKind::RxListLoaded: title = message = "RX List Loaded"; break;
		case SimpleNotificationKind::RxListSaved: title = message = "RX List Saved"; break;
		case SimpleNotificationKind::FrameMappingLoaded: title = message = "Frame Mapping Loaded"; break;
		case SimpleNotificationKind::FrameMappingSaved: title = message = "Frame Mapping Saved"; break;
		case SimpleNotificationKind::TxListLoadError:
			title = message = "TX List Load failed"; icon = wxICON_ERROR; break;
		case SimpleNotificationKind::RxListLoadError:
			title = message = "RX List Load failed"; icon = wxICON_ERROR; break;
		case SimpleNotificationKind::FrameMappingLoadError:
			title = message = "Frame Mapping Load failed"; icon = wxICON_ERROR; break;
		case SimpleNotificationKind::TxListSaveError:
			title = message = "TX List Save failed"; icon = wxICON_ERROR; break;
		case SimpleNotificationKind::RxListSaveError:
			title = message = "RX List Save failed"; icon = wxICON_ERROR; break;
		case SimpleNotificationKind::FrameMappingSaveError:
			title = message = "Frame Mapping Save failed"; icon = wxICON_ERROR; break;
		case SimpleNotificationKind::DidUpdated:
			title = "DID updated"; message = "DID value has been updated!"; break;
		case SimpleNotificationKind::SelectedLogsCopied:
			title = "Logs copied"; message = "Selected logs copied to clipboard"; break;
		case SimpleNotificationKind::EverythingSaved:
			title = "Configurations saved"; message = "Every configuration has been saved"; break;
	}

	Show(title, message, 3, icon, [kind = notification.kind](wxCommandEvent&)
	{
#ifdef _WIN32
		if(kind == SimpleNotificationKind::SettingsSaved)
		{
			wchar_t work_dir[1024]{};
			GetCurrentDirectoryW(WXSIZEOF(work_dir) - 1, work_dir);
			StrCatW(work_dir, L"\\settings.ini");
			ShellExecuteW(nullptr, L"open", work_dir, nullptr, nullptr, SW_SHOW);
		}
		else if(kind == SimpleNotificationKind::EverythingSaved)
		{
			wchar_t work_dir[1024]{};
			GetCurrentDirectoryW(WXSIZEOF(work_dir) - 1, work_dir);
			ShellExecuteW(nullptr, nullptr, work_dir, nullptr, nullptr, SW_SHOWNORMAL);
		}
#endif
	});
}

void NotificationPresenter::Present(const FileSavedNotification& notification)
{
	wxString title;
	wxString subject;
	bool path_is_relative = false;
	switch(notification.kind)
	{
		case SavedFileKind::Screenshot: title = "Screenshot saved"; subject = "Screenshot"; break;
		case SavedFileKind::CanLog: title = "CAN Log saved"; subject = "CAN log"; path_is_relative = true; break;
		case SavedFileKind::ModbusLog: title = "Modbus Log saved"; subject = "Modbus log"; path_is_relative = true; break;
		case SavedFileKind::Commands: title = "Commands saved"; subject = "Commands"; break;
		case SavedFileKind::DidCache: title = "DIDs cache saved"; subject = "DIDs cache"; break;
	}

	Show(title, wxString::Format("%s saved in %.3fms\nPath: %s", subject,
		static_cast<double>(notification.duration_ns) / 1'000'000.0, notification.filename),
		3, wxICON_INFORMATION, [filename = notification.filename, path_is_relative](wxCommandEvent&)
	{
#ifdef _WIN32
		std::string selected_file = filename;
		if(path_is_relative)
		{
			char work_dir[1024]{};
			GetCurrentDirectoryA(sizeof(work_dir) - 1, work_dir);
			selected_file = std::string(work_dir) + "\\" + selected_file;
			boost::algorithm::replace_all(selected_file, "/", "\\");
		}
		const std::string command_line = "/select,\"" + selected_file + "\"";
		ShellExecuteA(nullptr, "open", "explorer.exe", command_line.c_str(), nullptr, SW_NORMAL);
#endif
	});
}

void NotificationPresenter::Present(const PathSeparatorsReplacedNotification& notification)
{
	Show("Path separator replaced",
		wxString::Format("New form is in the clipboard:\n%s", notification.path.substr(0, 64)),
		3, wxICON_INFORMATION, [](wxCommandEvent&) {});
}

void NotificationPresenter::Present(const BackupCompletedNotification& notification)
{
	Show("Backup complete",
		wxString::Format("Backed up %zu files (%s) to %zu places in %.3fms", notification.file_count,
			utils::GetDataUnit(notification.bytes_copied), notification.destination_count,
			static_cast<double>(notification.duration_ns) / 1'000'000.0),
		3, wxICON_INFORMATION, [destination = notification.destination](wxCommandEvent&)
	{
#ifdef _WIN32
		ShellExecuteA(nullptr, nullptr, destination.generic_string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
	});
}

void NotificationPresenter::Present(const BackupFailedNotification& notification)
{
	Show("Backup failed!",
		"Backup failed due to wrong checksum values\nMake sure that your drive is not damaged\nCheck log file for more info",
		3, wxICON_ERROR, [destination = notification.destination](wxCommandEvent&)
	{
#ifdef _WIN32
		ShellExecuteA(nullptr, nullptr, destination.generic_string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
	});
}

void NotificationPresenter::Present(const AlarmSetupNotification& notification)
{
	Show(wxString::Format("Alarm setup - %s", notification.name),
		wxString::Format("Alarm has been set for %lld seconds", notification.duration.count()),
		3, wxICON_INFORMATION, [](wxCommandEvent&) {});
}

void NotificationPresenter::Present(const AlarmTriggeredNotification& notification)
{
	Show(wxString::Format("Alarm executed - %s", notification.name), "Alarm has been executed",
		3, wxICON_INFORMATION, [](wxCommandEvent&) {});
}

void NotificationPresenter::Present(const WorktimeToggledNotification& notification)
{
	const std::string state = notification.working ? "Started" : "Stopped";
	std::string details = wxString::Format("Worktime has been %s", state).ToStdString();
	if(notification.duration != std::chrono::seconds::zero())
	{
		const auto total_seconds = notification.duration.count();
		details = wxString::Format("Worktime has been %s\nDuration: %s", state,
			utils::SecondsToHms(static_cast<int>(total_seconds))).ToStdString();
	}
	Show(wxString::Format("Worktime - %s", state), details,
		3, wxICON_INFORMATION, [](wxCommandEvent&) {});
}

template<typename T> void NotificationPresenter::Show(const wxString& title, const wxString& message, int timeout, int flags, T&& fptr)
{
	wxNotificationMessageBase* m_notif = new wxGenericNotificationMessage(title, message, m_Owner, flags);
	m_notif->Show(timeout);
	m_notif->Bind(wxEVT_NOTIFICATION_MESSAGE_CLICK, fptr);
}
}
