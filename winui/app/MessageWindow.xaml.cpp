#include "pch.h"
#include "MessageWindow.xaml.h"
#include "ComposeWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "WindowIcon.h"
#include "Settings.h"

#include <microsoft.ui.xaml.window.h>
#include <shobjidl.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#if __has_include("MessageWindow.g.cpp")
#include "MessageWindow.g.cpp"
#endif

/* Fenêtre indépendante de lecture d'un message électronique avec en-têtes,
 * pièces jointes, bandeau de sécurité/confidentialité et barre d'actions. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace
{
    constexpr std::wstring_view ReaderOrigin = L"http://reader.evolution.invalid/";
    constexpr wchar_t RemoteContentKey[] = L"privacy.remote-content";
    constexpr wchar_t TrustedSendersKey[] = L"privacy.trusted-senders";
    constexpr wchar_t TrustedDomainsKey[] = L"privacy.trusted-domains";

    bool StartsWith(std::wstring_view text, std::wstring_view prefix)
    {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    bool IsRemoteUri(std::wstring_view uri)
    {
        auto lower = ToLower(uri.substr(0, 8));
        return StartsWith(lower, L"http:") || StartsWith(lower, L"https:") ||
               StartsWith(lower, L"ws:") || StartsWith(lower, L"wss:") || StartsWith(lower, L"ftp:");
    }

    std::wstring SenderAddress(hstring const& from)
    {
        std::wstring_view view{ from };
        std::wstring_view address;

        auto open = view.find(L'<');
        if (open != std::wstring_view::npos)
        {
            auto close = view.find(L'>', open);
            address = view.substr(open + 1, close == std::wstring_view::npos ? std::wstring_view::npos : close - open - 1);
        }
        else
        {
            address = view.substr(0, view.find(L','));
        }

        address = Trim(address);
        auto at = address.rfind(L'@');
        if (at == std::wstring_view::npos || at == 0 || at + 1 >= address.size() ||
            address.find_first_of(L" \t\"<>,;") != std::wstring_view::npos)
            return {};

        return ToLower(address);
    }

    std::wstring AddressDomain(std::wstring const& address)
    {
        auto at = address.rfind(L'@');
        return at == std::wstring::npos ? std::wstring{} : address.substr(at + 1);
    }

    std::vector<std::wstring> ReadTrustList(wchar_t const* key)
    {
        std::vector<std::wstring> list;
        for (auto const& value : ::EvolutionWin::Settings::Instance().GetArray(key))
        {
            if (value.ValueType() == JsonValueType::String)
            {
                auto entry = ToLower(Trim(value.GetString()));
                if (!entry.empty() && std::find(list.begin(), list.end(), entry) == list.end())
                    list.push_back(entry);
            }
        }
        return list;
    }

    void WriteTrustList(wchar_t const* key, std::vector<std::wstring> const& list)
    {
        JsonArray array;
        for (auto const& entry : list)
            array.Append(JsonValue::CreateStringValue(entry));
        ::EvolutionWin::Settings::Instance().Set(key, array);
    }

    std::wstring RemoteContentMode()
    {
        std::wstring mode{ ::EvolutionWin::Settings::Instance().GetString(RemoteContentKey, L"trusted") };
        return mode == L"never" || mode == L"always" ? mode : L"trusted";
    }

    bool IsRemoteContentTrusted(hstring const& from)
    {
        auto mode = RemoteContentMode();
        if (mode == L"always")
            return true;
        if (mode == L"never")
            return false;

        auto address = SenderAddress(from);
        if (address.empty())
            return false;

        auto senders = ReadTrustList(TrustedSendersKey);
        if (std::find(senders.begin(), senders.end(), address) != senders.end())
            return true;

        auto domains = ReadTrustList(TrustedDomainsKey);
        return std::find(domains.begin(), domains.end(), AddressDomain(address)) != domains.end();
    }
}

namespace winrt::EvolutionWin::implementation
{
    /* Constructeur de la fenêtre indépendante de message */
    MessageWindow::MessageWindow()
    {
        InitializeComponent();

        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        ::EvolutionWin::SetWindowIcon(AppWindow());

        RestoreWindowBounds();
        InitializeReaderAsync();

        Closed([this](auto const&, auto const&)
        {
            SaveWindowBounds();
        });
    }

    /* Configure la fenêtre avec le pont vers le moteur, les comptes, le
     * dossier et le message cible.
     * Paramètres :
     *   core             : pont vers evo-core
     *   accounts         : liste JSON des comptes configurés
     *   account          : identifiant du compte du message
     *   folder           : nom complet du dossier du message
     *   uid              : identifiant unique du message
     *   all_uids         : liste ordonnée des UID des messages affichés dans la vue
     *   account_folders  : arborescence des dossiers par compte (pour Déplacer)
     *   on_seen_changed  : rappel lorsque l'état lu/non lu change
     *   on_deleted       : rappel lorsque le message est supprimé
     *   on_moved         : rappel lorsque le message est déplacé */
    void MessageWindow::Setup(
        std::shared_ptr<::EvolutionWin::CoreBridge> const& core,
        JsonArray const& accounts,
        hstring const& account,
        hstring const& folder,
        hstring const& uid,
        std::vector<hstring> const& all_uids,
        std::map<std::wstring, JsonArray> const& account_folders,
        std::function<void(hstring const&, bool)> on_seen_changed,
        std::function<void(hstring const&)> on_deleted,
        std::function<void(hstring const&, hstring const&)> on_moved)
    {
        m_core = core;
        m_accounts = accounts;
        m_account = account;
        m_folder = folder;
        m_uid = uid;
        m_all_uids = all_uids;
        m_account_folders = account_folders;
        m_on_seen_changed = on_seen_changed;
        m_on_deleted = on_deleted;
        m_on_moved = on_moved;

        /* Trouver l'index courant dans la liste pour la navigation */
        m_current_index = 0;
        for (size_t ii = 0; ii < m_all_uids.size(); ++ii)
        {
            if (m_all_uids[ii] == m_uid)
            {
                m_current_index = ii;
                break;
            }
        }

        UpdateNavigationButtons();
        LoadCurrentMessage();
    }

    /* Met à jour l'activation des boutons Précédent et Suivant */
    void MessageWindow::UpdateNavigationButtons()
    {
        PrevButton().IsEnabled(!m_all_uids.empty() && m_current_index > 0);
        NextButton().IsEnabled(!m_all_uids.empty() && m_current_index + 1 < m_all_uids.size());
    }

    /* Charge et affiche le message courant via evo-core */
    void MessageWindow::LoadCurrentMessage()
    {
        auto core = m_core.lock();
        if (!core || m_account.empty() || m_folder.empty() || m_uid.empty())
            return;

        MessageLoadingRing().IsActive(true);
        uint64_t req = ++m_message_request;

        core->FetchMessage(m_account, m_folder, m_uid, [strong = get_strong(), req](hstring const& json, hstring const& error)
        {
            if (req != strong->m_message_request)
                return;

            strong->MessageLoadingRing().IsActive(false);

            if (!error.empty())
            {
                strong->ShowStatus(L"Erreur de chargement : " + error, InfoBarSeverity::Error);
                return;
            }

            JsonObject msg;
            if (!JsonObject::TryParse(json, msg))
                return;

            strong->m_current_message = msg;

            /* Marquer automatiquement comme lu à l'ouverture si non lu */
            bool seen = msg.GetNamedBoolean(L"seen", true);
            strong->m_seen = seen;
            if (!seen)
            {
                strong->m_seen = true;
                if (auto core_bridge = strong->m_core.lock())
                {
                    core_bridge->SetMessageFlags(strong->m_account, strong->m_folder, strong->m_uid,
                        L"seen", true, [](hstring const&, hstring const&) {});
                }
                if (strong->m_on_seen_changed)
                    strong->m_on_seen_changed(strong->m_uid, true);
            }

            /* Mise à jour du bouton Lu / Non lu */
            strong->ToggleReadButton().Label(strong->m_seen ? L"Non lu" : L"Lu");
            strong->ToggleReadIcon().Glyph(strong->m_seen ? L"\xE715" : L"\xE8C3");

            /* Remplissage des champs d'en-tête */
            hstring subject = msg.GetNamedString(L"subject", L"(Sans objet)");
            strong->SubjectText().Text(subject);
            strong->TitleText().Text(subject);
            strong->Title(subject);

            strong->FromText().Text(SenderName(msg.GetNamedString(L"from", L"")));
            strong->DateText().Text(FormatDate(msg.GetNamedNumber(L"date", 0)));

            hstring to = msg.GetNamedString(L"to", L"");
            hstring cc = msg.GetNamedString(L"cc", L"");
            strong->ToText().Text(L"À : " + to);
            if (!cc.empty())
            {
                strong->CcText().Text(L"Cc : " + cc);
                strong->CcText().Visibility(Visibility::Visible);
            }
            else
            {
                strong->CcText().Visibility(Visibility::Collapsed);
            }

            strong->UpdateSecurityBanner(msg.GetNamedObject(L"security", JsonObject{}));
            strong->PopulateAttachments(msg.GetNamedArray(L"attachments", JsonArray{}));
            strong->ShowCurrentMessageInReader(false);
            strong->UpdateNavigationButtons();
        });
    }

    /* Précédent : navigue vers le message précédent dans la liste */
    void MessageWindow::OnPrevClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_current_index > 0 && !m_all_uids.empty())
        {
            --m_current_index;
            m_uid = m_all_uids[m_current_index];
            UpdateNavigationButtons();
            LoadCurrentMessage();
        }
    }

    /* Suivant : navigue vers le message suivant dans la liste */
    void MessageWindow::OnNextClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_current_index + 1 < m_all_uids.size() && !m_all_uids.empty())
        {
            ++m_current_index;
            m_uid = m_all_uids[m_current_index];
            UpdateNavigationButtons();
            LoadCurrentMessage();
        }
    }

    /* Répondre à l'expéditeur */
    void MessageWindow::OnReplyClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_current_message)
            return;
        auto core = m_core.lock();
        if (!core)
            return;

        auto data = ::EvolutionWin::Helpers::BuildReplyComposeData(
            ::EvolutionWin::ReplyKind::Sender, m_current_message, m_account, m_accounts);
        auto compose = make<ComposeWindow>();
        get_self<ComposeWindow>(compose)->Setup(core, m_accounts, data);
        compose.Activate();
    }

    /* Répondre à tous */
    void MessageWindow::OnReplyAllClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_current_message)
            return;
        auto core = m_core.lock();
        if (!core)
            return;

        auto data = ::EvolutionWin::Helpers::BuildReplyComposeData(
            ::EvolutionWin::ReplyKind::All, m_current_message, m_account, m_accounts);
        auto compose = make<ComposeWindow>();
        get_self<ComposeWindow>(compose)->Setup(core, m_accounts, data);
        compose.Activate();
    }

    /* Transférer */
    void MessageWindow::OnForwardClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_current_message)
            return;
        auto core = m_core.lock();
        if (!core)
            return;

        auto data = ::EvolutionWin::Helpers::BuildReplyComposeData(
            ::EvolutionWin::ReplyKind::Forward, m_current_message, m_account, m_accounts);
        auto compose = make<ComposeWindow>();
        get_self<ComposeWindow>(compose)->Setup(core, m_accounts, data);
        compose.Activate();
    }

    /**
     * Transfère le message courant sous forme de pièce jointe (.eml).
     * Exporte le courriel via l'API CoreBridge et ouvre ComposeWindow
     * avec le fichier .eml attaché.
     * Paramètres :
     *   sender : bouton ayant émis le clic
     *   args   : arguments de l'événement routé
     */
    void MessageWindow::OnForwardAsAttachmentClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_uid.empty() || m_account.empty() || m_folder.empty())
            return;
        auto core = m_core.lock();
        if (!core)
            return;

        hstring first_subject;
        if (m_current_message)
        {
            first_subject = m_current_message.GetNamedString(L"subject", L"");
        }

        JsonObject req;
        req.SetNamedValue(L"account", JsonValue::CreateStringValue(m_account));
        req.SetNamedValue(L"folder", JsonValue::CreateStringValue(m_folder));
        JsonArray uid_array;
        uid_array.Append(JsonValue::CreateStringValue(m_uid));
        req.SetNamedValue(L"uids", uid_array);

        hstring account = m_account;
        ShowStatus(L"Préparation du courriel en pièce jointe…", InfoBarSeverity::Informational);

        core->ExportMessagesEml(req.Stringify(),
            [this, core, account, first_subject](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Transfert : " + error, InfoBarSeverity::Error);
                return;
            }

            JsonArray paths;
            if (!JsonArray::TryParse(json, paths))
                return;

            ::EvolutionWin::ComposeData data;
            data.account = account;
            data.reply = true;
            if (!first_subject.empty())
            {
                data.subject = StartsWithNoCase(first_subject, L"TR:") || StartsWithNoCase(first_subject, L"Fwd:")
                    ? first_subject
                    : L"TR: " + first_subject;
            }
            else
            {
                data.subject = L"TR: Message transféré";
            }

            for (auto const& val : paths)
                data.attachments.push_back(val.GetString());

            auto compose = make<ComposeWindow>();
            get_self<ComposeWindow>(compose)->Setup(core, m_accounts, data);
            compose.Activate();
        });
    }

    /* Bascule Lu / Non lu */
    void MessageWindow::OnToggleReadClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto core = m_core.lock();
        if (!core || m_uid.empty())
            return;

        m_seen = !m_seen;
        ToggleReadButton().Label(m_seen ? L"Non lu" : L"Lu");
        ToggleReadIcon().Glyph(m_seen ? L"\xE715" : L"\xE8C3");

        core->SetMessageFlags(m_account, m_folder, m_uid, L"seen", m_seen,
            [strong = get_strong()](hstring const&, hstring const& error)
        {
            if (!error.empty())
                strong->ShowStatus(L"Erreur lors de la mise à jour : " + error, InfoBarSeverity::Error);
        });

        if (m_on_seen_changed)
            m_on_seen_changed(m_uid, m_seen);
    }

    /* Remplit le sous-menu de déplacement avec les dossiers du compte */
    void MessageWindow::OnMoveMenuOpening(IInspectable const&, IInspectable const&)
    {
        MoveMenu().Items().Clear();
        auto it = m_account_folders.find(std::wstring{ m_account });
        if (it == m_account_folders.end() || !it->second)
            return;

        std::function<void(JsonArray const&, MenuFlyoutItemBase const&)> append_folders;
        append_folders = [this, &append_folders](JsonArray const& list, MenuFlyoutItemBase const& parent_menu)
        {
            for (auto const& item_val : list)
            {
                auto f = item_val.GetObject();
                hstring full_name = f.GetNamedString(L"full_name", L"");
                hstring name = f.GetNamedString(L"name", full_name);
                hstring type = f.GetNamedString(L"type", L"");
                auto children = f.GetNamedArray(L"children", JsonArray{});

                if (full_name == m_folder)
                    continue;

                if (children.Size() > 0)
                {
                    MenuFlyoutSubItem sub;
                    sub.Text(name);
                    sub.Icon(CreateFolderIcon(type));
                    append_folders(children, sub);
                    if (auto parent_sub = parent_menu.try_as<MenuFlyoutSubItem>())
                        parent_sub.Items().Append(sub);
                    else
                        MoveMenu().Items().Append(sub);
                }
                else
                {
                    MenuFlyoutItem menu_item;
                    menu_item.Text(name);
                    menu_item.Icon(CreateFolderIcon(type));
                    menu_item.Click([this, full_name](IInspectable const&, RoutedEventArgs const&)
                    {
                        auto core = m_core.lock();
                        if (!core)
                            return;
                        hstring moved_uid = m_uid;
                        core->MoveMessage(m_account, m_folder, moved_uid, m_account, full_name,
                            [strong = get_strong(), moved_uid, full_name](hstring const&, hstring const& error)
                        {
                            if (!error.empty())
                            {
                                strong->ShowStatus(L"Erreur de déplacement : " + error, InfoBarSeverity::Error);
                                return;
                            }
                            if (strong->m_on_moved)
                                strong->m_on_moved(moved_uid, full_name);

                            /* Avancer au message suivant ou précédent, sinon fermer */
                            if (strong->m_current_index + 1 < strong->m_all_uids.size())
                                strong->OnNextClick(nullptr, nullptr);
                            else if (strong->m_current_index > 0)
                                strong->OnPrevClick(nullptr, nullptr);
                            else
                                strong->Close();
                        });
                    });

                    if (auto parent_sub = parent_menu.try_as<MenuFlyoutSubItem>())
                        parent_sub.Items().Append(menu_item);
                    else
                        MoveMenu().Items().Append(menu_item);
                }
            }
        };

        append_folders(it->second, nullptr);
    }

    /* Supprime le message courant */
    void MessageWindow::OnDeleteClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto core = m_core.lock();
        if (!core || m_uid.empty())
            return;

        hstring deleted_uid = m_uid;
        core->DeleteMessage(m_account, m_folder, deleted_uid,
            [strong = get_strong(), deleted_uid](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                strong->ShowStatus(L"Erreur de suppression : " + error, InfoBarSeverity::Error);
                return;
            }
            if (strong->m_on_deleted)
                strong->m_on_deleted(deleted_uid);

            /* Avancer au message suivant ou précédent, sinon fermer */
            if (strong->m_current_index + 1 < strong->m_all_uids.size())
                strong->OnNextClick(nullptr, nullptr);
            else if (strong->m_current_index > 0)
                strong->OnPrevClick(nullptr, nullptr);
            else
                strong->Close();
        });
    }

    /* Impression native du message */
    void MessageWindow::OnPrintClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_reader_ready)
            return;
        try
        {
            ReaderWebView().CoreWebView2().ShowPrintUI(Microsoft::Web::WebView2::Core::CoreWebView2PrintDialogKind::Browser);
        }
        catch (hresult_error const& e)
        {
            ShowStatus(L"Impression impossible : " + e.message(), InfoBarSeverity::Warning);
        }
    }

    /* Initialisation asynchrone du composant WebView2 */
    fire_and_forget MessageWindow::InitializeReaderAsync()
    {
        auto strong = get_strong();

        wchar_t local_app_data[MAX_PATH];
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH))
        {
            std::wstring folder = std::wstring{ local_app_data } + L"\\EvolutionWin\\WebView2";
            SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", folder.c_str());
        }

        ReaderWebView().DefaultBackgroundColor(Microsoft::UI::Colors::White());
        try
        {
            co_await ReaderWebView().EnsureCoreWebView2Async();
        }
        catch (...)
        {
            co_return;
        }
        ReaderWebView().DefaultBackgroundColor(Microsoft::UI::Colors::White());

        auto core = ReaderWebView().CoreWebView2();
        if (!core)
            co_return;

        core.Profile().PreferredColorScheme(Microsoft::Web::WebView2::Core::CoreWebView2PreferredColorScheme::Light);

        auto settings = core.Settings();
        settings.IsScriptEnabled(false);
        settings.AreDevToolsEnabled(false);
        settings.IsStatusBarEnabled(true);

        core.AddWebResourceRequestedFilter(L"*", Microsoft::Web::WebView2::Core::CoreWebView2WebResourceContext::All);
        core.WebResourceRequested([this](auto const& sender, auto const& args)
        {
            OnReaderResourceRequested(sender, args);
        });

        core.NavigationStarting([this](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NavigationStartingEventArgs const& args)
        {
            std::wstring_view uri{ args.Uri() };
            if (uri == std::wstring_view{ m_reader_uri })
            {
                m_reader_navigation_id = args.NavigationId();
                m_reader_navigation_known = true;
                return;
            }

            if (args.IsUserInitiated())
            {
                if (uri.rfind(L"data:", 0) == 0 || uri.rfind(L"about:", 0) == 0)
                    return;
                args.Cancel(true);
                if (!StartsWith(uri, ReaderOrigin))
                    Windows::System::Launcher::LaunchUriAsync(Uri{ args.Uri() });
                return;
            }

            if (uri != L"about:blank")
                args.Cancel(true);
        });

        core.ContentLoading([this](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2ContentLoadingEventArgs const& args)
        {
            if (!m_reader_navigation_known || args.NavigationId() != m_reader_navigation_id)
                return;

            m_reader_navigation_known = false;
            m_reader_document_active = true;
            m_reader_remote_allowed = m_reader_pending_allow;
            m_reader_blocked_count = 0;
        });

        core.NewWindowRequested([](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NewWindowRequestedEventArgs const& args)
        {
            args.Handled(true);
            Windows::System::Launcher::LaunchUriAsync(Uri{ args.Uri() });
        });

        m_reader_ready = true;
        if (!m_reader_uri.empty())
            core.Navigate(m_reader_uri);
    }

    void MessageWindow::ShowInReader(hstring const& html, bool allow_remote)
    {
        ++m_reader_generation;
        m_reader_uri = hstring{ std::wstring{ ReaderOrigin } + L"message-win-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(m_reader_generation) };
        m_reader_html = html;
        m_reader_pending_allow = allow_remote;
        m_reader_navigation_known = false;
        m_reader_document_active = false;
        m_reader_remote_allowed = false;
        m_reader_blocked_count = 0;
        RemoteContentBar().IsOpen(false);

        if (!m_reader_ready)
            return;

        try
        {
            ReaderWebView().CoreWebView2().Navigate(m_reader_uri);
        }
        catch (hresult_error const& e)
        {
            ShowStatus(L"Impossible d'afficher le message : " + e.message(), InfoBarSeverity::Warning);
        }
    }

    void MessageWindow::ShowCurrentMessageInReader(bool force_remote)
    {
        if (!m_current_message)
            return;

        bool allow = force_remote || IsRemoteContentTrusted(m_current_message.GetNamedString(L"from", L""));
        ShowInReader(m_current_message.GetNamedString(L"html", L""), allow);
    }

    void MessageWindow::OnReaderResourceRequested(
        Microsoft::Web::WebView2::Core::CoreWebView2 const& sender,
        Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& args)
    {
        hstring request_uri = args.Request().Uri();
        std::wstring_view uri{ request_uri };

        if (StartsWith(ToLower(uri.substr(0, ReaderOrigin.size())), ReaderOrigin))
        {
            if (request_uri == m_reader_uri)
                ServeReaderDocumentAsync(sender, args, m_reader_html);
            else
                args.Response(sender.Environment().CreateWebResourceResponse(nullptr, 404, L"Not Found", L""));
            return;
        }

        if (!IsRemoteUri(uri))
            return;

        if (m_reader_document_active && m_reader_remote_allowed)
            return;

        args.Response(sender.Environment().CreateWebResourceResponse(nullptr, 403, L"Blocked", L""));

        if (m_reader_document_active)
        {
            ++m_reader_blocked_count;
            UpdateRemoteContentBanner();
        }
    }

    fire_and_forget MessageWindow::ServeReaderDocumentAsync(
        Microsoft::Web::WebView2::Core::CoreWebView2 sender,
        Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs args,
        hstring html)
    {
        auto deferral = args.GetDeferral();
        auto strong = get_strong();

        try
        {
            std::string utf8 = to_string(html);
            Windows::Storage::Streams::InMemoryRandomAccessStream stream;
            Windows::Storage::Streams::DataWriter writer{ stream };
            writer.WriteBytes(array_view<uint8_t const>(
                reinterpret_cast<uint8_t const*>(utf8.data()),
                reinterpret_cast<uint8_t const*>(utf8.data() + utf8.size())));
            co_await writer.StoreAsync();
            writer.DetachStream();
            stream.Seek(0);

            auto response = sender.Environment().CreateWebResourceResponse(
                stream, 200, L"OK", L"Content-Type: text/html; charset=utf-8\r\n");
            args.Response(response);
        }
        catch (...)
        {
            args.Response(sender.Environment().CreateWebResourceResponse(nullptr, 500, L"Error", L""));
        }

        deferral.Complete();
    }

    void MessageWindow::UpdateRemoteContentBanner()
    {
        if (m_reader_remote_allowed || m_reader_blocked_count == 0)
        {
            RemoteContentBar().IsOpen(false);
            return;
        }
        RemoteContentBar().IsOpen(true);
    }

    void MessageWindow::OnAllowRemoteForMessageClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowCurrentMessageInReader(true);
    }

    void MessageWindow::OnTrustSenderClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_current_message)
            return;
        auto address = SenderAddress(m_current_message.GetNamedString(L"from", L""));
        if (!address.empty())
        {
            auto list = ReadTrustList(TrustedSendersKey);
            if (std::find(list.begin(), list.end(), address) == list.end())
            {
                list.push_back(address);
                WriteTrustList(TrustedSendersKey, list);
            }
        }
        ShowCurrentMessageInReader(true);
    }

    void MessageWindow::OnTrustDomainClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_current_message)
            return;
        auto address = SenderAddress(m_current_message.GetNamedString(L"from", L""));
        auto domain = AddressDomain(address);
        if (!domain.empty())
        {
            auto list = ReadTrustList(TrustedDomainsKey);
            if (std::find(list.begin(), list.end(), domain) == list.end())
            {
                list.push_back(domain);
                WriteTrustList(TrustedDomainsKey, list);
            }
        }
        ShowCurrentMessageInReader(true);
    }

    void MessageWindow::UpdateSecurityBanner(JsonObject const& sec)
    {
        if (!sec || sec.Size() == 0)
        {
            SecurityBanner().IsOpen(false);
            return;
        }

        bool encrypted = sec.GetNamedBoolean(L"encrypted", false);
        bool signed_msg = sec.GetNamedBoolean(L"signed", false);
        bool valid = sec.GetNamedBoolean(L"valid", false);
        hstring signer = sec.GetNamedString(L"signer", L"");
        hstring protocol = sec.GetNamedString(L"protocol", L"");

        if (!encrypted && !signed_msg)
        {
            SecurityBanner().IsOpen(false);
            return;
        }

        std::wstring msg;
        if (encrypted)
            msg += L"Ce message a été déchiffré avec succès (" + std::wstring{ protocol } + L"). ";
        if (signed_msg)
        {
            if (valid)
                msg += L"Signature numérique valide de " + std::wstring{ signer } + L".";
            else
                msg += L"La signature numérique ne peut pas être vérifiée.";
        }

        SecurityBanner().Severity(valid ? InfoBarSeverity::Success : (signed_msg ? InfoBarSeverity::Warning : InfoBarSeverity::Informational));
        SecurityBanner().Title(encrypted ? L"Message chiffré" : L"Message signé");
        SecurityBanner().Message(msg);
        SecurityBanner().IsOpen(true);
    }

    void MessageWindow::PopulateAttachments(JsonArray const& attachments)
    {
        AttachmentList().Children().Clear();

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
            ToolTipService::SetToolTip(button, box_value(L"Enregistrer « " + filename + L" »"));

            button.Click([this, index, filename](IInspectable const&, RoutedEventArgs const&)
            {
                auto core = m_core.lock();
                if (!core)
                    return;

                Windows::Storage::Pickers::FileSavePicker picker;
                HWND hwnd = nullptr;
                if (auto native = try_as<::IWindowNative>())
                    native->get_WindowHandle(&hwnd);
                if (hwnd)
                    picker.as<::IInitializeWithWindow>()->Initialize(hwnd);

                picker.SuggestedFileName(filename);
                std::wstring ext = L".bin";
                std::wstring_view sv{ filename };
                auto dot = sv.find_last_of(L'.');
                if (dot != std::wstring_view::npos)
                    ext = std::wstring{ sv.substr(dot) };
                picker.FileTypeChoices().Insert(L"Fichier", single_threaded_vector<hstring>({ hstring{ ext } }));

                [this, core, index, picker]() -> fire_and_forget
                {
                    auto file = co_await picker.PickSaveFileAsync();
                    if (!file)
                        co_return;
                    hstring path = file.Path();

                    core->SaveAttachment(m_account, m_folder, m_uid, index, path,
                        [strong = get_strong(), path](hstring const&, hstring const& error)
                    {
                        if (!error.empty())
                            strong->ShowStatus(L"Erreur d'enregistrement : " + error, InfoBarSeverity::Error);
                        else
                            strong->ShowStatus(L"Pièce jointe enregistrée : « " + path + L" ».", InfoBarSeverity::Success);
                    });
                }();
            });

            AttachmentList().Children().Append(button);
        }

        SaveAllAttachmentsButton().Visibility(attachments.Size() > 1 ? Visibility::Visible : Visibility::Collapsed);
        AttachmentBar().Visibility(Visibility::Visible);
    }

    fire_and_forget MessageWindow::OnSaveAllAttachmentsClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto core = m_core.lock();
        if (!core || !m_current_message)
            co_return;

        auto attachments = m_current_message.GetNamedArray(L"attachments", JsonArray{});
        if (attachments.Size() == 0)
            co_return;

        Windows::Storage::Pickers::FolderPicker picker;
        HWND hwnd = nullptr;
        if (auto native = try_as<::IWindowNative>())
            native->get_WindowHandle(&hwnd);
        if (hwnd)
            picker.as<::IInitializeWithWindow>()->Initialize(hwnd);

        picker.FileTypeFilter().Append(L"*");
        auto folder = co_await picker.PickSingleFolderAsync();
        if (!folder)
            co_return;

        hstring dir = folder.Path();
        for (auto const& item : attachments)
        {
            auto att = item.GetObject();
            uint32_t index = static_cast<uint32_t>(att.GetNamedNumber(L"index", 0));
            hstring filename = att.GetNamedString(L"filename", L"piece-jointe");
            hstring target_path = dir + L"\\" + filename;

            core->SaveAttachment(m_account, m_folder, m_uid, index, target_path,
                [strong = get_strong(), target_path](hstring const&, hstring const& error)
            {
                if (!error.empty())
                    strong->ShowStatus(L"Erreur pour « " + target_path + L" » : " + error, InfoBarSeverity::Warning);
            });
        }
        ShowStatus(L"Pièces jointes enregistrées dans « " + dir + L" ».", InfoBarSeverity::Success);
    }

    void MessageWindow::ShowStatus(hstring const& message, InfoBarSeverity severity)
    {
        StatusBar().Message(message);
        StatusBar().Severity(severity);
        StatusBar().IsOpen(true);
    }

    void MessageWindow::RestoreWindowBounds()
    {
        int x = static_cast<int>(::EvolutionWin::Settings::Instance().GetNumber(L"window.message.x", -1));
        int y = static_cast<int>(::EvolutionWin::Settings::Instance().GetNumber(L"window.message.y", -1));
        int w = static_cast<int>(::EvolutionWin::Settings::Instance().GetNumber(L"window.message.width", 960));
        int h = static_cast<int>(::EvolutionWin::Settings::Instance().GetNumber(L"window.message.height", 720));

        if (w >= 400 && h >= 300)
        {
            if (x >= 0 && y >= 0)
                AppWindow().MoveAndResize({ x, y, w, h });
            else
                AppWindow().Resize({ w, h });
        }
    }

    void MessageWindow::SaveWindowBounds()
    {
        auto pos = AppWindow().Position();
        auto size = AppWindow().Size();
        if (size.Width >= 400 && size.Height >= 300)
        {
            ::EvolutionWin::Settings::Instance().SetNumber(L"window.message.x", pos.X);
            ::EvolutionWin::Settings::Instance().SetNumber(L"window.message.y", pos.Y);
            ::EvolutionWin::Settings::Instance().SetNumber(L"window.message.width", size.Width);
            ::EvolutionWin::Settings::Instance().SetNumber(L"window.message.height", size.Height);
        }
    }

    /**
     * @brief Ouvre le message visualisé dans le navigateur web par défaut du système.
     * @param sender Bouton de commande émetteur.
     * @param args Arguments de l'événement de clic.
     */
    void MessageWindow::OnOpenInBrowserClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        if (!m_current_message)
            return;

        bool ok = ::EvolutionWin::Helpers::OpenInDefaultBrowser(m_current_message, m_reader_html);
        if (!ok)
            ShowStatus(L"Impossible d'ouvrir le navigateur web par défaut.", InfoBarSeverity::Error);
    }
}
