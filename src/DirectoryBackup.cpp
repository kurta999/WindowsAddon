#include "pch.hpp"
#include "DirectoryBackupCore.hpp"

BackupEntry::BackupEntry(std::filesystem::path&& from_, std::vector<std::filesystem::path>&& to_, std::vector<std::wstring>&& ignore_list_, int max_backups_,
	bool compress_, bool calculate_hash_, size_t hash_buf_size_) :
	from(std::move(from_)), to(std::move(to_)), ignore_list(std::move(ignore_list_)), max_backups(max_backups_),
	calculate_hash(calculate_hash_), m_Compress(compress_), hash_buf_size(hash_buf_size_)
{
	if(!IsValid())
		return;
}

bool BackupEntry::IsValid() const
{
	if(!std::filesystem::exists(from))
	{
		LOG(LogLevel::Error, "Backup source directory \"{}\" doesn't exists!", from.generic_string());
		return false;
	}

	if(std::filesystem::is_regular_file(from))
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

	std::wstring ignore_w;
	utils::MBStringToWString(ignore, ignore_w);
	std::vector<std::wstring> ignore_list;
	boost::split(ignore_list, ignore_w, [](wchar_t input) { return input == L'|'; }, boost::algorithm::token_compress_on);

	AddEntry(BackupEntry(std::move(from_path), std::move(to_path), std::move(ignore_list),
		max_backups, compress_, calculate_hash, buffer_size));
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

void DirectoryBackup::BackupFile(int id)
{
	if(id >= 0)
	{
		auto backup = GetEntry(static_cast<std::size_t>(id));
		if(!backup)
			return;
		if(backup_future.valid())
			backup_future.get();
		backup_future = std::async(std::launch::async,
			[this, backup = std::move(*backup)] { DoBackup(backup); });
	}
}

bool DirectoryBackup::IsInProgress() const
{
	bool ret = false;
	if(backup_future.valid())
		ret = backup_future.wait_for(std::chrono::nanoseconds(1)) != std::future_status::ready;
	return ret;
}

void DirectoryBackup::Clear()
{
	std::scoped_lock lock(m_StateMutex);
	m_Backups.clear();
}

void DirectoryBackup::DoBackup(const BackupEntry& backup)
{
	std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
	auto folder_name = backup.from.filename();
	std::wstring folder_name_with_date = folder_name.generic_wstring();

	if(!backup.IsValid())
		return;
	if(auto* event_sink = m_EventSink.load(std::memory_order_acquire))
		event_sink->OnBackupStarted();
	m_IsCancelled = false;
	BackupRotation(backup);

	std::vector<std::string> ignore_rules;
	ignore_rules.reserve(backup.ignore_list.size());
	for(const auto& rule : backup.ignore_list)
		ignore_rules.push_back(std::filesystem::path(rule).generic_string());
	const auto source_metrics = backup_core::MeasureSource(backup.from, ignore_rules);
	if(!source_metrics.success)
		LOG(LogLevel::Warning, "Could not calculate complete backup metrics: {}", source_metrics.error);

	const auto backup_time_format = GetBackupTimeFormat();
	if(!backup_time_format.empty())
	{
		time_t current_time;
		time(&current_time);
		std::tm* now = std::localtime(&current_time);
		char datetime[64];
		strftime(datetime, sizeof(datetime), backup_time_format.c_str(), now);
		std::string date_tmp(datetime);

		folder_name_with_date += std::wstring(date_tmp.begin(), date_tmp.end());
	}
	else
	{
		LOG(LogLevel::Warning, "Backup time isn't set, backup functionality might not work as expected!");
	}

	std::unique_ptr<char[]> hash_buf = nullptr; 
	if(backup.calculate_hash)
		hash_buf = std::make_unique_for_overwrite<char[]>(backup.hash_buf_size * 1024 * 1024);

	bool first_run_hash = false;
	size_t file_count = source_metrics.file_count;
	size_t files_size = static_cast<size_t>(source_metrics.bytes);
	size_t dest_count = 0;
	SHA256_CTX ctx_from;
	SHA256_CTX ctx_to;
	uint8_t hash_from[SHA256_BLOCK_SIZE];
	uint8_t hash_tmp[SHA256_BLOCK_SIZE];
	if(backup.calculate_hash)
		sha256_init(&ctx_from);
	bool fail = false;

	std::chrono::steady_clock::time_point backup_start = std::chrono::steady_clock::now();
	for(auto& t : backup.to)
	{
		if(m_IsCancelled)
		{
			LOG(LogLevel::Normal, "Backup was cancelled by user");
			fail = true;
			break;
		}

		if(backup.calculate_hash)
			sha256_init(&ctx_to);
		std::filesystem::path destination_dir = t / folder_name_with_date;

		std::error_code ec;
		std::filesystem::create_directory(destination_dir, ec);
		if(ec)
		{
			LOG(LogLevel::Error, "Error with create_directory ({}): {}", destination_dir.generic_string(), ec.message());
			fail = true;
			break;
		}
		for(auto& p : std::filesystem::recursive_directory_iterator(backup.from))
		{
			if(m_IsCancelled)
			{
				LOG(LogLevel::Normal, "Backup was cancelled by user");
				fail = true;
				break;
			}

			auto rel_path = p.path().lexically_proximate(backup.from);
			bool is_file = std::filesystem::is_regular_file(p.path());

			if(backup_core::ShouldIgnore(rel_path, ignore_rules)) continue;

			std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
			int64_t dif = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - backup_start).count();

			if(dif > 3500 || GetCurrentFile().empty())
			{
				SetCurrentFile(p.path().generic_string());
				backup_start = std::chrono::steady_clock::now();
			}

			bool is_symlink = std::filesystem::is_symlink(p.path());
			if(is_symlink)
			{
				std::filesystem::path symlink_path;
				std::error_code ec;
				symlink_path = std::filesystem::read_symlink(p.path(), ec);
				if(ec)
				{
					LOG(LogLevel::Error, "Failed to read symlink ({}): {}", p.path().generic_string(), ec.message());
					fail = true;
					break;
				}

				std::filesystem::path destination_path = destination_dir / symlink_path;
				std::filesystem::path pointing_path = destination_dir / p.path().filename();

				auto path = std::filesystem::current_path();
				auto sim_base_path = pointing_path.lexically_relative(destination_dir);
				auto destination_symlink_path = path / destination_dir / sim_base_path;
				{
					std::error_code ec;
					std::filesystem::create_symlink(path / destination_path, destination_symlink_path, ec);
					if(ec)
					{
						LOG(LogLevel::Error, "Error with create_symlink ({}, {}): {}", destination_path.generic_string(), destination_symlink_path.generic_string(), ec.message());
						fail = true;
						break;
					}
				}
				continue;
			}

			if(!is_file)
			{
				std::filesystem::path destination_path = destination_dir / rel_path;

				std::error_code ec;
				std::filesystem::create_directory(destination_path, ec);
				if(ec)
				{
					LOG(LogLevel::Error, "Error with create_directory ({}): {}", destination_path.generic_string(), ec.message());
					fail = true;
					break;
				}

				RestoreAttributes(p, destination_path);
			}
			else
			{  
				std::filesystem::path destination_path = destination_dir / rel_path;

				std::error_code ec;
				std::filesystem::copy_file(p.path(), destination_path, ec);
				if(ec)
				{
					LOG(LogLevel::Error, "Error with copy_file ({}): {}", destination_path.generic_string(), ec.message());
					fail = true;
					break;
				}
				if(backup.calculate_hash && std::filesystem::is_regular_file(p.path()))
				{
					std::ifstream f(destination_path, std::ifstream::binary);
					if(f)
					{
						f.peek();
						while(f.good())
						{
							std::streamsize chars_read = f.read(hash_buf.get(), backup.hash_buf_size * 1024 * 1024).gcount();
							sha256_update(&ctx_to, reinterpret_cast<uint8_t*>(hash_buf.get()), chars_read);
						}
						f.close();
					}
					else
					{
						LOG(LogLevel::Error, "Failed to open file for calculating hash: \"{}\"", destination_path.generic_string());
					}
				}
			}
			
			if(!first_run_hash)  /* calculate source hash - only once */
			{
				if(backup.calculate_hash && std::filesystem::is_regular_file(p.path()))
				{
					std::ifstream f(p.path(), std::ifstream::binary);
					if(f)
					{
						f.peek();
						while(f.good())
						{
							std::streamsize chars_read = f.read(hash_buf.get(), backup.hash_buf_size * 1024 * 1024).gcount();
							sha256_update(&ctx_from, reinterpret_cast<uint8_t*>(hash_buf.get()), chars_read);
						}
						f.close();
					}
					else
					{
						LOG(LogLevel::Error, "Failed to open file for calculating hash: \"{}\"", p.path().generic_string());
					}
				}
			}
		}

		if(m_IsCancelled)
			break;

		if(backup.calculate_hash)
		{
			if(!first_run_hash)
			{
				first_run_hash = true;
				sha256_final(&ctx_from, hash_from);
			}

			sha256_final(&ctx_to, hash_tmp);
			if(memcmp(hash_from, hash_tmp, sizeof(hash_from)) != 0)
			{
				DBG("Hash mismatch\n");
				LOG(LogLevel::Critical, "Hash mismatch, destination dir: {}", t.generic_string());
				fail = true;
				break;
			}
		}

		if(backup.m_Compress && !fail)
		{
			SetCurrentFile("Compressing");
			if(!CompressBackup(destination_dir))
				fail = true;
		}
		if(!fail)
			dest_count++;
	}

	std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
	int64_t dif = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
	if(auto* event_sink = m_EventSink.load(std::memory_order_acquire))
	{
		BackupSummary summary;
		summary.success = !fail && !m_IsCancelled;
		summary.duration_ns = dif;
		summary.file_count = file_count;
		summary.bytes_copied = files_size;
		summary.destination_count = dest_count;
		if(!backup.to.empty())
			summary.destination = backup.to.front();
		event_sink->OnBackupFinished(summary);
	}
	SetCurrentFile({});
}

void DirectoryBackup::BackupRotation(const BackupEntry& backup)
{
	for(auto& t : backup.to)
	{
		if(!std::filesystem::exists(t))  /* not needed to go file checking when even the directory doesns't exists */
		{
			std::error_code ec;
			std::filesystem::create_directory(t, ec);
			if(ec)
			{
				LOG(LogLevel::Error, "Error with create_directory ({}): {}", t.generic_string(), ec.message());
			}
			continue;
		}

		if(!backup_core::RotateOwnedBackups(t, backup.from.filename().string(), backup.max_backups))
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

	std::string result = utils::exec(cmdline.c_str());
	if(result.find("Everything is Ok") != std::string::npos)
	{
		std::filesystem::remove_all(dst);
	}
	else
	{
		LOG(LogLevel::Error, "Failed to compress backup with command line arguments: {}", cmdline);
		ret = false;
	}
	return ret;
}
