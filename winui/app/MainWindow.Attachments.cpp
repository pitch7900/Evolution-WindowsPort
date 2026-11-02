#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "WindowIcon.h"
#include "Settings.h"

#include <winrt/Windows.System.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Security.Cryptography.Core.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <utility>
#include <vector>

/* Pièces jointes et enregistrement du message sur disque. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    /* Remplit la barre des pièces jointes du volet de lecture avec un bouton
     * de téléchargement pour chaque pièce jointe du message affiché.
     * Paramètres :
     *   attachments : tableau JSON des pièces jointes ("index", "filename", "size") */
    void MainWindow::PopulateAttachments(JsonArray const& attachments)
    {
        auto children = AttachmentBar().Children();
        children.Clear();

        if (!attachments || attachments.Size() == 0)
        {
            AttachmentBar().Visibility(Visibility::Collapsed);
            return;
        }

        for (auto const& item : attachments)
        {
            auto att = item.GetObject();
            uint32_t index = static_cast<uint32_t>(att.GetNamedNumber(L"index", 0));
            hstring filename = att.GetNamedString(L"filename", L"piece-jointe");
            int64_t size = static_cast<int64_t>(att.GetNamedNumber(L"size", 0));

            hstring size_label;
            if (size >= 1024 * 1024)
                size_label = L" (" + to_hstring(size / (1024 * 1024)) + L" Mo)";
            else if (size >= 1024)
                size_label = L" (" + to_hstring(size / 1024) + L" Ko)";
            else if (size > 0)
                size_label = L" (" + to_hstring(size) + L" o)";

            Button button;
            StackPanel content;
            content.Orientation(Orientation::Horizontal);
            content.Spacing(6);

            FontIcon icon;
            icon.Glyph(L"\xE723");
            icon.FontSize(12);
            content.Children().Append(icon);

            TextBlock label;
            label.Text(filename + size_label);
            content.Children().Append(label);

            button.Content(content);
            ToolTipService::SetToolTip(button, box_value(L"Aperçu de « " + filename + L" » (clic pour afficher, clic droit pour enregistrer)"));

            // Clic principal : aperçu sécurisé direct
            button.Click([this, index, filename](IInspectable const&, RoutedEventArgs const&)
            {
                PreviewAttachmentAsync(index, filename);
            });

            // Menu contextuel au clic droit
            MenuFlyout flyout;
            MenuFlyoutItem preview_item;
            preview_item.Text(L"Aperçu");
            preview_item.Icon(FontIcon{});
            preview_item.Icon().as<FontIcon>().Glyph(L"\xE890");
            preview_item.Click([this, index, filename](IInspectable const&, RoutedEventArgs const&)
            {
                PreviewAttachmentAsync(index, filename);
            });
            flyout.Items().Append(preview_item);

            MenuFlyoutItem save_item;
            save_item.Text(L"Enregistrer sous...");
            save_item.Icon(FontIcon{});
            save_item.Icon().as<FontIcon>().Glyph(L"\xE74E");
            save_item.Click([this, index, filename](IInspectable const&, RoutedEventArgs const&)
            {
                SaveAttachmentAsync(index, filename);
            });
            flyout.Items().Append(save_item);

            button.ContextFlyout(flyout);

            children.Append(button);
        }

        if (attachments.Size() > 1)
        {
            Button save_all;
            StackPanel content;
            content.Orientation(Orientation::Horizontal);
            content.Spacing(6);
            FontIcon icon;
            icon.Glyph(L"\xE896");
            icon.FontSize(12);
            content.Children().Append(icon);
            TextBlock label;
            label.Text(L"Tout enregistrer");
            content.Children().Append(label);
            save_all.Content(content);
            ToolTipService::SetToolTip(save_all, box_value(L"Enregistrer toutes les pièces jointes dans un dossier"));
            save_all.Click([this, attachments](IInspectable const&, RoutedEventArgs const&)
            {
                SaveAllAttachmentsAsync(attachments);
            });
            children.Append(save_all);
        }

        AttachmentBar().Visibility(Visibility::Visible);
    }

    /* Associe un sélecteur de fichiers / dossiers à la fenêtre (application
     * non empaquetée : IInitializeWithWindow). */
    void MainWindow::InitializePicker(IInspectable const& picker)
    {
        HWND hwnd = nullptr;
        if (auto native = try_as<::IWindowNative>())
            native->get_WindowHandle(&hwnd);
        if (hwnd)
            picker.as<::IInitializeWithWindow>()->Initialize(hwnd);
    }

    void MainWindow::OnSaveMessageClick(IInspectable const&, RoutedEventArgs const&)
    {
        SaveMessageAsAsync();
    }

    /* Enregistre le message affiché au format .eml (RFC 822) : export par
     * evo-core dans un dossier temporaire, puis copie à l'emplacement choisi. */
    fire_and_forget MainWindow::SaveMessageAsAsync()
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;
        hstring uid = SelectedMessageUid();
        hstring account = m_account;
        hstring folder = m_folder;

        if (uid.empty() || account.empty() || folder.empty())
            co_return;

        /* nom proposé : l'objet, sans caractères interdits dans un nom de fichier */
        std::wstring name{ m_current_message ? m_current_message.GetNamedString(L"subject", L"") : hstring{} };
        for (auto& c : name)
            if (wcschr(L"\\/:*?\"<>|", c) || c < 32)
                c = L'_';
        if (name.empty())
            name = L"message";

        Windows::Storage::Pickers::FileSavePicker picker;
        InitializePicker(picker);
        picker.SuggestedFileName(hstring{ name });
        picker.FileTypeChoices().Insert(L"Message électronique", single_threaded_vector<hstring>({ L".eml" }));

        auto file = co_await picker.PickSaveFileAsync();
        if (!file)
            co_return;
        hstring dest = file.Path();

        JsonObject request;
        request.SetNamedValue(L"account", JsonValue::CreateStringValue(account));
        request.SetNamedValue(L"folder", JsonValue::CreateStringValue(folder));
        JsonArray uids;
        uids.Append(JsonValue::CreateStringValue(uid));
        request.SetNamedValue(L"uids", uids);

        m_core->ExportMessagesEml(request.Stringify(), [this, dest](hstring const& json, hstring const& error)
        {
            JsonArray paths;
            if (!error.empty() || !JsonArray::TryParse(json, paths) || paths.Size() == 0)
            {
                ShowStatus(L"Enregistrement du message : " + error, InfoBarSeverity::Error);
                return;
            }

            hstring exported = paths.GetStringAt(0);
            if (!CopyFileW(exported.c_str(), dest.c_str(), FALSE))
            {
                ShowStatus(L"Enregistrement du message : impossible d'écrire « " + dest + L" ».", InfoBarSeverity::Error);
                return;
            }
            DeleteFileW(exported.c_str());
            ShowStatus(L"Message enregistré : « " + dest + L" ».", InfoBarSeverity::Success);
        });
    }

    /* Enregistre toutes les pièces jointes du message affiché dans un dossier
     * choisi ; les noms en double reçoivent un suffixe « (2) », « (3) »… */
    fire_and_forget MainWindow::SaveAllAttachmentsAsync(JsonArray attachments)
    {
        auto strong = get_strong();
        hstring uid = SelectedMessageUid();
        hstring account = m_account;
        hstring folder = m_folder;

        if (uid.empty() || account.empty() || folder.empty() || !attachments)
            co_return;

        Windows::Storage::Pickers::FolderPicker picker;
        InitializePicker(picker);
        picker.FileTypeFilter().Append(L"*");

        auto target = co_await picker.PickSingleFolderAsync();
        if (!target)
            co_return;
        std::wstring dir{ target.Path() };

        auto remaining = std::make_shared<uint32_t>(attachments.Size());
        auto failures = std::make_shared<uint32_t>(0);
        std::vector<std::wstring> used;

        for (auto const& value : attachments)
        {
            auto att = value.GetObject();
            uint32_t index = static_cast<uint32_t>(att.GetNamedNumber(L"index", 0));
            std::wstring filename{ att.GetNamedString(L"filename", L"") };
            for (auto& c : filename)
                if (wcschr(L"\\/:*?\"<>|", c) || c < 32)
                    c = L'_';
            if (filename.empty())
                filename = L"piece-jointe-" + std::to_wstring(index);

            /* nom libre dans le dossier et dans ce lot */
            auto dot = filename.rfind(L'.');
            std::wstring stem = dot == std::wstring::npos ? filename : filename.substr(0, dot);
            std::wstring ext = dot == std::wstring::npos ? L"" : filename.substr(dot);
            std::wstring path = dir + L"\\" + filename;
            for (int n = 2; GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES ||
                 std::find(used.begin(), used.end(), path) != used.end(); ++n)
                path = dir + L"\\" + stem + L" (" + std::to_wstring(n) + L")" + ext;
            used.push_back(path);

            m_core->SaveAttachment(account, folder, uid, index, hstring{ path },
                [this, remaining, failures, dir](hstring const&, hstring const& error)
            {
                if (!error.empty())
                    ++*failures;
                if (--*remaining > 0)
                    return;
                if (*failures)
                    ShowStatus(hstring{ std::to_wstring(*failures) + L" pièce(s) jointe(s) non enregistrée(s) dans « " + dir + L" »." },
                        InfoBarSeverity::Error);
                else
                    ShowStatus(hstring{ L"Pièces jointes enregistrées dans « " + dir + L" »." }, InfoBarSeverity::Success);
            });
        }
    }

    /* Ouvre un sélecteur d'enregistrement de fichier Windows et sauvegarde la
     * pièce jointe sélectionnée sur le disque via evo-core.
     * Paramètres :
     *   index    : indice de la pièce jointe (base 0) dans le message courant
     *   filename : nom de fichier suggéré pour l'enregistrement */
    fire_and_forget MainWindow::SaveAttachmentAsync(uint32_t index, hstring filename)
    {
        auto strong = get_strong();
        hstring uid = SelectedMessageUid();
        hstring account = m_account;
        hstring folder = m_folder;

        if (uid.empty() || account.empty() || folder.empty())
            co_return;

        Windows::Storage::Pickers::FileSavePicker picker;
        HWND hwnd = nullptr;
        if (auto native = try_as<::IWindowNative>())
            native->get_WindowHandle(&hwnd);
        if (hwnd)
            picker.as<::IInitializeWithWindow>()->Initialize(hwnd);

        picker.SuggestedFileName(filename);

        std::wstring_view view{ filename };
        auto dot = view.rfind(L'.');
        hstring ext = (dot != std::wstring_view::npos && dot + 1 < view.size())
            ? hstring{ view.substr(dot) }
            : L".bin";
        picker.FileTypeChoices().Insert(L"Fichier", single_threaded_vector<hstring>({ ext }));

        auto file = co_await picker.PickSaveFileAsync();
        if (!file)
            co_return;

        hstring dest_path = file.Path();
        m_core->SaveAttachment(account, folder, uid, index, dest_path,
            [this, filename](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Enregistrement de la pièce jointe : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Pièce jointe « " + filename + L" » enregistrée.", InfoBarSeverity::Success);
        });
    }
    /**
     * @brief Ferme le panneau d'aperçu de pièce jointe et réaffiche le corps du message.
     */
    void MainWindow::CloseAttachmentPreview()
    {
        AttachmentPreviewContainer().Visibility(Visibility::Collapsed);
        if (m_current_message)
        {
            ReaderContainer().Visibility(Visibility::Visible);
        }

        PreviewImage().Source(nullptr);
        PreviewText().Text(L"");
        PreviewImageScroll().Visibility(Visibility::Collapsed);
        PreviewTextScroll().Visibility(Visibility::Collapsed);
        PreviewUnsupported().Visibility(Visibility::Collapsed);
        PreviewLoadingRing().IsActive(false);

        if (!m_preview_current_path.empty())
        {
            DeleteFileW(m_preview_current_path.c_str());
            m_preview_current_path = L"";
        }
    }

    /**
     * @brief Gestionnaire de clic pour fermer l'aperçu et retourner au message.
     */
    void MainWindow::OnCloseAttachmentPreviewClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        CloseAttachmentPreview();
    }

    /**
     * @brief Télécharge temporairement et affiche un aperçu sécurisé d'une pièce jointe.
     * @param index Indice de la pièce jointe dans le message.
     * @param filename Nom du fichier joint.
     */
    fire_and_forget MainWindow::PreviewAttachmentAsync(uint32_t index, hstring filename)
    {
        auto strong = get_strong();
        hstring uid = SelectedMessageUid();
        hstring account = m_account;
        hstring folder = m_folder;

        if (uid.empty() || account.empty() || folder.empty())
            co_return;

        m_preview_current_index = index;
        m_preview_current_filename = filename;

        // Préparation du répertoire temporaire sécurisé
        wchar_t temp_dir[MAX_PATH];
        DWORD len = GetTempPathW(MAX_PATH, temp_dir);
        if (len == 0 || len > MAX_PATH)
            co_return;

        std::wstring preview_dir = std::wstring{ temp_dir } + L"EvolutionWin\\Previews";
        CreateDirectoryW((std::wstring{ temp_dir } + L"EvolutionWin").c_str(), nullptr);
        CreateDirectoryW(preview_dir.c_str(), nullptr);

        std::wstring temp_file = preview_dir + L"\\preview_" + std::to_wstring(index) + L"_" + std::wstring{ SanitizeFileName(filename, L"piece-jointe") };
        m_preview_current_path = hstring{ temp_file };

        // Masquage du lecteur et affichage du panneau d'aperçu
        ReaderContainer().Visibility(Visibility::Collapsed);
        AttachmentPreviewContainer().Visibility(Visibility::Visible);
        PreviewFilenameText().Text(filename);

        PreviewImageScroll().Visibility(Visibility::Collapsed);
        PreviewTextScroll().Visibility(Visibility::Collapsed);
        PreviewUnsupported().Visibility(Visibility::Collapsed);
        PreviewLoadingRing().IsActive(true);

        m_core->SaveAttachment(account, folder, uid, index, m_preview_current_path,
            [this, strong, filename, temp_file](hstring const&, hstring const& error)
        {
            DispatcherQueue().TryEnqueue([this, strong, filename, temp_file, error]() -> fire_and_forget
            {
                PreviewLoadingRing().IsActive(false);

                if (!error.empty())
                {
                    ShowStatus(L"Impossible de charger l'aperçu : " + error, InfoBarSeverity::Error);
                    CloseAttachmentPreview();
                    co_return;
                }

                auto kind = DetectAttachmentPreviewKind(filename);
                if (kind == AttachmentPreviewKind::Image)
                {
                    try
                    {
                        auto file = co_await Windows::Storage::StorageFile::GetFileFromPathAsync(temp_file);
                        auto stream = co_await file.OpenReadAsync();
                        Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
                        bitmap.SetSource(stream);
                        PreviewImage().Source(bitmap);
                        PreviewImageScroll().Visibility(Visibility::Visible);
                    }
                    catch (...)
                    {
                        PreviewUnsupported().Visibility(Visibility::Visible);
                    }
                }
                else if (kind == AttachmentPreviewKind::Text)
                {
                    try
                    {
                        auto file = co_await Windows::Storage::StorageFile::GetFileFromPathAsync(temp_file);
                        hstring content = co_await Windows::Storage::FileIO::ReadTextAsync(file);
                        PreviewText().Text(content);
                        PreviewTextScroll().Visibility(Visibility::Visible);
                    }
                    catch (...)
                    {
                        // Repli en lecture binaire brute si encodage non UTF-8
                        HANDLE h = CreateFileW(temp_file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                        if (h != INVALID_HANDLE_VALUE)
                        {
                            DWORD size = GetFileSize(h, nullptr);
                            if (size > 0 && size < 2 * 1024 * 1024)
                            {
                                std::string buf(size, '\0');
                                DWORD read_bytes = 0;
                                ReadFile(h, buf.data(), size, &read_bytes, nullptr);
                                std::wstring wcontent(buf.begin(), buf.end());
                                PreviewText().Text(hstring{ wcontent });
                                PreviewTextScroll().Visibility(Visibility::Visible);
                            }
                            else
                            {
                                PreviewUnsupported().Visibility(Visibility::Visible);
                            }
                            CloseHandle(h);
                        }
                        else
                        {
                            PreviewUnsupported().Visibility(Visibility::Visible);
                        }
                    }
                }
                else
                {
                    PreviewUnsupported().Visibility(Visibility::Visible);
                }
            });
        });
    }

    /**
     * @brief Déclenche l'enregistrement sous de la pièce jointe actuellement prévisualisée.
     */
    void MainWindow::OnPreviewSaveClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        SaveAttachmentAsync(m_preview_current_index, m_preview_current_filename);
    }

    /**
     * @brief Ouvre le fichier de pièce jointe prévisualisé dans l'application par défaut du système.
     */
    void MainWindow::OnPreviewOpenClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        if (!m_preview_current_path.empty())
        {
            ShellExecuteW(nullptr, L"open", m_preview_current_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
}
