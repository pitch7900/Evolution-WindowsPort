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
#include <memory>
#include <optional>
#include <fstream>
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

            hstring size_str = FormatFileSize(size);
            hstring size_label = size_str.empty() ? hstring{} : L" (" + size_str + L")";

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
            ToolTipService::SetToolTip(button, box_value(Fmt(Tr(L"Aperçu de « {0} » (clic pour afficher, clic droit pour enregistrer)"), { filename })));

            // Clic principal : aperçu sécurisé direct
            button.Click([this, index, filename](IInspectable const&, RoutedEventArgs const&)
            {
                PreviewAttachmentAsync(index, filename);
            });

            // Menu contextuel au clic droit
            MenuFlyout flyout;
            MenuFlyoutItem preview_item;
            preview_item.Text(Tr(L"Aperçu"));
            preview_item.Icon(FontIcon{});
            preview_item.Icon().as<FontIcon>().Glyph(L"\xE890");
            preview_item.Click([this, index, filename](IInspectable const&, RoutedEventArgs const&)
            {
                PreviewAttachmentAsync(index, filename);
            });
            flyout.Items().Append(preview_item);

            MenuFlyoutItem save_item;
            save_item.Text(Tr(L"Enregistrer sous..."));
            save_item.Icon(FontIcon{});
            save_item.Icon().as<FontIcon>().Glyph(L"\xE74E");
            save_item.Click([this, index, filename](IInspectable const&, RoutedEventArgs const&)
            {
                SaveAttachmentAsync(index, filename);
            });
            flyout.Items().Append(save_item);

            button.ContextFlyout(flyout);

            /* Glisser-déposer sortant : la pièce jointe est extraite dans le dossier
             * temporaire de glisser-déposer au début du glissement (report asynchrone),
             * puis offerte à l'Explorateur / au Bureau sous forme de fichier. */
            button.CanDrag(true);

            /* Un Button capture le pointeur : le glissement natif (CanDrag) ne démarre alors
             * pas. On le déclenche explicitement quand la souris, bouton gauche enfoncé, s'éloigne
             * de plus de quelques pixels du point d'appui. */
            auto press_point = std::make_shared<std::optional<Windows::Foundation::Point>>();
            button.AddHandler(UIElement::PointerPressedEvent(),
                box_value(Input::PointerEventHandler([press_point](IInspectable const&, Input::PointerRoutedEventArgs const& a)
            {
                *press_point = a.GetCurrentPoint(nullptr).Position();
            })), true);
            button.AddHandler(UIElement::PointerReleasedEvent(),
                box_value(Input::PointerEventHandler([press_point](IInspectable const&, Input::PointerRoutedEventArgs const&)
            {
                press_point->reset();
            })), true);
            button.AddHandler(UIElement::PointerMovedEvent(),
                box_value(Input::PointerEventHandler([press_point](IInspectable const& sender, Input::PointerRoutedEventArgs const& a)
            {
                if (!press_point->has_value())
                    return;
                auto point = a.GetCurrentPoint(nullptr);
                if (!point.Properties().IsLeftButtonPressed())
                {
                    press_point->reset();
                    return;
                }
                double dx = point.Position().X - (*press_point)->X;
                double dy = point.Position().Y - (*press_point)->Y;
                if (dx * dx + dy * dy < 36.0)
                    return;
                press_point->reset();
                sender.as<UIElement>().StartDragAsync(point);
            })), true);

            button.DragStarting([this, index, filename](UIElement const&, DragStartingEventArgs const& args)
            {
                hstring uid = SelectedMessageUid();
                if (uid.empty() || m_account.empty() || m_folder.empty())
                {
                    args.Cancel(true);
                    return;
                }

                std::wstring dir = ::EvolutionWin::Helpers::GetDragDropTempDir();
                std::wstring path = dir + std::wstring{ SanitizeFileName(filename, L"piece-jointe") };
                auto deferral = args.GetDeferral();
                auto data = args.Data();
                data.RequestedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);

                m_core->SaveAttachment(m_account, m_folder, uid, index, hstring{ path },
                    [this, deferral, data, path](hstring const&, hstring const& error)
                {
                    DispatcherQueue().TryEnqueue(::EvolutionWin::Helpers::KeepCoroutineAlive(
                        [this, deferral, data, path, error]() -> Windows::Foundation::IAsyncAction
                    {
                        if (error.empty())
                        {
                            try
                            {
                                auto file = co_await Windows::Storage::StorageFile::GetFileFromPathAsync(path);
                                data.SetStorageItems(single_threaded_vector<Windows::Storage::IStorageItem>({ file }));
                            }
                            catch (...)
                            {
                            }
                        }
                        else
                        {
                            ShowStatus(Fmt(Tr(L"Impossible de glisser la pièce jointe : {0}"), { error }), InfoBarSeverity::Error);
                        }
                        deferral.Complete();
                    }));
                });
            });

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
            label.Text(Tr(L"Tout enregistrer"));
            content.Children().Append(label);
            save_all.Content(content);
            ToolTipService::SetToolTip(save_all, box_value(Tr(L"Enregistrer toutes les pièces jointes dans un dossier")));
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

    /**
     * @brief Enregistre les messages sélectionnés au format .eml (RFC 822).
     *        Pour un message unique, ouvre un FileSavePicker proposant l'objet comme nom de fichier.
     *        Pour une sélection multiple, ouvre un FolderPicker et y exporte tous les courriels.
     */
    fire_and_forget MainWindow::SaveMessageAsAsync()
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;

        auto sel_uids = SelectedMessageUids();
        hstring uid = sel_uids.empty() ? SelectedMessageUid() : sel_uids.front();
        auto uid_location = GetMessageLocation(uid);
        hstring account = uid_location.account;
        hstring folder = uid_location.folder;

        if (uid.empty() || account.empty() || folder.empty())
            co_return;

        /* Export multiple : sélection d'un dossier de destination via FolderPicker */
        if (sel_uids.size() > 1)
        {
            Windows::Storage::Pickers::FolderPicker picker;
            InitializePicker(picker);
            picker.SuggestedStartLocation(Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);
            picker.FileTypeFilter().Append(L"*");

            auto folder_dest = co_await picker.PickSingleFolderAsync();
            if (!folder_dest)
                co_return;

            hstring dest_dir = folder_dest.Path();
            JsonObject request = BuildMessagesRequest(sel_uids);

            ShowStatus(Fmt(Tr(L"Exportation de {0} messages en cours…"), { to_hstring(sel_uids.size()) }), InfoBarSeverity::Informational);
            m_core->ExportMessagesEml(request.Stringify(), [this, dest_dir](hstring const& json, hstring const& error)
            {
                JsonArray paths;
                if (!error.empty() || !JsonArray::TryParse(json, paths) || paths.Size() == 0)
                {
                    ShowStatus(Fmt(Tr(L"Enregistrement des messages : {0}"), { error }), InfoBarSeverity::Error);
                    return;
                }
                uint32_t saved = 0;
                for (auto const& p : paths)
                {
                    hstring src_path = p.GetString();
                    std::filesystem::path src_fs{ src_path.c_str() };
                    std::filesystem::path dest_fs = std::filesystem::path{ dest_dir.c_str() } / src_fs.filename();
                    if (CopyFileW(src_fs.c_str(), dest_fs.c_str(), FALSE))
                    {
                        ++saved;
                        DeleteFileW(src_fs.c_str());
                    }
                }
                ShowStatus(Fmt(Tr(L"Terminé : {0} messages enregistrés dans « {1} »."), { to_hstring(saved), dest_dir }), InfoBarSeverity::Success);
            });
            co_return;
        }

        /* Export unitaire : sélection du fichier via FileSavePicker */
        hstring name = m_current_message ? m_current_message.GetNamedString(L"subject", L"") : hstring{};
        hstring sanitized = ::EvolutionWin::Helpers::SanitizeFileName(name, L"message");

        Windows::Storage::Pickers::FileSavePicker picker;
        InitializePicker(picker);
        picker.SuggestedFileName(sanitized);
        picker.FileTypeChoices().Insert(Tr(L"Message électronique"), single_threaded_vector<hstring>({ L".eml" }));

        auto file = co_await picker.PickSaveFileAsync();
        if (!file)
            co_return;
        hstring dest = file.Path();

        JsonObject request = BuildMessagesRequest({ uid });

        m_core->ExportMessagesEml(request.Stringify(), [this, dest](hstring const& json, hstring const& error)
        {
            JsonArray paths;
            if (!error.empty() || !JsonArray::TryParse(json, paths) || paths.Size() == 0)
            {
                ShowStatus(Fmt(Tr(L"Enregistrement du message : {0}"), { error }), InfoBarSeverity::Error);
                return;
            }

            hstring exported = paths.GetStringAt(0);
            if (!CopyFileW(exported.c_str(), dest.c_str(), FALSE))
            {
                ShowStatus(Fmt(Tr(L"Enregistrement du message : impossible d'écrire « {0} »."), { dest }), InfoBarSeverity::Error);
                return;
            }
            DeleteFileW(exported.c_str());
            ShowStatus(Fmt(Tr(L"Message enregistré : « {0} »."), { dest }), InfoBarSeverity::Success);
        });
    }

    /**
     * @brief Exporte la totalité des courriels d'un dossier sous forme de fichiers .eml dans un répertoire choisi.
     * @param account Identifiant du compte.
     * @param folder Chemin complet du dossier.
     * @param folder_name Nom convivial du dossier affiché.
     */
    fire_and_forget MainWindow::ExportFolderEmlAsync(hstring account, hstring folder, hstring folder_name)
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;

        if (!m_core || account.empty() || folder.empty())
            co_return;

        Windows::Storage::Pickers::FolderPicker picker;
        InitializePicker(picker);
        picker.SuggestedStartLocation(Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);
        picker.FileTypeFilter().Append(L"*");

        auto folder_dest = co_await picker.PickSingleFolderAsync();
        if (!folder_dest)
            co_return;

        hstring dest_dir = folder_dest.Path();
        ShowStatus(Fmt(Tr(L"Récupération de la liste des messages de « {0} »…"), { folder_name }), InfoBarSeverity::Informational);

        m_core->ListMessages(account, folder, 0, [this, account, folder, folder_name, dest_dir](hstring const& json, hstring const& error)
        {
            DispatcherQueue().TryEnqueue(::EvolutionWin::Helpers::KeepCoroutineAlive([this, account, folder, folder_name, dest_dir, json, error]() -> Windows::Foundation::IAsyncAction
            {
                JsonArray msgs;
                if (!error.empty() || !JsonArray::TryParse(json, msgs) || msgs.Size() == 0)
                {
                    ShowStatus(Fmt(Tr(L"Aucun message à exporter dans « {0} »."), { folder_name }), InfoBarSeverity::Warning);
                    co_return;
                }

                JsonArray uids;
                for (auto const& item : msgs)
                {
                    if (auto obj = item.try_as<JsonObject>())
                    {
                        hstring u = obj.GetNamedString(L"uid", L"");
                        if (!u.empty())
                            uids.Append(JsonValue::CreateStringValue(u));
                    }
                }

                if (uids.Size() == 0)
                {
                    ShowStatus(Tr(L"Aucun identifiant de message trouvé pour l'export."), InfoBarSeverity::Warning);
                    co_return;
                }

                JsonObject req;
                req.SetNamedValue(L"account", JsonValue::CreateStringValue(account));
                req.SetNamedValue(L"folder", JsonValue::CreateStringValue(folder));
                req.SetNamedValue(L"uids", uids);

                ShowStatus(Fmt(Tr(L"Exportation de {0} messages de « {1} »…"), { to_hstring(uids.Size()), folder_name }), InfoBarSeverity::Informational);
                m_core->ExportMessagesEml(req.Stringify(), [this, dest_dir, folder_name](hstring const& res_json, hstring const& res_err)
                {
                    JsonArray paths;
                    if (!res_err.empty() || !JsonArray::TryParse(res_json, paths) || paths.Size() == 0)
                    {
                        ShowStatus(Fmt(Tr(L"Erreur lors de l'exportation : {0}"), { res_err }), InfoBarSeverity::Error);
                        return;
                    }

                    uint32_t count = 0;
                    for (auto const& p : paths)
                    {
                        hstring src = p.GetString();
                        std::filesystem::path src_path(src.c_str());
                        std::filesystem::path target = std::filesystem::path(dest_dir.c_str()) / src_path.filename();
                        std::error_code ec;
                        std::filesystem::copy_file(src_path, target, std::filesystem::copy_options::overwrite_existing, ec);
                        DeleteFileW(src.c_str());
                        if (!ec)
                            count++;
                    }
                    ShowStatus(Fmt(Tr(L"Exportation réussie : {0} messages de « {1} » enregistrés sous format .eml."), { to_hstring(count), folder_name }), InfoBarSeverity::Success);
                });
            }));
        });
    }

    /* Enregistre toutes les pièces jointes du message affiché dans un dossier
     * choisi ; les noms en double reçoivent un suffixe « (2) », « (3) »… */
    fire_and_forget MainWindow::SaveAllAttachmentsAsync(JsonArray attachments)
    {
        auto strong = get_strong();
        hstring uid = SelectedMessageUid();
        auto uid_location = GetMessageLocation(uid);
        hstring account = uid_location.account;
        hstring folder = uid_location.folder;

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
                    ShowStatus(Fmt(Tr(L"Échec : {0} pièce(s) jointe(s) non enregistrée(s) dans « {1} »."), { to_hstring(*failures), hstring{ dir } }),
                        InfoBarSeverity::Error);
                else
                    ShowStatus(Fmt(Tr(L"Pièces jointes enregistrées dans « {0} »."), { hstring{ dir } }), InfoBarSeverity::Success);
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
        auto uid_location = GetMessageLocation(uid);
        hstring account = uid_location.account;
        hstring folder = uid_location.folder;

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
        picker.FileTypeChoices().Insert(Tr(L"Fichier"), single_threaded_vector<hstring>({ ext }));

        auto file = co_await picker.PickSaveFileAsync();
        if (!file)
            co_return;

        hstring dest_path = file.Path();
        m_core->SaveAttachment(account, folder, uid, index, dest_path,
            [this, filename](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(Fmt(Tr(L"Enregistrement de la pièce jointe : {0}"), { error }), InfoBarSeverity::Error);
                return;
            }
            ShowStatus(Fmt(Tr(L"Pièce jointe « {0} » enregistrée."), { filename }), InfoBarSeverity::Success);
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
        PreviewPdfPages().Children().Clear();
        PreviewPdfScroll().Visibility(Visibility::Collapsed);
        PreviewWeb().Visibility(Visibility::Collapsed);
        if (PreviewWeb().CoreWebView2())
            PreviewWeb().CoreWebView2().NavigateToString(L"<html></html>");

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
        auto uid_location = GetMessageLocation(uid);
        hstring account = uid_location.account;
        hstring folder = uid_location.folder;

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
        PreviewPdfScroll().Visibility(Visibility::Collapsed);
        PreviewWeb().Visibility(Visibility::Collapsed);
        PreviewLoadingRing().IsActive(true);

        m_core->SaveAttachment(account, folder, uid, index, m_preview_current_path,
            [this, strong, filename, temp_file](hstring const&, hstring const& error)
        {
            DispatcherQueue().TryEnqueue(::EvolutionWin::Helpers::KeepCoroutineAlive([this, strong, filename, temp_file, error]() -> Windows::Foundation::IAsyncAction
            {
                PreviewLoadingRing().IsActive(false);

                if (!error.empty())
                {
                    ShowStatus(Fmt(Tr(L"Impossible de charger l'aperçu : {0}"), { error }), InfoBarSeverity::Error);
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
                else if (kind == AttachmentPreviewKind::Pdf)
                {
                    try
                    {
                        auto file = co_await Windows::Storage::StorageFile::GetFileFromPathAsync(temp_file);
                        auto doc = co_await Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(file);
                        PreviewPdfPages().Children().Clear();
                        uint32_t count = (std::min)(doc.PageCount(), 60u);
                        for (uint32_t p = 0; p < count; ++p)
                        {
                            auto page = doc.GetPage(p);
                            Windows::Storage::Streams::InMemoryRandomAccessStream mem;
                            Windows::Data::Pdf::PdfPageRenderOptions opts;
                            opts.DestinationWidth(900);
                            co_await page.RenderToStreamAsync(mem, opts);
                            Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
                            co_await bitmap.SetSourceAsync(mem);
                            Image img;
                            img.Source(bitmap);
                            img.Width(900);
                            img.Stretch(Microsoft::UI::Xaml::Media::Stretch::Uniform);
                            PreviewPdfPages().Children().Append(img);
                            PreviewPdfScroll().Visibility(Visibility::Visible);
                        }
                    }
                    catch (...)
                    {
                        PreviewPdfScroll().Visibility(Visibility::Collapsed);
                        PreviewUnsupported().Visibility(Visibility::Visible);
                    }
                }
                else if (kind == AttachmentPreviewKind::Html)
                {
                    try
                    {
                        co_await PreviewWeb().EnsureCoreWebView2Async();
                        auto core = PreviewWeb().CoreWebView2();
                        core.Settings().IsScriptEnabled(false);
                        core.Settings().AreDevToolsEnabled(false);
                        core.Settings().AreDefaultContextMenusEnabled(false);
                        core.NavigationStarting([](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NavigationStartingEventArgs const& a)
                        {
                            /* seule la page injectee (data:) est autorisee : pas de lien ni de redirection */
                            if (std::wstring_view{ a.Uri() }.rfind(L"data:", 0) != 0)
                                a.Cancel(true);
                        });
                        core.NewWindowRequested([](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NewWindowRequestedEventArgs const& a) { a.Handled(true); });
                        auto file = co_await Windows::Storage::StorageFile::GetFileFromPathAsync(temp_file);
                        hstring html = co_await Windows::Storage::FileIO::ReadTextAsync(file);
                        /* CSP : aucune ressource distante, images integrees seulement */
                        std::wstring doc = L"<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; img-src data:; style-src 'unsafe-inline'\">" + std::wstring{ html };
                        PreviewWeb().Visibility(Visibility::Visible);
                        core.NavigateToString(hstring{ doc });
                    }
                    catch (...)
                    {
                        PreviewWeb().Visibility(Visibility::Collapsed);
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
            }));
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

    /**
     * @brief Gestionnaire de clic pour afficher le flux brut MIME du message actif.
     * @param sender Contrôle bouton déclencheur.
     * @param args Arguments de l'événement de clic.
     */
    void MainWindow::OnViewRawMimeClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        hstring uid = SelectedMessageUid();
        if (!uid.empty())
        {
            ShowRawMimeSourceAsync(uid);
        }
    }

    /**
     * @brief Extrait et affiche le contenu complet brut RFC 822 / MIME dans une boîte de dialogue dédiée.
     * @param uid Identifiant unique du message à inspecter.
     */
    fire_and_forget MainWindow::ShowRawMimeSourceAsync(hstring uid)
    {
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        auto loc = GetMessageLocation(uid);
        hstring account = loc.account.empty() ? m_account : loc.account;
        hstring folder = loc.folder.empty() ? m_folder : loc.folder;

        if (!m_core || account.empty() || folder.empty() || uid.empty())
            co_return;

        JsonObject request = BuildMessagesRequest({ uid });

        m_core->ExportMessagesEml(request.Stringify(), [this](hstring const& json, hstring const& error)
        {
            DispatcherQueue().TryEnqueue(::EvolutionWin::Helpers::KeepCoroutineAlive([this, json, error]() -> Windows::Foundation::IAsyncAction
            {
                JsonArray paths;
                if (!error.empty() || !JsonArray::TryParse(json, paths) || paths.Size() == 0)
                {
                    ShowStatus(Fmt(Tr(L"Impossible d'extraire la source brute : {0}"), { error }), InfoBarSeverity::Error);
                    co_return;
                }

                hstring eml_path = paths.GetStringAt(0);
                std::wstring raw_content;
                {
                    std::ifstream in(std::filesystem::path(eml_path.c_str()), std::ios::binary);
                    if (in)
                    {
                        std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
                        if (len > 0)
                        {
                            raw_content.resize(len - 1);
                            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, raw_content.data(), len);
                        }
                        else
                        {
                            int len_a = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
                            if (len_a > 0)
                            {
                                raw_content.resize(len_a - 1);
                                MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, raw_content.data(), len_a);
                            }
                        }
                    }
                    DeleteFileW(eml_path.c_str());
                }

                if (raw_content.empty())
                {
                    ShowStatus(Tr(L"Flux brut du message vide."), InfoBarSeverity::Warning);
                    co_return;
                }

                ContentDialog dialog;
                dialog.XamlRoot(Content().XamlRoot());
                dialog.Title(box_value(Tr(L"Source brute MIME du message (RFC 822)")));
                dialog.CloseButtonText(Tr(L"Fermer"));
                dialog.DefaultButton(ContentDialogButton::Close);

                Grid root;
                root.RowDefinitions().Append(RowDefinition{});
                root.RowDefinitions().Append(RowDefinition{});
                root.RowDefinitions().GetAt(0).Height(GridLengthHelper::Auto());
                RowDefinition body_row;
                body_row.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                root.RowDefinitions().Append(body_row);

                StackPanel toolbar;
                toolbar.Orientation(Orientation::Horizontal);
                toolbar.Spacing(8);
                toolbar.Margin(ThicknessHelper::FromLengths(0, 0, 0, 8));

                TextBox search_box;
                search_box.PlaceholderText(Tr(L"Rechercher dans la source (Ctrl+F)..."));
                search_box.Width(280);

                Button copy_btn;
                copy_btn.Content(box_value(Tr(L"Copier tout")));

                Button save_btn;
                save_btn.Content(box_value(Tr(L"Enregistrer sous (.eml)...")));

                toolbar.Children().Append(search_box);
                toolbar.Children().Append(copy_btn);
                toolbar.Children().Append(save_btn);

                TextBox editor;
                editor.Text(hstring{ raw_content });
                editor.IsReadOnly(true);
                editor.AcceptsReturn(true);
                editor.TextWrapping(TextWrapping::NoWrap);
                editor.FontFamily(Microsoft::UI::Xaml::Media::FontFamily(L"Cascadia Mono, Consolas, Courier New"));
                editor.FontSize(11);
                editor.Width(750);
                editor.Height(440);

                search_box.TextChanged([editor, raw_content, search_box](IInspectable const&, Controls::TextChangedEventArgs const&)
                {
                    std::wstring q{ search_box.Text() };
                    if (q.empty()) return;
                    size_t pos = ::EvolutionWin::Helpers::FindInRawMime(raw_content, q);
                    if (pos != std::wstring::npos)
                    {
                        editor.Select(static_cast<int32_t>(pos), static_cast<int32_t>(q.size()));
                    }
                });

                copy_btn.Click([raw_content](IInspectable const&, RoutedEventArgs const&)
                {
                    Windows::ApplicationModel::DataTransfer::DataPackage pkg;
                    pkg.SetText(hstring{ raw_content });
                    Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(pkg);
                });

                save_btn.Click([this](IInspectable const&, RoutedEventArgs const&)
                {
                    SaveMessageAsAsync();
                });

                Grid::SetRow(toolbar, 0);
                Grid::SetRow(editor, 1);
                root.Children().Append(toolbar);
                root.Children().Append(editor);

                dialog.Content(root);

                m_dialog_open = true;
                co_await dialog.ShowAsync();
                m_dialog_open = false;
            }));
        });

        co_return;
    }
}
