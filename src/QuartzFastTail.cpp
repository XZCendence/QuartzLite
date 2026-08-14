#include "QuartzFastTail.h"
#include "QuartzTypes.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
	constexpr auto PollInterval = std::chrono::milliseconds(50);

#ifdef _WIN32
	// paths arrive as utf-8. the narrow crt entry points expect the ansi code page
	std::wstring PathToWide(const char* Path)
	{
		const int Len = ::MultiByteToWideChar(CP_UTF8, 0, Path, -1, nullptr, 0);
		std::wstring Out(Len > 0 ? Len - 1 : 0, L'\0');
		if (Len > 1)
		{
			::MultiByteToWideChar(CP_UTF8, 0, Path, -1, &Out[0], Len);
		}
		return Out;
	}
#endif

	FILE* OpenLogFile(const char* Path)
	{
#ifdef _WIN32
		return _wfopen(PathToWide(Path).c_str(), L"rb");
#else
		return fopen(Path, "rb");
#endif
	}

	bool GetLogFileSize(const char* Path, int64& OutSize)
	{
#ifdef _WIN32
		struct __stat64 Stat;
		if (_wstat64(PathToWide(Path).c_str(), &Stat) != 0)
		{
			return false;
		}
#else
		struct stat Stat;
		if (stat(Path, &Stat) != 0)
		{
			return false;
		}
#endif
		OutSize = (int64)Stat.st_size;
		return true;
	}

	// parse the UE formatted timestamp
	size_t ParseTimestamp(const char* Text, size_t Length, FLogLine& Line)
	{
		// exactly 25 characters with separators in known places
		if (Length < 25 || Text[0] != '[' || Text[24] != ']' || Text[11] != '-')
		{
			return 0;
		}
		int32 Year = 0;
		int32 Month = 0;
		int32 Day = 0;
		int32 Hour = 0;
		int32 Minute = 0;
		int32 Second = 0;
		int32 Millisecond = 0;
		if (sscanf(Text, "[%4d.%2d.%2d-%2d.%2d.%2d:%3d]",
			&Year, &Month, &Day, &Hour, &Minute, &Second, &Millisecond) != 7)
		{
			return 0;
		}
		char Buf[16];
		snprintf(Buf, sizeof(Buf), "%02d:%02d:%02d.%03d", Hour, Minute, Second, Millisecond);
		Line.Timestamp = Buf;
		Line.Hour = Hour;
		Line.Minute = Minute;
		Line.Second = Second;
		Line.Millisecond = Millisecond;
		return 25;
	}

	// "[  7]" at Text[0]. returns length consumed, 0 if absent
	size_t ParseFrame(const char* Text, size_t Length, FLogLine& Line)
	{
		if (Length < 2 || Text[0] != '[')
		{
			return 0;
		}
		size_t At = 1;
		while (At < Length && Text[At] == ' ')
		{
			++At;
		}
		int32 Value = 0;
		const size_t DigitsAt = At;
		while (At < Length && Text[At] >= '0' && Text[At] <= '9')
		{
			Value = Value * 10 + (Text[At++] - '0');
		}
		if (At == DigitsAt || At >= Length || Text[At] != ']')
		{
			return 0;
		}
		Line.Frame = Value;
		return At + 1;
	}

	// "LogCategory: " at Text[0]. strict so ordinary prose with a colon is left alone
	size_t ParseCategory(const char* Text, size_t Length, FLogLine& Line)
	{
		size_t At = 0;
		while (At < Length)
		{
			const char C = Text[At];
			const bool bWord = (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z')
				|| (C >= '0' && C <= '9') || C == '_';
			if (!bWord)
			{
				break;
			}
			++At;
		}
		if (At == 0 || At + 1 >= Length || Text[At] != ':' || Text[At + 1] != ' ')
		{
			return 0;
		}
		Line.Category.assign(Text, At);
		return At + 2;
	}

	// "Warning: " etc. untagged lines are Log
	struct FLevelTag
	{
		const char* Tag;
		size_t      Len;
		ELogLevel   Level;
	};

	size_t ParseLevel(const char* Text, size_t Length, FLogLine& Line)
	{
		static const FLevelTag Tags[] =
		{
			{ "Fatal: ",       7,  ELogLevel::Fatal },
			{ "Error: ",       7,  ELogLevel::Error },
			{ "Warning: ",     9,  ELogLevel::Warning },
			{ "Display: ",     9,  ELogLevel::Display },
			{ "Verbose: ",     9,  ELogLevel::Verbose },
			{ "VeryVerbose: ", 13, ELogLevel::VeryVerbose },
		};
		for (const FLevelTag& Tag : Tags)
		{
			if (Length >= Tag.Len && memcmp(Text, Tag.Tag, Tag.Len) == 0)
			{
				Line.Level = Tag.Level;
				return Tag.Len;
			}
		}
		return 0;
	}

	// split on lf. carry holds a trailing partial line between reads
	void ConsumeChunk(FLogSource* Source, std::string& Carry, const char* Data, size_t Size, ELogArrival Arrival)
	{
		std::vector<FLogLine> Batch;
		size_t LineStart = 0;
		for (size_t At = 0; At < Size; ++At)
		{
			if (Data[At] != '\n')
			{
				continue;
			}
			if (!Carry.empty())
			{
				Carry.append(Data + LineStart, At - LineStart);
				Batch.push_back(ParseLogLine(Carry.data(), Carry.size()));
				Carry.clear();
			}
			else
			{
				Batch.push_back(ParseLogLine(Data + LineStart, At - LineStart));
			}
			LineStart = At + 1;
		}
		if (LineStart < Size)
		{
			Carry.append(Data + LineStart, Size - LineStart);
		}
		if (!Batch.empty())
		{
			Source->Add(std::move(Batch), Arrival);
		}
	}

	void TailThread(FLogSource* Source)
	{
		const std::string Path = Source->Path;
		FILE* File = nullptr;
		int64 ReadOffset = 0;
		bool bCaughtUp = false;   // false during the initial read
		std::string Carry;
		std::vector<char> Buffer(64 * 1024);

		while (!Source->bStop.load(std::memory_order_relaxed))
		{
			if (!Source->bCollecting.load(std::memory_order_relaxed))
			{
				std::this_thread::sleep_for(PollInterval);
				continue;
			}

			if (!File)
			{
				File = OpenLogFile(Path.c_str());
				if (!File)
				{
					Source->SetStatus("waiting for " + Path);
					std::this_thread::sleep_for(std::chrono::milliseconds(400));
					continue;
				}
				ReadOffset = 0;
				bCaughtUp = false;
				Carry.clear();
				Source->SetStatus("watching " + Path);
				// skip a utf-8 bom so the first timestamp lands at byte 0
				char Bom[3];
				if (fread(Bom, 1, 3, File) == 3 && (unsigned char)Bom[0] == 0xEF
					&& (unsigned char)Bom[1] == 0xBB && (unsigned char)Bom[2] == 0xBF)
				{
					ReadOffset = 3;
				}
#ifdef _WIN32
				_fseeki64(File, ReadOffset, SEEK_SET);
#else
				fseeko(File, ReadOffset, SEEK_SET);
#endif
			}

			// truncation or recreation. start over
			int64 Size = 0;
			if (!GetLogFileSize(Path.c_str(), Size) || Size < ReadOffset)
			{
				fclose(File);
				File = nullptr;
				continue;
			}

			if (Size == ReadOffset)
			{
				bCaughtUp = true;
				std::this_thread::sleep_for(PollInterval);
				continue;
			}

			const size_t Want = (size_t)std::min<int64>(Size - ReadOffset, (int64)Buffer.size());
			const size_t Got = fread(Buffer.data(), 1, Want, File);
			if (Got == 0)
			{
				clearerr(File);
				std::this_thread::sleep_for(PollInterval);
				continue;
			}
			ReadOffset += (int64)Got;
			ConsumeChunk(Source, Carry, Buffer.data(), Got,
				bCaughtUp ? ELogArrival::Live : ELogArrival::Preloaded);
		}

		if (File)
		{
			fclose(File);
		}
	}
}

FLogLine ParseLogLine(const char* Text, size_t Length)
{
	// strip trailing cr. files are crlf on windows, the reader splits on lf
	if (Length > 0 && Text[Length - 1] == '\r')
	{
		--Length;
	}

	FLogLine Line;
	size_t At = ParseTimestamp(Text, Length, Line);
	At += ParseFrame(Text + At, Length - At, Line);
	At += ParseCategory(Text + At, Length - At, Line);
	At += ParseLevel(Text + At, Length - At, Line);
	Line.Text.assign(Text + At, Length - At);
	return Line;
}

std::thread StartTail(FLogSource* Source)
{
	return std::thread(TailThread, Source);
}
