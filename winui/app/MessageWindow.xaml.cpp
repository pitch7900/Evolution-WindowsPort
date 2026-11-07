#include "pch.h"

#include "MessageWindow.xaml.h"

#include "ComposeWindow.xaml.h"

#include "MainWindow.Helpers.h"
#include "UiTranslate.h"

#include "WindowIcon.h"

#include "Settings.h"
#include <fstream>

#include <microsoft.ui.xaml.window.h>

#include <shobjidl.h>

#include <winrt/Windows.System.h>

#include <winrt/Windows.Storage.h>

#include <winrt/Windows.Storage.Pickers.h>

#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>

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
        ::EvolutionWin::UiTranslate::ApplyToWindow(*this);

        ExtendsContentIntoTitleBar(true);

        SetTitleBar(AppTitleBar());

        ::EvolutionWin::SetWindowIcon(AppWindow());

        RestoreWindowBounds();

        InitializeReaderAsync();

        Closed([this](auto const&, auto const&)

        {

            StopSpeech();

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

        StopSpeech();

        uint64_t req = ++m_message_request;

        core->FetchMessage(m_account, m_folder, m_uid, [strong = get_strong(), req](hstring const& json, hstring const& error)

        {

            if (req != strong->m_message_request)

                return;

            strong->MessageLoadingRing().IsActive(false);

            if (!error.empty())

            {

                strong->ShowStatus(Fmt(Tr(L"Erreur de chargement : {0}"), { error }), InfoBarSeverity::Error);

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

            strong->ToggleReadButton().Label(strong->m_seen ? Tr(L"Non lu") : Tr(L"Lu"));

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

            strong->ToText().Text(Fmt(Tr(L"À : {0}"), { to }));

            if (!cc.empty())

            {

                strong->CcText().Text(Fmt(Tr(L"Cc : {0}"), { cc }));

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

        ShowStatus(Tr(L"Préparation du courriel en pièce jointe…"), InfoBarSeverity::Informational);

        core->ExportMessagesEml(req.Stringify(),

            [this, core, account, first_subject](hstring const& json, hstring const& error)

        {

            if (!error.empty())

            {

                ShowStatus(Fmt(Tr(L"Transfert : {0}"), { error }), InfoBarSeverity::Error);

                return;

            }

            JsonArray paths;

            if (!JsonArray::TryParse(json, paths))

                return;

            ::EvolutionWin::ComposeData data;

            data.account = account;

            data.reply = true;

            std::wstring fwd_p{ ::EvolutionWin::Settings::Instance().GetString(L"mail.forward-prefix", L"TR: ") };
            if (!first_subject.empty())
            {
                data.subject = hstring{ FormatForwardSubject(first_subject, fwd_p) };
            }
            else
            {
                data.subject = hstring{ FormatForwardSubject(std::wstring{ Tr(L"Message transféré") }, fwd_p) };

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

        ToggleReadButton().Label(m_seen ? Tr(L"Non lu") : Tr(L"Lu"));

        ToggleReadIcon().Glyph(m_seen ? L"\xE715" : L"\xE8C3");

        core->SetMessageFlags(m_account, m_folder, m_uid, L"seen", m_seen,

            [strong = get_strong()](hstring const&, hstring const& error)

        {

            if (!error.empty())

                strong->ShowStatus(Fmt(Tr(L"Erreur lors de la mise à jour : {0}"), { error }), InfoBarSeverity::Error);

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

                                strong->ShowStatus(Fmt(Tr(L"Erreur de déplacement : {0}"), { error }), InfoBarSeverity::Error);

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

                strong->ShowStatus(Fmt(Tr(L"Erreur de suppression : {0}"), { error }), InfoBarSeverity::Error);

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

            ShowStatus(Fmt(Tr(L"Impression impossible : {0}"), { e.message() }), InfoBarSeverity::Warning);

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

        
        m_reader_zoom = ::EvolutionWin::Settings::Instance().GetDouble(L"mail.reading-zoom", 1.0);
        m_view_plain_text = ::EvolutionWin::Settings::Instance().GetBoolean(L"mail.reading-plain-text", false);

        core.NavigationCompleted([this](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NavigationCompletedEventArgs const&)
        {
            if (m_reader_zoom != 1.0)
            {
                ApplyReaderZoom(m_reader_zoom);
            }
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

            ShowStatus(Fmt(Tr(L"Impossible d'afficher le message : {0}"), { e.message() }), InfoBarSeverity::Warning);

        }

    }

    void MessageWindow::ShowCurrentMessageInReader(bool force_remote)
    {
        if (!m_current_message)
            return;

        if (m_view_plain_text)
        {
            hstring body_text = m_current_message.GetNamedString(L"body", L"");
            if (body_text.empty())
                body_text = m_current_message.GetNamedString(L"text", L"");
            if (body_text.empty())
                body_text = ::EvolutionWin::Helpers::ExtractPlainTextForSpeech(m_current_message);

            std::wstring plain_html = L"<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>"
                L"body { margin:0; padding:16px; background:#fff; color:#1a1a1a; font-family:'Segoe UI Variable Text','Segoe UI',sans-serif; font-size:14px; line-height:1.6; word-break:break-word; white-space:pre-wrap; }"
                L"</style></head><body>" + ::EvolutionWin::Helpers::LinkifyEscapedHtml(::EvolutionWin::Helpers::EscapeHtml(body_text)) + L"</body></html>";

            ShowInReader(hstring{ plain_html }, false);
            return;
        }

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

        bool trusted = sec.GetNamedBoolean(L"trusted", false);
        hstring desc = sec.GetNamedString(L"description", L"");

        hstring title;
        InfoBarSeverity severity = InfoBarSeverity::Warning;
        if (encrypted && signed_msg)
        {
            title = valid ? Tr(L"Message chiffré et signé") : Tr(L"Message chiffré, mais la signature pose problème");
            severity = valid ? (trusted ? InfoBarSeverity::Success : InfoBarSeverity::Informational) : InfoBarSeverity::Warning;
        }
        else if (encrypted)
        {
            title = valid ? Tr(L"Message chiffré : lisible par vous seul") : Tr(L"Ce message chiffré n'a pas pu être lu");
            severity = valid ? InfoBarSeverity::Success : InfoBarSeverity::Warning;
        }
        else if (valid)
        {
            title = trusted ? Tr(L"Message signé et vérifié") : Tr(L"Message signé : contenu intact, expéditeur non certifié");
            severity = trusted ? InfoBarSeverity::Success : InfoBarSeverity::Informational;
        }
        else
        {
            title = Tr(L"Attention : la signature de ce message n'est pas fiable");
        }

        std::wstring msg;
        if (!signer.empty())
            msg = std::wstring{ Fmt(Tr(L"Signataire : {0}"), { signer }) };
        if (!desc.empty())
        {
            if (!msg.empty())
                msg += L" — ";
            msg += desc;
        }

        SecurityBanner().Severity(severity);
        SecurityBanner().Title(title);
        SecurityBanner().Message(hstring{ msg });

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

            hstring size_str = ::EvolutionWin::Helpers::FormatFileSize(size);
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

            ToolTipService::SetToolTip(button, box_value(Fmt(Tr(L"Aperçu de « {0} » (clic pour afficher, clic droit pour enregistrer)"), { hstring{ filename } })));



            // Clic direct : aperçu sécurisé

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

                auto core = m_core.lock();

                if (!core || m_account.empty() || m_folder.empty() || m_uid.empty())

                    return;



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



                [strong = get_strong(), index, filename, picker]() -> fire_and_forget

                {

                    auto file = co_await picker.PickSaveFileAsync();

                    if (!file)

                        co_return;



                    hstring dest_path = file.Path();

                    if (auto c = strong->m_core.lock())

                    {

                        c->SaveAttachment(strong->m_account, strong->m_folder, strong->m_uid, index, dest_path,

                            [strong, filename](hstring const&, hstring const& error)

                        {

                            if (!error.empty())

                            {

                                strong->ShowStatus(Fmt(Tr(L"Erreur lors de l'enregistrement : {0}"), { error }), InfoBarSeverity::Error);

                                return;

                            }

                            strong->ShowStatus(Fmt(Tr(L"Pièce jointe « {0} » enregistrée."), { hstring{ filename } }), InfoBarSeverity::Success);

                        });

                    }

                }();

            });

            flyout.Items().Append(save_item);



            button.ContextFlyout(flyout);

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

                    strong->ShowStatus(Fmt(Tr(L"Erreur pour « {0} » : {1}"), { hstring{ target_path }, error }), InfoBarSeverity::Warning);

            });

        }

        ShowStatus(Fmt(Tr(L"Pièces jointes enregistrées dans « {0} »."), { hstring{ dir } }), InfoBarSeverity::Success);

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

            ShowStatus(Tr(L"Impossible d'ouvrir le navigateur web par défaut."), InfoBarSeverity::Error);

    }

    /**

     * @brief Arrête la synthèse vocale en cours et réinitialise l'icône du bouton dans la fenêtre de lecture.

     */

    void MessageWindow::StopSpeech()

    {

        if (m_speech_player)

        {

            try

            {

                m_speech_player.Pause();

                m_speech_player.Source(nullptr);

            }

            catch (...)

            {

            }

        }

        m_is_speaking = false;

        try

        {

            ReadAloudIcon().Glyph(L"\xEC92");

            ReadAloudButton().Label(Tr(L"Lire à voix haute"));

        }

        catch (...)

        {

        }

    }

    /**

     * @brief Déclenche ou interrompt la lecture à voix haute du message courant via Windows.Media.SpeechSynthesis.

     * @param sender Bouton émetteur de l'action.

     * @param args Arguments de l'événement routé.

     */

    fire_and_forget MessageWindow::OnReadAloudClick(

        [[maybe_unused]] IInspectable const& sender,

        [[maybe_unused]] RoutedEventArgs const& args)

    {

        if (m_is_speaking)

        {

            StopSpeech();

            ShowStatus(Tr(L"Lecture vocale interrompue."), InfoBarSeverity::Informational);

            co_return;

        }

        if (!m_current_message)

        {

            ShowStatus(Tr(L"Aucun message chargé à lire."), InfoBarSeverity::Informational);

            co_return;

        }

        std::wstring speech_text = ::EvolutionWin::Helpers::ExtractPlainTextForSpeech(m_current_message, m_reader_html);

        if (speech_text.empty())

        {

            ShowStatus(Tr(L"Le message ne contient aucun texte à lire."), InfoBarSeverity::Warning);

            co_return;

        }

        try

        {

            if (!m_speech_synth)

            {

                m_speech_synth = Windows::Media::SpeechSynthesis::SpeechSynthesizer();

            }

            if (!m_speech_player)

            {

                m_speech_player = Windows::Media::Playback::MediaPlayer();

                m_speech_player.MediaEnded([this](Windows::Media::Playback::MediaPlayer const&, IInspectable const&)

                {

                    DispatcherQueue().TryEnqueue([this]()

                    {

                        StopSpeech();

                    });

                });

            }

            auto stream = co_await m_speech_synth.SynthesizeTextToStreamAsync(hstring{ speech_text });

            auto source = Windows::Media::Core::MediaSource::CreateFromStream(stream, stream.ContentType());

            m_speech_player.Source(source);

            m_speech_player.Play();

            m_is_speaking = true;

            ReadAloudIcon().Glyph(L"\xE71A");

            ReadAloudButton().Label(Tr(L"Arrêter la lecture"));

            ShowStatus(Tr(L"Lecture à voix haute en cours..."), InfoBarSeverity::Informational);

        }

        catch (hresult_error const& ex)

        {

            StopSpeech();

            ShowStatus(Fmt(Tr(L"Erreur lors de la synthèse vocale : {0}"), { ex.message() }), InfoBarSeverity::Error);

        }

        catch (...)

        {

            StopSpeech();

            ShowStatus(Tr(L"Erreur inattendue lors de la lecture vocale."), InfoBarSeverity::Error);

        }

    }



    /**

     * @brief Ferme le panneau d'aperçu de pièce jointe et réaffiche le corps du message.

     */

    void MessageWindow::CloseAttachmentPreview()

    {

        AttachmentPreviewContainer().Visibility(Visibility::Collapsed);

        ReaderWebView().Visibility(Visibility::Visible);



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

     * @brief Gestionnaire de clic pour fermer l'aperçu et retourner au corps du message.

     */

    void MessageWindow::OnCloseAttachmentPreviewClick(

        [[maybe_unused]] IInspectable const& sender,

        [[maybe_unused]] RoutedEventArgs const& args)

    {

        CloseAttachmentPreview();

    }



    /**

     * @brief Télécharge temporairement et affiche un aperçu sécurisé d'une pièce jointe dans la fenêtre indépendante.

     * @param index Indice de la pièce jointe dans le message.

     * @param filename Nom du fichier joint.

     */

    fire_and_forget MessageWindow::PreviewAttachmentAsync(uint32_t index, hstring filename)

    {

        auto strong = get_strong();

        auto core = m_core.lock();

        if (!core || m_account.empty() || m_folder.empty() || m_uid.empty())

            co_return;



        m_preview_current_index = index;

        m_preview_current_filename = filename;



        wchar_t temp_dir[MAX_PATH];

        DWORD len = GetTempPathW(MAX_PATH, temp_dir);

        if (len == 0 || len > MAX_PATH)

            co_return;



        std::wstring preview_dir = std::wstring{ temp_dir } + L"EvolutionWin\\Previews";

        CreateDirectoryW((std::wstring{ temp_dir } + L"EvolutionWin").c_str(), nullptr);

        CreateDirectoryW(preview_dir.c_str(), nullptr);



        std::wstring temp_file = preview_dir + L"\\preview_msg_" + std::to_wstring(index) + L"_" + std::wstring{ SanitizeFileName(filename, L"piece-jointe") };

        m_preview_current_path = hstring{ temp_file };



        ReaderWebView().Visibility(Visibility::Collapsed);

        AttachmentPreviewContainer().Visibility(Visibility::Visible);

        PreviewFilenameText().Text(filename);



        PreviewImageScroll().Visibility(Visibility::Collapsed);

        PreviewTextScroll().Visibility(Visibility::Collapsed);

        PreviewUnsupported().Visibility(Visibility::Collapsed);

        PreviewLoadingRing().IsActive(true);



        core->SaveAttachment(m_account, m_folder, m_uid, index, m_preview_current_path,

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

     * @brief Déclenche l'enregistrement sous de la pièce jointe prévisualisée.

     */

    void MessageWindow::OnPreviewSaveClick(

        [[maybe_unused]] IInspectable const& sender,

        [[maybe_unused]] RoutedEventArgs const& args)

    {

        Windows::Storage::Pickers::FileSavePicker picker;

        HWND hwnd = nullptr;

        if (auto native = try_as<::IWindowNative>())

            native->get_WindowHandle(&hwnd);

        if (hwnd)

            picker.as<::IInitializeWithWindow>()->Initialize(hwnd);



        picker.SuggestedFileName(m_preview_current_filename);

        std::wstring_view view{ m_preview_current_filename };

        auto dot = view.rfind(L'.');

        hstring ext = (dot != std::wstring_view::npos && dot + 1 < view.size())

            ? hstring{ view.substr(dot) }

            : L".bin";

        picker.FileTypeChoices().Insert(Tr(L"Fichier"), single_threaded_vector<hstring>({ ext }));



        [strong = get_strong(), picker]() -> fire_and_forget

        {

            auto file = co_await picker.PickSaveFileAsync();

            if (!file)

                co_return;



            hstring dest_path = file.Path();

            if (auto c = strong->m_core.lock())

            {

                c->SaveAttachment(strong->m_account, strong->m_folder, strong->m_uid, strong->m_preview_current_index, dest_path,

                    [strong](hstring const&, hstring const& error)

                {

                    if (!error.empty())

                    {

                        strong->ShowStatus(Fmt(Tr(L"Erreur lors de l'enregistrement : {0}"), { error }), InfoBarSeverity::Error);

                        return;

                    }

                    strong->ShowStatus(Tr(L"Pièce jointe enregistrée avec succès."), InfoBarSeverity::Success);

                });

            }

        }();

    }



    /**

     * @brief Ouvre le fichier prévisualisé dans l'application par défaut.

     */

    void MessageWindow::OnPreviewOpenClick(

        [[maybe_unused]] IInspectable const& sender,

        [[maybe_unused]] RoutedEventArgs const& args)

    {

        if (!m_preview_current_path.empty())

        {

            ShellExecuteW(nullptr, L"open", m_preview_current_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

        }

    }


    /**
     * @brief Gestionnaire de clic du bouton « Enregistrer sous » de la fenêtre autonome de message.
     * @param sender Bouton déclencheur.
     * @param args Arguments d'événement routé.
     */
    void MessageWindow::OnSaveAsClick([[maybe_unused]] IInspectable const& sender, [[maybe_unused]] RoutedEventArgs const& args)
    {
        SaveMessageAsAsync();
    }

    /**
     * @brief Exporte le message affiché au format .eml (RFC 822) vers l'emplacement choisi par l'utilisateur.
     */
    fire_and_forget MessageWindow::SaveMessageAsAsync()
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;

        if (m_uid.empty() || m_account.empty() || m_folder.empty())
            co_return;

        auto core = m_core.lock();
        if (!core)
            co_return;

        hstring name = m_current_message ? m_current_message.GetNamedString(L"subject", L"") : hstring{};
        hstring sanitized = ::EvolutionWin::Helpers::SanitizeFileName(name, L"message");

        Windows::Storage::Pickers::FileSavePicker picker;
        HWND hwnd = nullptr;
        if (auto native = try_as<::IWindowNative>())
            native->get_WindowHandle(&hwnd);
        if (hwnd)
            picker.as<::IInitializeWithWindow>()->Initialize(hwnd);

        picker.SuggestedFileName(sanitized);
        picker.FileTypeChoices().Insert(Tr(L"Message électronique"), single_threaded_vector<hstring>({ L".eml" }));

        auto file = co_await picker.PickSaveFileAsync();
        if (!file)
            co_return;

        hstring dest = file.Path();

        JsonObject request;
        request.SetNamedValue(L"account", JsonValue::CreateStringValue(m_account));
        request.SetNamedValue(L"folder", JsonValue::CreateStringValue(m_folder));
        JsonArray uids;
        uids.Append(JsonValue::CreateStringValue(m_uid));
        request.SetNamedValue(L"uids", uids);

        core->ExportMessagesEml(request.Stringify(), [this, dest](hstring const& json, hstring const& error)
        {
            JsonArray paths;
            if (!error.empty() || !JsonArray::TryParse(json, paths) || paths.Size() == 0)
                return;

            hstring exported = paths.GetStringAt(0);
            if (CopyFileW(exported.c_str(), dest.c_str(), FALSE))
            {
                DeleteFileW(exported.c_str());
            }
        });
    }

    /**
     * @brief Gestionnaire de clic pour afficher le flux brut MIME du message dans la fenêtre dédiée.
     * @param sender Contrôle bouton déclencheur.
     * @param args Arguments de l'événement de clic.
     */
    void MessageWindow::OnRawSourceClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        ShowRawMimeSourceAsync();
    }

    /**
     * @brief Affiche la source MIME brute du message affiché dans un dialogue avec recherche et copie.
     */
    fire_and_forget MessageWindow::ShowRawMimeSourceAsync()
    {
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        auto core = m_core.lock();
        if (!core || m_account.empty() || m_folder.empty() || m_uid.empty())
            co_return;

        JsonObject request;
        request.SetNamedValue(L"account", JsonValue::CreateStringValue(m_account));
        request.SetNamedValue(L"folder", JsonValue::CreateStringValue(m_folder));
        JsonArray uids;
        uids.Append(JsonValue::CreateStringValue(m_uid));
        request.SetNamedValue(L"uids", uids);

        core->ExportMessagesEml(request.Stringify(), [this](hstring const& json, hstring const& error)
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

    void MessageWindow::ApplyReaderZoom(double zoom)
    {
        m_reader_zoom = ::EvolutionWin::Helpers::ClampZoomFactor(zoom);
        ::EvolutionWin::Settings::Instance().SetDouble(L"mail.reading-zoom", m_reader_zoom);

        if (m_reader_ready && ReaderWebView().CoreWebView2())
        {
            std::wstring js = L"if (document.body) { document.body.style.zoom = '" + std::to_wstring(m_reader_zoom) + L"'; }";
            ReaderWebView().CoreWebView2().ExecuteScriptAsync(js);
        }
    }

    void MessageWindow::OnZoomInClick(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyReaderZoom(::EvolutionWin::Helpers::StepZoomIn(m_reader_zoom));
        ShowStatus(Fmt(Tr(L"Niveau de zoom : {0}"), { hstring{ ::EvolutionWin::Helpers::FormatZoomPercentage(m_reader_zoom) } }), Microsoft::UI::Xaml::Controls::InfoBarSeverity::Informational);
    }

    void MessageWindow::OnZoomOutClick(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyReaderZoom(::EvolutionWin::Helpers::StepZoomOut(m_reader_zoom));
        ShowStatus(Fmt(Tr(L"Niveau de zoom : {0}"), { hstring{ ::EvolutionWin::Helpers::FormatZoomPercentage(m_reader_zoom) } }), Microsoft::UI::Xaml::Controls::InfoBarSeverity::Informational);
    }

    void MessageWindow::OnZoomResetClick(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyReaderZoom(1.0);
        ShowStatus(Tr(L"Zoom réinitialisé à 100 %"), Microsoft::UI::Xaml::Controls::InfoBarSeverity::Informational);
    }

    void MessageWindow::OnTogglePlainTextClick(IInspectable const&, RoutedEventArgs const&)
    {
        m_view_plain_text = !m_view_plain_text;
        ::EvolutionWin::Settings::Instance().SetBoolean(L"mail.reading-plain-text", m_view_plain_text);

        if (m_current_message)
        {
            ShowCurrentMessageInReader(false);
            ShowStatus(m_view_plain_text ? Tr(L"Affichage en texte brut activé") : Tr(L"Affichage HTML activé"), Microsoft::UI::Xaml::Controls::InfoBarSeverity::Informational);
        }
    }
}
