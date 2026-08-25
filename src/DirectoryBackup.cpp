#include "pch_core.hpp"
#include "DirectoryBackup.hpp"
#include "DirectoryBackupCore.hpp"
#include "Logger.hpp"
#include "platform/StandardBackupFileSystem.hpp"
#include "Utils.hpp"
#include "SettingsReader.hpp"
#include "SettingsWriter.hpp"
#include <ostream>
#include "BackupSettings.hpp"

int BackupEntry::ClampMaxBackups(long long configured)
{
	if(configured <= 0)
		return 0;  /* unlimited */
	return static_cast<int>(std::min<long long>(configured, max_backups_limit));
}

BackupEntry::BackupEntry(std::filesystem::path&& from_, std::vector<std::filesystem::path>&& to_, std::vector<std::string>&& ignore_list_, int max_backups_,
	bool compress_, bool calculate_hash_, size_t hash_buf_size_) :
	from(std::move(from_)), to(std::move(to_)), ignore_list(std::move(ignore_list_)), max_backups(ClampMaxBackups(max_backups_)),
	calculate_hash(calculate_hash_), m_Compress(compress_), hash_buf_size(hash_buf_size_)
{
	/* No filesystem check here: a plain value should be constructible without a
	   filesystem to consult. IsValid is called by LoadEntry and by BackupFile. */
}

IBackupFileSystem& DirectoryBackup::FileSystem() const noexcept
{
	/* The real filesystem is the default so nothing has to be wired up for the
	   application to work; a test replaces it. */
	static StandardBackupFileSystem standard;
	return m_FileSystem ? *m_FileSystem : standard;
}

bool BackupEntry::IsValid(const IBackupFileSystem& fs) const
{
	if(!fs.Exists(from))
	{
		LOG(LogLevel::Error, "Backup source directory \"{}\" doesn't exists!", from.generic_string());
		return false;
	}

	if(fs.IsRegularFile(from))
	{
		LOG(LogLevel::Error, "Backup source directory \"{}\" is a regular file, it should be directory!", from.generic_string());
		return false;
	}
	return true;
}

void DirectoryBackup::Init()
{

}

void DirectoryBackup::LoadEntry(const std::string& from, const std::string& to, const std::string& ignore, int max_backups, bool compress_, bool calculate_hash, size_t buffer_size)
{
	std::filesystem::path from_path = from;

	std::vector<std::filesystem::path> to_path;
	boost::split(to_path, to, [](char input) { return input == '|'; }, boost::algorithm::token_compress_on);

	std::vector<std::string> ignore_list;
	boost::split(ignore_list, ignore, [](char input) { return input == '|'; }, boost::algorithm::token_compress_on);

	BackupEntry entry(std::move(from_path), std::move(to_path), std::move(ignore_list),
		max_backups, compress_, calculate_hash, buffer_size);

	/* Report an unusable source while the configuration is being read rather
	   than leaving the first backup attempt to discover it. */
	(void)entry.IsValid(FileSystem());

	AddEntry(std::move(entry));
}

std::size_t DirectoryBackup::AddEntry(BackupEntry entry)
{
	std::scoped_lock lock(m_StateMutex);
	m_Backups.push_back(std::move(entry));
	return m_Backups.size() - 1;
}

bool DirectoryBackup::RemoveEntry(std::size_t id)
{
	std::scoped_lock lock(m_StateMutex);
	if(id >= m_Backups.size())
		return false;
	m_Backups.erase(m_Backups.begin() + static_cast<std::ptrdiff_t>(id));
	return true;
}

bool DirectoryBackup::UpdateEntry(std::size_t id, BackupEntry entry)
{
	std::scoped_lock lock(m_StateMutex);
	if(id >= m_Backups.size())
		return false;
	m_Backups[id] = std::move(entry);
	return true;
}

std::optional<BackupEntry> DirectoryBackup::GetEntry(std::size_t id) const
{
	std::scoped_lock lock(m_StateMutex);
	if(id >= m_Backups.size())
		return std::nullopt;
	return m_Backups[id];
}

std::vector<BackupEntry> DirectoryBackup::GetEntries() const
{
	std::scoped_lock lock(m_StateMutex);
	return m_Backups;
}

