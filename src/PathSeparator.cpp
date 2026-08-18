#include "pch.hpp"

void PathSeparator::ReplaceClipboard(ReplaceType type)
{
	if(wxTheClipboard->Open())
	{
		if(wxTheClipboard->IsSupported(wxDF_TEXT))
		{
			wxTextDataObject data;
			wxTheClipboard->GetData(data);
			std::string input(data.GetText());

			switch(type)
			{
				case ReplaceType::PATH_SEPARATOR:
				{
					ReplaceString(input);
					break;
				}				
				case ReplaceType::WSL:
				{
					ReplaceStringFromWindowsToWsl(input);
					break;
				}
			}

			wxTheClipboard->SetData(new wxTextDataObject(input));
			MyFrame* frame = static_cast<MyFrame*>(wxGetApp().GetTopWindow());
			{
				frame->PostNotification(PathSeparatorsReplacedNotification{std::move(input)});
			}
		}
		wxTheClipboard->Close();
	}
}

void PathSeparator::ReplaceString(std::string& str)
{
	if(str.find('\\') != std::string::npos)
		boost::algorithm::replace_all(str, "\\", "/");
	else
		boost::algorithm::replace_all(str, "/", "\\");
}

void PathSeparator::ReplaceStringFromWindowsToWsl(std::string& str)
{
	if(str.find("/mnt/") != std::string::npos)
	{
		boost::algorithm::replace_all(str, "/", "\\");
		str.erase(0, 5); // strip leading \mnt\ prefix (slashes already replaced above)
		if(!str.empty())
		{
			str[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(str[0])));
			str.insert(1, ":");
		}
	}
	else
	{
		boost::algorithm::replace_all(str, "\\", "/");
	}
}
