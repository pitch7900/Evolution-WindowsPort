#pragma once

/*
 * Journal des erreurs inattendues de l'interface : exceptions WinRT ou C++
 * levées dans un rappel d'evo-core ou un gestionnaire d'événement XAML, qui
 * arrêteraient sinon l'application (exception « stowed » relevée par
 * CoreMessagingXP). Une ligne par erreur dans
 * %LOCALAPPDATA%\EvolutionWin\errors.log (UTF-8, 1 Mo au plus, l'ancien
 * journal est conservé en errors.old.log) et dans la sortie de débogage.
 */

namespace EvolutionWin
{
    inline void LogError(wchar_t const* context, int32_t code, std::wstring_view message)
    {
        SYSTEMTIME now{};
        GetLocalTime(&now);

        wchar_t prefix[96]{};
        swprintf_s(prefix, L"%04u-%02u-%02u %02u:%02u:%02u [%s] 0x%08X ",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
            context, static_cast<uint32_t>(code));
        std::wstring line = std::wstring{ prefix } + std::wstring{ message } + L"\r\n";
        OutputDebugStringW(line.c_str());

        wchar_t local_app_data[MAX_PATH]{};
        if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH))
            return;

        std::wstring folder = std::wstring{ local_app_data } + L"\\EvolutionWin";
        std::wstring path = folder + L"\\errors.log";
        CreateDirectoryW(folder.c_str(), nullptr);

        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
            (info.nFileSizeHigh > 0 || info.nFileSizeLow > 1024 * 1024))
            MoveFileExW(path.c_str(), (folder + L"\\errors.old.log").c_str(), MOVEFILE_REPLACE_EXISTING);

        std::string utf8 = winrt::to_string(line);
        HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        CloseHandle(file);
    }

    /* À appeler dans un bloc catch (...) : journalise l'exception en cours. */
    inline void LogCurrentException(wchar_t const* context)
    {
        try
        {
            throw;
        }
        catch (winrt::hresult_error const& e)
        {
            LogError(context, e.code(), e.message());
        }
        catch (std::exception const& e)
        {
            LogError(context, E_FAIL, winrt::to_hstring(e.what()));
        }
        catch (...)
        {
            LogError(context, E_UNEXPECTED, L"exception inconnue");
        }
    }
}