void DirectoryBackup::SetBackupTimeFormat(std::string format)
{
	std::scoped_lock lock(m_StateMutex);
	m_BackupTimeFormat = std::move(format);
}

std::string DirectoryBackup::GetBackupTimeFormat() const
{
	std::scoped_lock lock(m_StateMutex);
	return m_BackupTimeFormat;
}

void DirectoryBackup::SetCompressor(Compressor compressor)
{
	std::scoped_lock lock(m_StateMutex);
	m_Compressor = std::move(compressor);
}

DirectoryBackup::Compressor DirectoryBackup::GetCompressor()
{
	{
		std::scoped_lock lock(m_StateMutex);
		if(m_Compressor)
			return m_Compressor;
	}
	return [this](const std::filesystem::path& destination) { return CompressBackup(destination); };
}

std::string DirectoryBackup::GetCurrentFile() const
{
	std::scoped_lock lock(m_StateMutex);
	return m_CurrentFile;
}

void DirectoryBackup::SetCurrentFile(std::string current_file)
{
	std::scoped_lock lock(m_StateMutex);
	m_CurrentFile = std::move(current_file);
}

DirectoryBackup::~DirectoryBackup()
{
	/* The worker reads members that are about to be destroyed, so it is stopped
	   and joined before any of them go. Nothing else may hold m_WorkerMutex by
	   the time a singleton is being deleted. */
	RequestCancel();
	{
		std::scoped_lock lock(m_WorkerMutex);
		m_PendingBackups.clear();
	}
	if(backup_future.valid())
		backup_future.wait();
}

void DirectoryBackup::BackupFile(int id)
{
	if(id < 0)
		return;

	auto backup = GetEntry(static_cast<std::size_t>(id));
	if(!backup)
		return;

	std::scoped_lock lock(m_WorkerMutex);
	m_PendingBackups.push_back(std::move(*backup));
	if(m_WorkerRunning)
		return;  /* the worker that is running will pick this up */

	/* The previous worker has already returned, so this only reaps its thread
	   and cannot wait for the length of a backup. */
	if(backup_future.valid())
		backup_future.wait();

	m_WorkerRunning = true;
	backup_future = std::async(std::launch::async, [this] { RunQueuedBackups(); });
}

void DirectoryBackup::RunQueuedBackups()
{
	for(;;)
	{
		std::optional<BackupEntry> next;
		{
			std::scoped_lock lock(m_WorkerMutex);
			if(m_PendingBackups.empty())
			{
				m_WorkerRunning = false;
				return;
			}
			next = std::move(m_PendingBackups.front());
			m_PendingBackups.pop_front();
		}

		/* Nobody waits on this future, so an escaping exception would be
		   swallowed when it is replaced. Report it instead. */
		try
		{
			DoBackup(*next);
		}
		catch(const std::exception& e)
		{
			LOG(LogLevel::Critical, "Backup threw: {}", e.what());
		}
	}
}

bool DirectoryBackup::IsInProgress() const
{
	std::scoped_lock lock(m_WorkerMutex);
	return m_WorkerRunning || !m_PendingBackups.empty();
}

void DirectoryBackup::Clear()
{
	std::scoped_lock lock(m_StateMutex);
	m_Backups.clear();
}

void DirectoryBackup::DoBackup(const BackupEntry& backup)
{
	const auto started_at = std::chrono::steady_clock::now();

	if(!backup.IsValid(FileSystem()))
		return;
	if(auto* event_sink = m_EventSink.load(std::memory_order_acquire))
		event_sink->OnBackupStarted();
	m_IsCancelled = false;
	BackupRotation(backup);

	/* Copied rather than referenced: the entry is a local copy of the caller's,
	   but the options outlive this frame's use of it. */
	const std::vector<std::string> ignore_rules = backup.ignore_list;

	/* The source is deliberately not measured up front any more: the copy
	   reports what it actually wrote, which is the only figure that stays true
	   when a backup is cancelled or fails part way, and measuring cost a second
	   full walk of the tree. */

	std::filesystem::path backup_name = backup.from.filename();
	const auto backup_time_format = GetBackupTimeFormat();
	if(!backup_time_format.empty())
	{
		time_t current_time;
		time(&current_time);
		std::tm* now = std::localtime(&current_time);
		char datetime[64];
		strftime(datetime, sizeof(datetime), backup_time_format.c_str(), now);
		backup_name += datetime;
	}
	else
	{
		LOG(LogLevel::Warning, "Backup time isn't set, backup functionality might not work as expected!");
	}

	/* The progress text feeds a tray tooltip, so it is refreshed every few
	   seconds rather than once per file. */
	auto last_progress = std::chrono::steady_clock::now();
	const auto report_progress = [this, &last_progress](const std::filesystem::path& source_path) {
		const auto now = std::chrono::steady_clock::now();
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_progress).count();
		if(elapsed > 3500 || GetCurrentFile().empty())
		{
			SetCurrentFile(source_path.generic_string());
			last_progress = now;
		}
	};

	backup_core::Options options;
	options.ignore_rules = ignore_rules;
	options.cancelled = &m_IsCancelled;
	options.verify_hash = backup.calculate_hash;
	options.hash_buffer_bytes = backup.hash_buf_size * 1024 * 1024;
	options.after_copy_file = [&report_progress](const std::filesystem::path& source_path, const std::filesystem::path&) {
		report_progress(source_path);
	};
	options.after_create_directory = [this, &report_progress](const std::filesystem::path& source_path, const std::filesystem::path& destination_path) {
		report_progress(source_path);
		RestoreAttributes(source_path, destination_path);
	};
	if(backup.m_Compress)
	{
		options.compressor = [this, compressor = GetCompressor()](const std::filesystem::path& destination_dir) {
			SetCurrentFile("Compressing");
			return compressor(destination_dir);
		};
	}

	const auto results = backup_core::CopyToDestinations(backup.from, backup.to, backup_name, options);

	/* A backup with nowhere to go did not succeed, it just had nothing to do. */
	bool fail = backup.to.empty();
	if(fail)
		LOG(LogLevel::Error, "Backup entry for \"{}\" has no destination", backup.from.generic_string());

	size_t dest_count = 0;
	const backup_core::DestinationResult* copied = nullptr;
	const backup_core::DestinationResult* first_failure = nullptr;
	for(const auto& result : results)
	{
		if(result.success)
		{
			++dest_count;
			if(!copied)
				copied = &result;
			continue;
		}

		fail = true;
		if(!first_failure)
			first_failure = &result;
		if(result.cancelled)
			LOG(LogLevel::Normal, "Backup was cancelled by user");
		else if(result.hash_mismatch)
			LOG(LogLevel::Critical, "Hash mismatch, destination dir: {}", result.destination.generic_string());
		else
			LOG(LogLevel::Error, "Backup to \"{}\" failed: {}", result.destination.generic_string(), result.error);
	}

	const auto finished_at = std::chrono::steady_clock::now();
	if(auto* event_sink = m_EventSink.load(std::memory_order_acquire))
	{
		BackupSummary summary;
		summary.success = !fail && !m_IsCancelled;
		summary.duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(finished_at - started_at).count();

		/* What one finished backup holds, rather than what the source was
		   measured at - a cancelled run would otherwise report the whole
		   source as though it had been copied. */
		if(copied)
		{
			summary.file_count = copied->file_count;
			summary.bytes_copied = static_cast<size_t>(copied->bytes);
		}
		summary.destination_count = dest_count;

		/* On a failure the notification names this destination, so it has to be
		   the one that actually failed. */
		if(first_failure)
			summary.destination = first_failure->destination;
		else if(!backup.to.empty())
			summary.destination = backup.to.front();

		event_sink->OnBackupFinished(summary);
	}
	SetCurrentFile({});
}

void DirectoryBackup::BackupRotation(const BackupEntry& backup)
{
	const auto backup_time_format = GetBackupTimeFormat();

	/* Rotation recognises its own backups by the timestamp in their name. A
	   format it cannot read back means every backup is kept for ever, which is
	   worth saying out loud rather than discovering when the disk fills. */
	if(backup.max_backups > 0 && !backup_core::IsRotatableTimeFormat(backup_time_format))
	{
		LOG(LogLevel::Warning,
			"Backup time format \"{}\" cannot be recognised again, so backups of \"{}\" will not be rotated",
			backup_time_format, backup.from.generic_string());
	}

	for(auto& t : backup.to)
	{
		if(!FileSystem().Exists(t))  /* not needed to go file checking when even the directory doesns't exists */
		{
			std::error_code ec;
			if(!FileSystem().CreateDirectories(t, ec))
			{
				LOG(LogLevel::Error, "Error with create_directory ({}): {}", t.generic_string(), ec.message());
			}
			continue;
		}

		if(!backup_core::RotateOwnedBackups(t, backup.from.filename().string(), backup.max_backups,
			backup_time_format))
			LOG(LogLevel::Error, "Failed to rotate one or more backups in {}", t.generic_string());
	}
}

void DirectoryBackup::RestoreAttributes(const std::filesystem::path& src, const std::filesystem::path& dst)
{
#ifdef _WIN32
	const DWORD attributes = GetFileAttributesW(src.generic_wstring().c_str());
	if(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0)
		SetFileAttributesW(dst.generic_wstring().c_str(), attributes);
#else
	(void)src;
	(void)dst;
#endif
}

bool DirectoryBackup::CompressBackup(const std::filesystem::path& dst)
{
	bool ret = true;

	std::string cmdline = std::format("7z a \"{}\" \"{}\"", dst.generic_string(), dst.generic_string());

	/* The uncompressed directory is removed by the caller once the archive is
	   confirmed, so a failed compression leaves the copy in place. */
	std::string result = utils::exec(cmdline.c_str());
	if(result.find("Everything is Ok") == std::string::npos)
	{
		LOG(LogLevel::Error, "Failed to compress backup with command line arguments: {}", cmdline);
		ret = false;
	}
	return ret;
}

void DirectoryBackup::LoadSettings(SettingsReader& reader)
{
    SetBackupTimeFormat(reader.Required("BackupSettings", "BackupFileFormat"));

    Clear();
    for(std::size_t counter = 1; reader.CountSection("Backup_" + std::to_string(counter)) == 1; ++counter)
    {
        const std::string section = "Backup_" + std::to_string(counter);
        backup_settings::RawEntry raw;
        raw.from = reader.Required(section, "From");
        raw.to = reader.Required(section, "To");
        raw.ignore = reader.Required(section, "Ignore");
        raw.max_backups = reader.Required(section, "MaxBackups");
        raw.compress = reader.Required(section, "Compress");
        raw.calculate_hash = reader.Required(section, "CalculateHash");
        raw.buffer_size = reader.Required(section, "BufferSize");
        reader.TrackSection(section);
        backup_settings::AddEntry(*this, raw);
    }
}

namespace
{
constexpr std::string_view kBufferSizeNote =
    "Buffer size for file operations - determines how much data is read once, Unit: Megabytes";
}

void DirectoryBackup::SaveSettings(std::ostream& out) const
{
    SettingsWriter writer(out, "BackupSettings");
    writer.Key("BackupFileFormat", GetBackupTimeFormat());

    int cnt = 1;
    for(const auto& entry : GetEntries())
    {
        const auto raw = backup_settings::Format(entry);
        writer.Blank().Section(std::format("Backup_{}", cnt++))
            .Key("From", raw.from)
            .Key("To", raw.to)
            .Key("Ignore", raw.ignore)
            .Key("MaxBackups", raw.max_backups)
            .Key("Compress", raw.compress)
            .Key("CalculateHash", raw.calculate_hash)
            .Key("BufferSize", raw.buffer_size, kBufferSizeNote);
    }
    writer.Blank();
}

void DirectoryBackup::SaveDefaultSettings(std::ostream& out) const
{
    SettingsWriter writer(out, "BackupSettings");
    writer.Key("BackupFileFormat", GetBackupTimeFormat())
        .Blank()
        .Section("Backup_1")
        .Key("From", R"(C:\Users\Ati\Desktop\folder_from_backup)")
        .Key("To", R"(C:\Users\Ati\Desktop\folder_where_to_backup|F:\Backup\folder_where_to_backup)")
        .Key("Ignore", "git/COMMIT_EDITMSG|.git|.vs|Debug|Release|Screenshots|x64|Graphs/Line Chart|Graphs/Temperature.html|Graphs/Humidity.html|Graphs/CO2.html|Graphs/Lux.html|Graphs/VOC.html|Graphs/CCT.html|Graphs/PM10.html|Graphs/PM25.html")
        .Key("MaxBackups", 5)
        .Key("Compress", 0)
        .Key("CalculateHash", 1)
        .Key("BufferSize", 2)
        .Blank();
}
