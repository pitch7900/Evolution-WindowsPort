#include "pch.h"
#include "MainWindow.xaml.h"
#include "WindowIcon.h"
#include "Settings.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

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

/* version de l'application, depuis le fichier VERSION (généré par MSBuild) */
#include "evo-version.h"
#define EVO_WIDEN_(text) L ## text
#define EVO_WIDEN(text) EVO_WIDEN_(text)
#define EVO_VERSION_W EVO_WIDEN(EVO_VERSION_STR)

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace
{
    /* dossier de l'exécutable (sans barre oblique finale) */
    std::wstring ExecutableDirectory()
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring dir{ path };
        return dir.substr(0, dir.find_last_of(L'\\'));
    }

    /* « 24H2 (build 26100.4061) », pour les rapports de bug */
    std::wstring WindowsVersionText()
    {
        constexpr wchar_t key[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
        wchar_t display[64]{}, build[32]{};
        DWORD size = sizeof(display), ubr = 0, ubr_size = sizeof(ubr);

        RegGetValueW(HKEY_LOCAL_MACHINE, key, L"DisplayVersion", RRF_RT_REG_SZ, nullptr, display, &size);
        size = sizeof(build);
        RegGetValueW(HKEY_LOCAL_MACHINE, key, L"CurrentBuild", RRF_RT_REG_SZ, nullptr, build, &size);
        RegGetValueW(HKEY_LOCAL_MACHINE, key, L"UBR", RRF_RT_REG_DWORD, nullptr, &ubr, &ubr_size);

        return std::wstring{ display } + L" (build " + build + L"." + std::to_wstring(ubr) + L")";
    }

    /* Panneaux redimensionnables de la vue Courrier : clé de la largeur
     * mémorisée, largeur par défaut et largeur minimale (pixels). */
    struct PaneLayout
    {
        wchar_t const* key;
        double default_width;
        double min_width;
    };

    constexpr PaneLayout FoldersPane{ L"layout.folders-width", 260, 180 };
    constexpr PaneLayout ListPane{ L"layout.list-width", 420, 280 };
    /* le volet de lecture prend la place restante, au moins cette largeur */
    constexpr double ReaderMinWidth = 320;
    /* borne d'une largeur relue, contre un fichier de préférences aberrant */
    constexpr double RestoredMaxWidth = 1200;

    /* Segoe Fluent Icons glyphs */
    hstring FolderGlyph(hstring const& type)
    {
        if (type == L"inbox") return L"\xE715";
        if (type == L"outbox") return L"\xE8BD";
        if (type == L"sent") return L"\xE724";
        if (type == L"drafts") return L"\xE70F";
        if (type == L"trash") return L"\xE74D";
        if (type == L"junk") return L"\xE7BA";
        if (type == L"archive") return L"\xE7B8";
        return L"\xE8B7";
    }

    hstring FormatDate(double unix_time)
    {
        if (unix_time <= 0)
            return L"";

        auto time = winrt::clock::from_time_t(static_cast<time_t>(unix_time));
        Windows::Globalization::DateTimeFormatting::DateTimeFormatter formatter{ L"shortdate shorttime" };

        return formatter.Format(time);
    }

    /* display name of "Name <address>" */
    hstring SenderName(hstring const& from)
    {
        std::wstring_view view{ from };
        auto pos = view.find(L" <");

        if (pos != std::wstring_view::npos && pos > 0)
        {
            auto name = view.substr(0, pos);
            if (name.size() >= 2 && name.front() == L'"' && name.back() == L'"')
                name = name.substr(1, name.size() - 2);
            return hstring{ name };
        }

        return from;
    }

    /* Origine des documents du lecteur : nom réservé (.invalid), jamais
     * résolu, toutes ses requêtes étant servies par WebResourceRequested.
     * En http (et non https) pour que les images http autorisées ne soient
     * pas bloquées comme contenu mixte. */
    constexpr std::wstring_view ReaderOrigin = L"http://reader.evolution.invalid/";

    /* clés des préférences de confidentialité (voir Settings.h) */
    constexpr wchar_t RemoteContentKey[] = L"privacy.remote-content";
    constexpr wchar_t TrustedSendersKey[] = L"privacy.trusted-senders";
    constexpr wchar_t TrustedDomainsKey[] = L"privacy.trusted-domains";

    bool StartsWith(std::wstring_view text, std::wstring_view prefix)
    {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    std::wstring ToLower(std::wstring_view text)
    {
        std::wstring lower{ text };
        for (auto& c : lower)
            c = static_cast<wchar_t>(std::towlower(c));
        return lower;
    }

    std::wstring_view Trim(std::wstring_view text)
    {
        while (!text.empty() && std::iswspace(text.front()))
            text.remove_prefix(1);
        while (!text.empty() && std::iswspace(text.back()))
            text.remove_suffix(1);
        return text;
    }

    /* Requête vers un serveur distant (images, CSS, polices, cadres, pixels
     * de suivi…), par opposition aux data:, about:, blob: du document. */
    bool IsRemoteUri(std::wstring_view uri)
    {
        auto lower = ToLower(uri.substr(0, 8));
        return StartsWith(lower, L"http:") || StartsWith(lower, L"https:") ||
               StartsWith(lower, L"ws:") || StartsWith(lower, L"wss:") || StartsWith(lower, L"ftp:");
    }

    /* Adresse électronique en minuscules d'un expéditeur « Nom <adresse> »
     * ou « adresse » (la première s'il y en a plusieurs) ; vide si aucune.
     * Paramètres :
     *   from : champ From tel que fourni par le moteur */
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

    /* domaine d'une adresse (« exemple.ch » pour « a@exemple.ch ») */
    std::wstring AddressDomain(std::wstring const& address)
    {
        auto at = address.rfind(L'@');
        return at == std::wstring::npos ? std::wstring{} : address.substr(at + 1);
    }

    /* Liste de confiance (tableau JSON de chaînes) lue dans les préférences.
     * Paramètres :
     *   key : TrustedSendersKey ou TrustedDomainsKey */
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

    /* "never", "trusted" (par défaut) ou "always" */
    std::wstring RemoteContentMode()
    {
        std::wstring mode{ ::EvolutionWin::Settings::Instance().GetString(RemoteContentKey, L"trusted") };
        return mode == L"never" || mode == L"always" ? mode : L"trusted";
    }

    /* Le contenu distant d'un message de cet expéditeur peut-il être chargé
     * sans demande ? « Jamais » l'emporte sur les listes de confiance.
     * Paramètres :
     *   from : champ From du message */
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
    MainWindow::MainWindow()
    {
        /* named elements exist once the XAML is loaded: initialize on the
           first activation */
        m_activated_token = Activated([this](IInspectable const&, WindowActivatedEventArgs const&)
        {
            if (!m_initialized)
            {
                m_initialized = true;
                Initialize();
            }
        });
    }

    MainWindow::~MainWindow()
    {
        m_core.reset();
    }

    void MainWindow::Initialize()
    {
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        AppWindow().Resize({ 1500, 950 });
        ::EvolutionWin::SetWindowIcon(AppWindow());
        RebuildQuickAccess();
        SyncConfidentialControls();
        AppTitleBar().SizeChanged([this](IInspectable const&, SizeChangedEventArgs const&) { UpdateTitleBarPassthrough(); });
        RestorePaneWidths();
        ApplyFolderPaneVisibility(::EvolutionWin::Settings::Instance().GetBoolean(L"layout.folders-visible", true));

        /* menu contextuel des dossiers (clic droit, touche Menu) */
        FolderTree().ContextRequested([this](UIElement const& sender, Input::ContextRequestedEventArgs const& args)
        {
            ShowFolderContextMenu(sender, args);
        });

        InitializeReaderAsync();

        m_core = std::make_shared<::EvolutionWin::CoreBridge>(
            DispatcherQueue(),
            [this](hstring const& event, hstring const& json) { OnCoreEvent(event, json); });

        Closed([this](IInspectable const&, WindowEventArgs const&)
        {
            /* stops the engine before the window goes away */
            m_core.reset();
        });
    }

    /* Rétablit les largeurs des dossiers et de la liste de messages
     * mémorisées lors d'une session précédente (layout.folders-width,
     * layout.list-width). */
    void MainWindow::RestorePaneWidths()
    {
        auto& settings = ::EvolutionWin::Settings::Instance();

        for (auto [column, pane] : { std::pair{ FoldersColumn(), &FoldersPane }, std::pair{ ListColumn(), &ListPane } })
        {
            double width = settings.GetNumber(pane->key, pane->default_width);
            width = std::clamp(width, pane->min_width, RestoredMaxWidth);
            column.Width(GridLengthHelper::FromPixels(width));
        }
    }

    /* Colonne redimensionnée par un séparateur.
     * Paramètres :
     *   splitter : FoldersSplitter ou ListSplitter */
    ColumnDefinition MainWindow::SplitterColumn(IInspectable const& splitter)
    {
        return splitter == FoldersSplitter() ? FoldersColumn() : ListColumn();
    }

    /* Début du glisser : capture le pointeur pour suivre le mouvement même
     * hors du séparateur (au-dessus de la liste ou du volet de lecture). */
    void MainWindow::OnSplitterPointerPressed(IInspectable const& sender, Input::PointerRoutedEventArgs const& args)
    {
        auto splitter = sender.as<UIElement>();

        if (!args.GetCurrentPoint(splitter).Properties().IsLeftButtonPressed())
            return;
        if (!splitter.CapturePointer(args.Pointer()))
            return;

        m_drag_column = SplitterColumn(sender);
        m_drag_origin = args.GetCurrentPoint(MailView()).Position().X;
        m_drag_start_width = m_drag_column.ActualWidth();
    }

    /* Pendant le glisser : nouvelle largeur de la colonne, bornée par son
     * minimum et par la place à laisser aux autres colonnes et au volet de
     * lecture. Les positions sont lues dans MailView, qui ne bouge pas. */
    void MainWindow::OnSplitterPointerMoved(IInspectable const& sender, Input::PointerRoutedEventArgs const& args)
    {
        if (!m_drag_column || SplitterColumn(sender) != m_drag_column)
            return;

        PaneLayout const& pane = m_drag_column == FoldersColumn() ? FoldersPane : ListPane;
        auto columns = MailView().ColumnDefinitions();

        /* largeur occupée par les autres colonnes fixes (autre panneau et
           séparateurs) ; la dernière est le volet de lecture */
        double others = 0;
        for (uint32_t i = 0; i + 1 < columns.Size(); ++i)
        {
            if (columns.GetAt(i) != m_drag_column)
                others += columns.GetAt(i).ActualWidth();
        }

        double max_width = (std::max)(pane.min_width, MailView().ActualWidth() - others - ReaderMinWidth);
        double x = args.GetCurrentPoint(MailView()).Position().X;
        double width = std::clamp(m_drag_start_width + x - m_drag_origin, pane.min_width, max_width);

        m_drag_column.Width(GridLengthHelper::FromPixels(std::round(width)));
    }

    void MainWindow::OnSplitterPointerReleased(IInspectable const& sender, Input::PointerRoutedEventArgs const& args)
    {
        if (!m_drag_column)
            return;

        EndSplitterDrag();
        sender.as<UIElement>().ReleasePointerCapture(args.Pointer());
    }

    /* capture perdue (Échap, perte du focus de la fenêtre…) : la largeur
       atteinte est conservée */
    void MainWindow::OnSplitterPointerCaptureLost(IInspectable const&, Input::PointerRoutedEventArgs const&)
    {
        EndSplitterDrag();
    }

    /* Fin du glisser : mémorise la largeur de la colonne pour la prochaine
     * session. */
    void MainWindow::EndSplitterDrag()
    {
        if (!m_drag_column)
            return;

        auto column = m_drag_column;
        m_drag_column = nullptr;

        PaneLayout const& pane = column == FoldersColumn() ? FoldersPane : ListPane;
        ::EvolutionWin::Settings::Instance().SetNumber(pane.key, column.Width().Value);
    }

    /* Double-clic sur un séparateur : rétablit la largeur par défaut du
     * panneau. Le glisser éventuellement commencé par le second appui est
     * abandonné, sans quoi le prochain mouvement remettrait l'ancienne
     * largeur. */
    void MainWindow::OnSplitterDoubleTapped(IInspectable const& sender, Input::DoubleTappedRoutedEventArgs const& args)
    {
        auto column = SplitterColumn(sender);
        PaneLayout const& pane = column == FoldersColumn() ? FoldersPane : ListPane;

        m_drag_column = nullptr;
        sender.as<UIElement>().ReleasePointerCaptures();

        column.Width(GridLengthHelper::FromPixels(pane.default_width));
        ::EvolutionWin::Settings::Instance().SetNumber(pane.key, pane.default_width);
        args.Handled(true);
    }

    std::wstring MainWindow::NodeKey(hstring const& account, hstring const& folder)
    {
        return std::wstring{ account } + L'\x1F' + std::wstring{ folder };
    }

    void MainWindow::OnCoreEvent(hstring const& event, hstring const& json)
    {
        hstring message;
        JsonObject object;

        if (!JsonObject::TryParse(json, object))
            object = JsonObject{};

        message = object.GetNamedString(L"message", L"");

        if (event == L"ready")
        {
            StatusBar().IsOpen(false);
            LoadAccounts();
        }
        else if (event == L"folder-changed")
        {
            hstring account = object.GetNamedString(L"account", L"");
            hstring folder = object.GetNamedString(L"folder", L"");

            if (auto node = m_nodes.find(NodeKey(account, folder)); node != m_nodes.end())
            {
                if (auto item = node->second.Content().try_as<EvolutionWin::FolderItem>())
                    item.Unread(static_cast<int32_t>(object.GetNamedNumber(L"unread", 0)));
            }
            UpdateMailBadge();

            /* new or removed messages in the folder on screen */
            if (account == m_account && folder == m_folder &&
                (object.GetNamedNumber(L"added", 0) > 0 || object.GetNamedNumber(L"removed", 0) > 0))
                LoadMessages(true);
        }
        else if (event == L"folders-updated")
        {
            hstring account = object.GetNamedString(L"account", L"");

            m_core->ListFolders(account, [this, account](hstring const& folders, hstring const& error)
            {
                if (error.empty())
                {
                    auto parsed = JsonArray::Parse(folders);
                    m_account_folders[std::wstring{ account }] = parsed;
                    UpdateFolderCounts(account, parsed);
                    UpdateMailBadge();
                }
            });
        }
        else if (event == L"sending")
        {
            ShowStatus(L"Envoi de « " + object.GetNamedString(L"subject", L"") + L" »…", InfoBarSeverity::Informational);
        }
        else if (event == L"sent")
        {
            ShowStatus(L"Message envoyé : « " + object.GetNamedString(L"subject", L"") + L" »", InfoBarSeverity::Success);
        }
        else if (event == L"error")
        {
            ShowStatus(L"Le moteur de messagerie n'a pas pu démarrer : " + message, InfoBarSeverity::Error);
        }
        else if (event == L"status")
        {
            ShowStatus(message, InfoBarSeverity::Informational);
        }
        else if (event == L"new-mail")
        {
            ShowNewMailNotification(object);
        }
        else if (event == L"auth-required")
        {
            PromptPasswordAsync(
                object.GetNamedString(L"source_uid", L""),
                object.GetNamedString(L"name", L""),
                object.GetNamedString(L"user", L""),
                object.GetNamedBoolean(L"rejected", false));
        }
        else if (event == L"tls-trust-required")
        {
            PromptTlsCertificateAsync(object);
        }
        else if (event == L"oauth2-required")
        {
            SignInOAuth2Async(object.GetNamedString(L"source_uid", L""), object.GetNamedString(L"name", L""));
        }
    }

    void MainWindow::ShowStatus(hstring const& message, InfoBarSeverity severity)
    {
        StatusBar().Message(message);
        StatusBar().Severity(severity);
        StatusBar().IsOpen(!message.empty());
    }

    void MainWindow::LoadAccounts()
    {
        m_core->ListAccounts([this](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Comptes : " + error, InfoBarSeverity::Error);
                return;
            }

            FolderTree().RootNodes().Clear();
            m_nodes.clear();
            m_account_folders.clear();
            m_accounts = JsonArray::Parse(json);
            RefreshAccountsView();

            for (auto const& value : m_accounts)
            {
                auto account = value.GetObject();
                hstring uid = account.GetNamedString(L"uid");
                bool local = account.GetNamedBoolean(L"local", false);

                TreeViewNode node;
                node.Content(EvolutionWin::FolderItem(account.GetNamedString(L"name", uid),
                    local ? L"\xE7F8" : L"\xE715", 0, uid, L"", true));
                node.IsExpanded(true);
                FolderTree().RootNodes().Append(node);
                m_nodes[NodeKey(uid, L"")] = node;

                m_core->ListFolders(uid, [this, node, uid](hstring const& folders, hstring const& folders_error)
                {
                    if (!folders_error.empty())
                    {
                        ShowStatus(L"Dossiers : " + folders_error, InfoBarSeverity::Warning);
                        return;
                    }

                    auto parsed = JsonArray::Parse(folders);
                    m_account_folders[std::wstring{ uid }] = parsed;
                    AddFolders(node, parsed, uid);
                    UpdateMailBadge();
                });
            }
        });
    }

    /* Rang d'un dossier spécial dans l'arborescence (ordre d'Outlook), les
     * autres dossiers après, par ordre alphabétique. */
    static int SpecialFolderRank(hstring const& type)
    {
        static wchar_t const* const order[] = { L"inbox", L"drafts", L"sent", L"trash", L"archive", L"junk", L"outbox" };
        for (int ii = 0; ii < static_cast<int>(std::size(order)); ii++)
            if (type == order[ii])
                return ii;
        return static_cast<int>(std::size(order));
    }

    /* Nom affiché d'un dossier spécial, quelle que soit la langue du serveur
     * (« INBOX », « Sent Items »…) ; vide pour un dossier ordinaire. */
    static hstring SpecialFolderName(hstring const& type)
    {
        if (type == L"inbox") return L"Boîte de réception";
        if (type == L"drafts") return L"Brouillons";
        if (type == L"sent") return L"Éléments envoyés";
        if (type == L"trash") return L"Éléments supprimés";
        if (type == L"archive") return L"Archive";
        if (type == L"junk") return L"Courrier indésirable";
        if (type == L"outbox") return L"Boîte d'envoi";
        return {};
    }

    void MainWindow::AddFolders(TreeViewNode const& parent, JsonArray const& folders, hstring const& account)
    {
        /* dossiers spéciaux d'abord, puis ordre alphabétique (règles de la langue) */
        std::vector<JsonObject> sorted;
        for (auto const& value : folders)
            sorted.push_back(value.GetObject());

        std::stable_sort(sorted.begin(), sorted.end(), [](JsonObject const& a, JsonObject const& b)
        {
            int ra = SpecialFolderRank(a.GetNamedString(L"type", L"")), rb = SpecialFolderRank(b.GetNamedString(L"type", L""));
            if (ra != rb)
                return ra < rb;
            hstring na = a.GetNamedString(L"name", L""), nb = b.GetNamedString(L"name", L"");
            return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS,
                na.c_str(), static_cast<int>(na.size()), nb.c_str(), static_cast<int>(nb.size()),
                nullptr, nullptr, 0) == CSTR_LESS_THAN;
        });

        for (auto const& folder : sorted)
        {
            hstring full_name = folder.GetNamedString(L"full_name");
            hstring type = folder.GetNamedString(L"type", L"");
            hstring name = SpecialFolderName(type);
            if (name.empty())
                name = folder.GetNamedString(L"name", full_name);

            TreeViewNode node;
            node.Content(EvolutionWin::FolderItem(name, FolderGlyph(type),
                static_cast<int32_t>(folder.GetNamedNumber(L"unread", 0)), account,
                folder.GetNamedBoolean(L"selectable", true) ? full_name : hstring{}, false));
            node.IsExpanded(type == L"inbox");
            parent.Children().Append(node);
            m_nodes[NodeKey(account, full_name)] = node;

            AddFolders(node, folder.GetNamedArray(L"children", JsonArray{}), account);
        }
    }

    void MainWindow::UpdateFolderCounts(hstring const& account, JsonArray const& folders)
    {
        for (auto const& value : folders)
        {
            auto folder = value.GetObject();
            hstring full_name = folder.GetNamedString(L"full_name");

            if (auto node = m_nodes.find(NodeKey(account, full_name)); node != m_nodes.end())
            {
                if (auto item = node->second.Content().try_as<EvolutionWin::FolderItem>())
                    item.Unread(static_cast<int32_t>(folder.GetNamedNumber(L"unread", 0)));
            }

            UpdateFolderCounts(account, folder.GetNamedArray(L"children", JsonArray{}));
        }
    }

    /* Badge de la barre latérale sur « Courrier » : total des non-lus des
     * boîtes de réception de tous les comptes (masqué à zéro). */
    void MainWindow::UpdateMailBadge()
    {
        int32_t unread = 0;
        hstring inbox_glyph = FolderGlyph(L"inbox");

        for (auto const& [key, node] : m_nodes)
        {
            if (auto item = node.Content().try_as<EvolutionWin::FolderItem>(); item && !item.IsAccount() && item.Glyph() == inbox_glyph)
                unread += item.Unread();
        }

        if (unread <= 0)
        {
            MailItem().InfoBadge(nullptr);
            ToolTipService::SetToolTip(MailItem(), box_value(L"Courrier"));
            return;
        }

        InfoBadge badge;
        badge.Value(unread);
        MailItem().InfoBadge(badge);
        ToolTipService::SetToolTip(MailItem(), box_value(L"Courrier (" + to_hstring(unread) + L" non lus)"));
    }

    void MainWindow::OnNavigationSelectionChanged(NavigationView const&, NavigationViewSelectionChangedEventArgs const& args)
    {
        if (args.IsSettingsSelected())
        {
            m_view_tag = L"settings";
            RefreshBanner();
            UpdateRibbon();
            MailView().Visibility(Visibility::Collapsed);
            CalendarViewPane().Visibility(Visibility::Collapsed);
            ContactsViewPane().Visibility(Visibility::Collapsed);
            TasksViewPane().Visibility(Visibility::Collapsed);
            SettingsViewPane().Visibility(Visibility::Visible);
            RefreshAccountsView();
            RefreshPrivacyView();
            RefreshSignaturesView();
            return;
        }

        auto item = args.SelectedItem().try_as<NavigationViewItem>();
        hstring tag = item ? unbox_value_or<hstring>(item.Tag(), L"") : hstring{};
        m_view_tag = tag;
        RefreshBanner();
        UpdateRibbon();

        MailView().Visibility(tag == L"mail" ? Visibility::Visible : Visibility::Collapsed);
        CalendarViewPane().Visibility(tag == L"calendar" ? Visibility::Visible : Visibility::Collapsed);
        ContactsViewPane().Visibility(tag == L"contacts" ? Visibility::Visible : Visibility::Collapsed);
        TasksViewPane().Visibility(tag == L"tasks" ? Visibility::Visible : Visibility::Collapsed);
        SettingsViewPane().Visibility(Visibility::Collapsed);

        if (tag == L"calendar")
            LoadEvents();
        else if (tag == L"contacts")
            LoadContacts(ContactSearchBox().Text());
        else if (tag == L"tasks")
            LoadTasks();
    }

    /* Entrées sans sélection de la barre latérale (pied) : « À propos ». */
    void MainWindow::OnNavigationItemInvoked(NavigationView const&, NavigationViewItemInvokedEventArgs const& args)
    {
        auto item = args.InvokedItemContainer().try_as<NavigationViewItem>();
        if (item && unbox_value_or<hstring>(item.Tag(), L"") == L"about")
            ShowAboutDialogAsync();
    }

    /* Boîte « À propos » : version de l'application (fichier VERSION, comme
     * l'exécutable et l'installeur), licence, liens, versions des composants
     * et bouton de copie de ces informations pour les rapports de bug. */
    fire_and_forget MainWindow::ShowAboutDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;
        m_dialog_open = true;

        std::wstring exe_dir = ExecutableDirectory();

        /* versions : moteur (evo-core), Windows App SDK, WebView2, Windows */
        JsonObject engine;
        if (!JsonObject::TryParse(to_hstring(evo_core_version_info()), engine))
            engine = JsonObject{};
        hstring webview2;
        try
        {
            webview2 = Microsoft::Web::WebView2::Core::CoreWebView2Environment::GetAvailableBrowserVersionString();
        }
        catch (...) {}

        std::vector<std::pair<std::wstring, std::wstring>> components{
            { L"evolution-data-server", std::wstring{ engine.GetNamedString(L"evolution_data_server", L"") } },
            { L"evolution-ews", std::wstring{ engine.GetNamedString(L"evolution_ews", L"") } },
            { L"GLib", std::wstring{ engine.GetNamedString(L"glib", L"") } },
            { L"libsoup", std::wstring{ engine.GetNamedString(L"libsoup", L"") } },
            { L"Windows App SDK", EVO_WIDEN(EVO_WINDOWSAPPSDK_VERSION_STR) },
            { L"WebView2", std::wstring{ webview2 } },
            { L"Windows", WindowsVersionText() },
        };

        std::wstring report = L"Evolution pour Windows " EVO_VERSION_W L"\r\n";
        for (auto const& [name, version] : components)
            report += name + L" : " + (version.empty() ? L"non installé" : version) + L"\r\n";

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"À propos d'Evolution"));
        dialog.CloseButtonText(L"Fermer");
        dialog.DefaultButton(ContentDialogButton::Close);

        StackPanel panel;
        panel.Spacing(12);
        panel.MinWidth(420);

        StackPanel header;
        header.Orientation(Orientation::Horizontal);
        header.Spacing(16);
        Image logo;
        logo.Width(64);
        logo.Height(64);
        logo.Source(Imaging::BitmapImage{ Uri{ hstring{ exe_dir + L"\\Assets\\EvolutionWin.png" } } });
        header.Children().Append(logo);

        StackPanel title;
        title.VerticalAlignment(VerticalAlignment::Center);
        TextBlock name;
        name.Text(L"Evolution pour Windows");
        name.FontSize(20);
        name.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
        title.Children().Append(name);
        TextBlock version;
        version.Text(L"Version " EVO_VERSION_W);
        title.Children().Append(version);
        header.Children().Append(title);
        panel.Children().Append(header);

        TextBlock about;
        about.TextWrapping(TextWrapping::Wrap);
        about.Text(L"Client de messagerie, calendrier et contacts natif Windows 11, "
            L"bâti sur le moteur de GNOME Evolution (evolution-data-server, Camel). "
            L"Logiciel libre sous licence GNU LGPL 2.1 ou ultérieure.");
        panel.Children().Append(about);

        StackPanel links;
        links.Orientation(Orientation::Horizontal);
        HyperlinkButton repository;
        repository.Content(box_value(L"Dépôt GitHub"));
        repository.NavigateUri(Uri{ L"https://github.com/pitch7900/Evolution-WindowsPort" });
        links.Children().Append(repository);
        HyperlinkButton changelog;
        changelog.Content(box_value(L"Nouveautés (CHANGELOG)"));
        changelog.NavigateUri(Uri{ L"https://github.com/pitch7900/Evolution-WindowsPort/blob/main/CHANGELOG.md" });
        links.Children().Append(changelog);
        panel.Children().Append(links);

        Grid table;
        table.ColumnSpacing(16);
        table.ColumnDefinitions().Append(ColumnDefinition{});
        table.ColumnDefinitions().Append(ColumnDefinition{});
        int32_t row = 0;
        for (auto const& [component, component_version] : components)
        {
            table.RowDefinitions().Append(RowDefinition{});
            TextBlock label;
            label.Text(component);
            label.Opacity(0.7);
            Grid::SetRow(label, row);
            table.Children().Append(label);
            TextBlock value;
            value.Text(component_version.empty() ? L"non installé" : component_version);
            value.IsTextSelectionEnabled(true);
            Grid::SetRow(value, row);
            Grid::SetColumn(value, 1);
            table.Children().Append(value);
            ++row;
        }
        panel.Children().Append(table);

        Button copy;
        copy.Content(box_value(L"Copier les informations"));
        copy.Click([report](IInspectable const& sender, RoutedEventArgs const&)
        {
            Windows::ApplicationModel::DataTransfer::DataPackage package;
            package.SetText(report);
            Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
            sender.as<Button>().Content(box_value(L"Informations copiées"));
        });
        panel.Children().Append(copy);

        dialog.Content(panel);
        co_await dialog.ShowAsync();
        m_dialog_open = false;
    }

    /* Menu contextuel d'un dossier de l'arborescence : Marquer tout comme lu,
     * Vider la Corbeille / les Indésirables, Vider le dossier, puis la gestion
     * du dossier (Nouveau dossier, Renommer, Déplacer, Copier, Supprimer).
     * Sur un compte : Nouveau dossier (premier niveau). */
    void MainWindow::ShowFolderContextMenu(UIElement const&, Input::ContextRequestedEventArgs const& args)
    {
        /* élément de l'arborescence sous le pointeur (ou ayant le focus) */
        DependencyObject element = args.OriginalSource().try_as<DependencyObject>();
        while (element && !element.try_as<TreeViewItem>())
            element = VisualTreeHelper::GetParent(element);
        auto container = element ? element.try_as<TreeViewItem>() : nullptr;
        auto node = container ? FolderTree().NodeFromContainer(container) : nullptr;
        auto item = node ? node.Content().try_as<EvolutionWin::FolderItem>() : nullptr;

        if (!item)
            return;
        args.Handled(true);

        bool trash = item.Glyph() == FolderGlyph(L"trash");
        bool junk = item.Glyph() == FolderGlyph(L"junk");

        MenuFlyout menu;
        Microsoft::UI::Xaml::Controls::Primitives::FlyoutShowOptions options;
        Point position;
        if (args.TryGetPosition(container, position))
            options.Position(position);

        /* compte ou dossier non sélectionnable : gestion des dossiers seule */
        if (item.IsAccount() || item.Folder().empty())
        {
            AppendFolderManagementItems(menu, item, NodeFolderName(node));
            if (menu.Items().Size() > 0)
                menu.ShowAt(container, options);
            return;
        }

        MenuFlyoutItem mark_read;
        mark_read.Text(L"Marquer tout comme lu");
        mark_read.Icon(FontIcon{});
        mark_read.Icon().as<FontIcon>().Glyph(L"\xE8C3");
        mark_read.Click([this, item](IInspectable const&, RoutedEventArgs const&)
        {
            RunFolderActionAsync(item, L"mark-all-read");
        });
        menu.Items().Append(mark_read);
        menu.Items().Append(MenuFlyoutSeparator{});

        MenuFlyoutItem empty;
        empty.Text(trash ? L"Vider la Corbeille" : junk ? L"Vider les Indésirables" : L"Vider le dossier");
        empty.Icon(FontIcon{});
        empty.Icon().as<FontIcon>().Glyph(L"\xE74D");
        empty.Click([this, item](IInspectable const&, RoutedEventArgs const&)
        {
            RunFolderActionAsync(item, L"empty");
        });
        menu.Items().Append(empty);

        menu.Items().Append(MenuFlyoutSeparator{});
        AppendFolderManagementItems(menu, item, NodeFolderName(node));

        menu.ShowAt(container, options);
    }

    /* Exécute une action sur tout un dossier, avec confirmation pour
     * « empty » (définitif pour la Corbeille et les Indésirables).
     * Paramètres :
     *   item   : dossier visé
     *   action : "mark-all-read" ou "empty" */
    fire_and_forget MainWindow::RunFolderActionAsync(EvolutionWin::FolderItem item, hstring action)
    {
        auto strong = get_strong();

        if (action == L"empty")
        {
            if (m_dialog_open || !Content() || !Content().XamlRoot())
                co_return;
            bool permanent = item.Glyph() == FolderGlyph(L"trash") || item.Glyph() == FolderGlyph(L"junk");

            ContentDialog confirm;
            confirm.XamlRoot(Content().XamlRoot());
            confirm.Title(box_value(permanent ? L"Supprimer définitivement ?" : L"Vider le dossier ?"));
            TextBlock text;
            text.TextWrapping(TextWrapping::Wrap);
            text.Text(permanent
                ? L"Tous les messages de « " + item.Name() + L" » seront supprimés définitivement. Cette action est irréversible."
                : L"Tous les messages de « " + item.Name() + L" » seront déplacés vers la Corbeille.");
            confirm.Content(text);
            confirm.PrimaryButtonText(L"Supprimer");
            confirm.CloseButtonText(L"Annuler");
            confirm.DefaultButton(ContentDialogButton::Close);

            m_dialog_open = true;
            auto result = co_await confirm.ShowAsync();
            m_dialog_open = false;
            if (result != ContentDialogResult::Primary)
                co_return;
        }

        hstring account = item.Account();
        hstring folder = item.Folder();
        hstring name = item.Name();
        ShowStatus((action == L"empty" ? L"Suppression des messages de « " : L"Marquage comme lus dans « ") + name + L" »…",
            InfoBarSeverity::Informational);

        m_core->FolderAction(account, folder, action, [this, account, folder, name, action](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"« " + name + L" » : " + error, InfoBarSeverity::Error);
                return;
            }

            JsonObject result;
            uint32_t count = JsonObject::TryParse(json, result) ? static_cast<uint32_t>(result.GetNamedNumber(L"count", 0)) : 0;
            ShowStatus(action == L"empty"
                ? hstring{ std::to_wstring(count) + L" message(s) supprimé(s) de « " } + name + L" »."
                : hstring{ std::to_wstring(count) + L" message(s) marqué(s) comme lu(s) dans « " } + name + L" ».",
                InfoBarSeverity::Success);

            if (account == m_account && folder == m_folder)
                LoadMessages(false);
        });
    }

    /* Vrai si « full_name » est le dossier « root » ou l'un de ses
     * sous-dossiers (chemins complets, séparateur « / »). */
    static bool IsFolderOrSubfolder(std::wstring_view full_name, std::wstring_view root)
    {
        return !root.empty() && full_name.substr(0, root.size()) == root &&
            (full_name.size() == root.size() || full_name[root.size()] == L'/');
    }

    /* Chemin complet du dossier d'un nœud de l'arborescence (y compris un
     * dossier non sélectionnable, dont FolderItem::Folder() est vide).
     * Paramètres :
     *   node : nœud de l'arborescence
     * Retourne : le chemin complet, vide pour un compte ou un nœud inconnu. */
    hstring MainWindow::NodeFolderName(TreeViewNode const& node)
    {
        for (auto const& [key, value] : m_nodes)
        {
            if (value == node)
            {
                auto separator = key.find(L'\x1F');
                return separator == std::wstring::npos ? hstring{} : hstring{ key.substr(separator + 1) };
            }
        }
        return {};
    }

    /* Recherche récursivement un dossier dans l'arborescence JSON d'un compte.
     * Paramètres :
     *   folders   : tableau JSON des dossiers (evo_core_list_folders)
     *   full_name : chemin complet recherché
     * Retourne : l'objet JSON du dossier, ou nullptr. */
    JsonObject MainWindow::FindFolderJson(JsonArray const& folders, hstring const& full_name)
    {
        for (auto const& value : folders)
        {
            auto folder = value.GetObject();
            if (folder.GetNamedString(L"full_name", L"") == full_name)
                return folder;
            if (auto found = FindFolderJson(folder.GetNamedArray(L"children", JsonArray{}), full_name))
                return found;
        }
        return nullptr;
    }

    /* Ajoute récursivement les dossiers d'un compte comme destinations d'un
     * déplacement ou d'une copie de dossier.
     * Paramètres :
     *   items           : collection d'éléments du sous-menu à remplir
     *   folders         : tableau JSON des dossiers à parcourir
     *   prefix          : préfixe d'affichage des sous-dossiers
     *   excluded        : dossier exclu avec ses sous-dossiers (vide : aucun)
     *   skipped         : dossier non proposé, ses sous-dossiers restant proposés
     *   selectable_only : n'ajouter que les dossiers sélectionnables
     *   on_pick         : action recevant le chemin complet choisi */
    void MainWindow::AppendFolderTargetItems(
        Collections::IVector<MenuFlyoutItemBase> const& items,
        JsonArray const& folders,
        hstring const& prefix,
        hstring const& excluded,
        hstring const& skipped,
        bool selectable_only,
        std::function<void(hstring const&)> const& on_pick)
    {
        for (auto const& value : folders)
        {
            auto folder = value.GetObject();
            hstring full_name = folder.GetNamedString(L"full_name");
            hstring name = folder.GetNamedString(L"name", full_name);
            hstring label = prefix.empty() ? name : prefix + L" / " + name;

            if (IsFolderOrSubfolder(full_name, excluded))
                continue;

            if (full_name != skipped && (!selectable_only || folder.GetNamedBoolean(L"selectable", true)))
            {
                MenuFlyoutItem item;
                item.Text(label);
                item.Icon(FontIcon{});
                item.Icon().as<FontIcon>().Glyph(FolderGlyph(folder.GetNamedString(L"type", L"")));
                item.Click([on_pick, full_name](IInspectable const&, RoutedEventArgs const&)
                {
                    on_pick(full_name);
                });
                items.Append(item);
            }

            AppendFolderTargetItems(items, folder.GetNamedArray(L"children", JsonArray{}), label, excluded,
                skipped, selectable_only, on_pick);
        }
    }

    /* Ajoute au menu contextuel de l'arborescence les entrées de gestion du
     * dossier : Nouveau dossier…, Renommer…, Déplacer vers, Copier vers,
     * Supprimer le dossier… (Nouveau dossier… seul sur un compte). Les
     * dossiers spéciaux ne peuvent être ni renommés, ni déplacés, ni
     * supprimés ; un dossier qui a des sous-dossiers n'est pas supprimable.
     * Paramètres :
     *   menu      : menu contextuel à compléter
     *   item      : élément du nœud (dossier ou compte)
     *   full_name : chemin complet du dossier (vide pour un compte) */
    void MainWindow::AppendFolderManagementItems(MenuFlyout const& menu, EvolutionWin::FolderItem const& item,
                                                 hstring const& full_name)
    {
        hstring account = item.Account();
        hstring name = item.Name();
        bool is_account = item.IsAccount();

        auto folders_it = m_account_folders.find(std::wstring{ account });
        JsonArray folders = folders_it != m_account_folders.end() ? folders_it->second : nullptr;
        if (!folders)
            return;

        JsonObject folder = is_account ? nullptr : FindFolderJson(folders, full_name);
        if (!is_account && !folder)
            return;

        hstring type = folder ? folder.GetNamedString(L"type", L"") : hstring{};
        bool selectable = !folder || folder.GetNamedBoolean(L"selectable", true);
        bool special = !type.empty() && type != L"archive";
        bool has_children = folder && folder.GetNamedArray(L"children", JsonArray{}).Size() > 0;

        auto add_item = [&menu](hstring const& text, hstring const& glyph, bool enabled, auto handler)
        {
            MenuFlyoutItem entry;
            entry.Text(text);
            entry.Icon(FontIcon{});
            entry.Icon().as<FontIcon>().Glyph(glyph);
            entry.IsEnabled(enabled);
            entry.Click(handler);
            menu.Items().Append(entry);
        };

        add_item(L"Nouveau dossier…", L"\xE8F4", selectable,
            [this, account, full_name, name](IInspectable const&, RoutedEventArgs const&)
            {
                CreateFolderAsync(account, full_name, name);
            });

        if (is_account)
            return;

        add_item(L"Renommer…", L"\xE8AC", !special,
            [this, account, full_name, name](IInspectable const&, RoutedEventArgs const&)
            {
                RenameFolderAsync(account, full_name, name);
            });

        /* destinations : tout le compte sauf le parent actuel (déplacement
         * sans effet, copie déjà présente), et pour un déplacement le
         * dossier lui-même et ses sous-dossiers */
        std::wstring path{ full_name };
        auto slash = path.rfind(L'/');
        hstring parent{ slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash) };

        for (bool copy : { false, true })
        {
            MenuFlyoutSubItem sub;
            sub.Text(copy ? L"Copier vers" : L"Déplacer vers");
            sub.Icon(FontIcon{});
            sub.Icon().as<FontIcon>().Glyph(copy ? L"\xE8C8" : L"\xE8DE");
            sub.IsEnabled(copy ? selectable : !special);

            auto on_pick = [this, account, full_name, name, copy](hstring const& target)
            {
                MoveOrCopyFolder(account, full_name, name, target, copy);
            };

            if (!parent.empty())
            {
                MenuFlyoutItem top;
                top.Text(L"Premier niveau du compte");
                top.Icon(FontIcon{});
                top.Icon().as<FontIcon>().Glyph(L"\xE715");
                top.Click([on_pick](IInspectable const&, RoutedEventArgs const&) { on_pick(L""); });
                sub.Items().Append(top);
            }

            AppendFolderTargetItems(sub.Items(), folders, L"", copy ? hstring{} : full_name, parent, copy, on_pick);

            if (sub.Items().Size() == 0)
            {
                MenuFlyoutItem none;
                none.Text(L"Aucun dossier disponible");
                none.IsEnabled(false);
                sub.Items().Append(none);
            }
            menu.Items().Append(sub);
        }

        add_item(L"Supprimer le dossier…", L"\xE74D", !special && !has_children,
            [this, account, full_name, name, folder](IInspectable const&, RoutedEventArgs const&)
            {
                DeleteFolderAsync(account, full_name, name, static_cast<int32_t>(folder.GetNamedNumber(L"total", 0)));
            });
    }

    /* Demande un nom de dossier (ContentDialog avec une zone de texte).
     * Paramètres :
     *   title   : titre de la boîte de dialogue
     *   label   : libellé de la zone de texte
     *   initial : texte initial (nom actuel pour un renommage)
     * Retourne : le nom saisi sans espaces de début et de fin, vide si
     *            l'utilisateur annule. */
    IAsyncOperation<hstring> MainWindow::PromptFolderNameAsync(hstring title, hstring label, hstring initial)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return hstring{};

        auto trimmed = [](hstring const& text)
        {
            std::wstring value{ text };
            auto first = value.find_first_not_of(L" \t\r\n");
            auto last = value.find_last_not_of(L" \t\r\n");
            return first == std::wstring::npos ? std::wstring{} : value.substr(first, last - first + 1);
        };

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(title));
        TextBox box;
        box.Header(box_value(label));
        box.Text(initial);
        box.SelectAll();
        box.MinWidth(320);
        dialog.Content(box);
        dialog.PrimaryButtonText(L"OK");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.IsPrimaryButtonEnabled(!trimmed(initial).empty());
        box.TextChanged([dialog, trimmed](IInspectable const& sender, TextChangedEventArgs const&)
        {
            auto value = trimmed(sender.as<TextBox>().Text());
            dialog.IsPrimaryButtonEnabled(!value.empty() && value.find(L'/') == std::wstring::npos);
        });

        m_dialog_open = true;
        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result != ContentDialogResult::Primary)
            co_return hstring{};
        co_return hstring{ trimmed(box.Text()) };
    }

    /* Nouveau dossier sous un dossier ou au premier niveau d'un compte.
     * Paramètres :
     *   account     : identifiant du compte
     *   parent      : chemin complet du parent ("" : premier niveau)
     *   parent_name : nom affiché du parent ou du compte */
    fire_and_forget MainWindow::CreateFolderAsync(hstring account, hstring parent, hstring parent_name)
    {
        auto strong = get_strong();
        hstring name = co_await PromptFolderNameAsync(L"Nouveau dossier",
            (parent.empty() ? L"Nom du nouveau dossier au premier niveau de « " : L"Nom du nouveau sous-dossier de « ") +
                parent_name + L" »",
            L"");
        if (name.empty())
            co_return;

        ShowStatus(L"Création du dossier « " + name + L" »…", InfoBarSeverity::Informational);
        m_core->CreateFolder(account, parent, name, [this, account, name](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Nouveau dossier « " + name + L" » : " + error, InfoBarSeverity::Error);
                return;
            }

            JsonObject result;
            hstring full_name = JsonObject::TryParse(json, result) ? result.GetNamedString(L"full_name", L"") : hstring{};
            ShowStatus(L"Dossier « " + name + L" » créé.", InfoBarSeverity::Success);
            ReloadAccountFolders(account, L"", full_name);
        });
    }

    /* Renomme un dossier (il garde son parent).
     * Paramètres :
     *   account   : identifiant du compte
     *   full_name : chemin complet du dossier
     *   name      : nom affiché actuel */
    fire_and_forget MainWindow::RenameFolderAsync(hstring account, hstring full_name, hstring name)
    {
        auto strong = get_strong();
        hstring new_name = co_await PromptFolderNameAsync(L"Renommer le dossier", L"Nouveau nom de « " + name + L" »", name);
        if (new_name.empty() || new_name == name)
            co_return;

        ShowStatus(L"Renommage de « " + name + L" »…", InfoBarSeverity::Informational);
        m_core->RenameFolder(account, full_name, new_name,
            [this, account, full_name, name, new_name](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Renommer « " + name + L" » : " + error, InfoBarSeverity::Error);
                return;
            }

            JsonObject result;
            hstring renamed = JsonObject::TryParse(json, result) ? result.GetNamedString(L"full_name", L"") : hstring{};
            ShowStatus(L"Dossier « " + name + L" » renommé en « " + new_name + L" ».", InfoBarSeverity::Success);
            ReloadAccountFolders(account, full_name, renamed);
        });
    }

    /* Déplace ou copie un dossier sous un autre parent du même compte.
     * Paramètres :
     *   account   : identifiant du compte
     *   full_name : chemin complet du dossier
     *   name      : nom affiché du dossier
     *   parent    : chemin complet de la destination ("" : premier niveau)
     *   copy      : copier (dossier de même nom et copie des messages) */
    void MainWindow::MoveOrCopyFolder(hstring const& account, hstring const& full_name, hstring const& name,
                                      hstring const& parent, bool copy)
    {
        ShowStatus((copy ? L"Copie du dossier « " : L"Déplacement du dossier « ") + name + L" »…",
            InfoBarSeverity::Informational);

        m_core->MoveOrCopyFolder(account, full_name, parent, copy,
            [this, account, full_name, name, copy](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus((copy ? L"Copier « " : L"Déplacer « ") + name + L" » : " + error, InfoBarSeverity::Error);
                /* une copie interrompue laisse le dossier créé */
                if (copy)
                    ReloadAccountFolders(account, L"", L"");
                return;
            }

            JsonObject result;
            if (!JsonObject::TryParse(json, result))
                result = JsonObject{};
            hstring target = result.GetNamedString(L"full_name", L"");

            if (copy)
            {
                auto count = static_cast<uint32_t>(result.GetNamedNumber(L"count", 0));
                ShowStatus(hstring{ L"Dossier « " } + name + L" » copié (" + to_hstring(count) + L" message(s)).",
                    InfoBarSeverity::Success);
                ReloadAccountFolders(account, L"", target);
            }
            else
            {
                ShowStatus(L"Dossier « " + name + L" » déplacé.", InfoBarSeverity::Success);
                ReloadAccountFolders(account, full_name, target);
            }
        });
    }

    /* Supprime un dossier sans sous-dossiers, après confirmation : ses
     * messages sont supprimés définitivement.
     * Paramètres :
     *   account   : identifiant du compte
     *   full_name : chemin complet du dossier
     *   name      : nom affiché du dossier
     *   total     : nombre de messages connu (0 si inconnu) */
    fire_and_forget MainWindow::DeleteFolderAsync(hstring account, hstring full_name, hstring name, int32_t total)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        ContentDialog confirm;
        confirm.XamlRoot(Content().XamlRoot());
        confirm.Title(box_value(L"Supprimer le dossier ?"));
        TextBlock text;
        text.TextWrapping(TextWrapping::Wrap);
        text.Text(total > 0
            ? hstring{ L"Le dossier « " } + name + L" » et ses " + to_hstring(total) +
                L" message(s) seront supprimés définitivement. Cette action est irréversible."
            : L"Le dossier « " + name + L" » et tous les messages qu'il contient seront supprimés définitivement. "
                L"Cette action est irréversible.");
        confirm.Content(text);
        confirm.PrimaryButtonText(L"Supprimer");
        confirm.CloseButtonText(L"Annuler");
        confirm.DefaultButton(ContentDialogButton::Close);

        m_dialog_open = true;
        auto result = co_await confirm.ShowAsync();
        m_dialog_open = false;
        if (result != ContentDialogResult::Primary)
            co_return;

        ShowStatus(L"Suppression du dossier « " + name + L" »…", InfoBarSeverity::Informational);
        m_core->DeleteFolder(account, full_name, [this, account, full_name, name](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Supprimer « " + name + L" » : " + error, InfoBarSeverity::Error);
                return;
            }

            JsonObject result;
            auto count = JsonObject::TryParse(json, result) ? static_cast<uint32_t>(result.GetNamedNumber(L"count", 0)) : 0u;
            ShowStatus(hstring{ L"Dossier « " } + name + L" » supprimé (" + to_hstring(count) + L" message(s)).",
                InfoBarSeverity::Success);
            ReloadAccountFolders(account, full_name, L"");
        });
    }

    /* Recharge les dossiers d'un compte après une opération de gestion, en
     * gardant les nœuds dépliés et la sélection.
     * Paramètres :
     *   account  : identifiant du compte
     *   old_name : dossier renommé, déplacé ou supprimé ("" : aucun)
     *   new_name : nouveau chemin du dossier renommé ou déplacé, ou dossier
     *              créé ("" pour une suppression) */
    void MainWindow::ReloadAccountFolders(hstring const& account, hstring const& old_name, hstring const& new_name)
    {
        /* chemin d'un dossier après l'opération (vide : supprimé) */
        auto map_name = [old_name, new_name](std::wstring const& path) -> std::wstring
        {
            if (!IsFolderOrSubfolder(path, old_name))
                return path;
            return new_name.empty() ? std::wstring{} : std::wstring{ new_name } + path.substr(old_name.size());
        };

        /* dossier affiché renommé, déplacé ou supprimé */
        if (account == m_account && IsFolderOrSubfolder(m_folder, old_name))
        {
            hstring current{ map_name(std::wstring{ m_folder }) };
            m_folder = current;
            if (current.empty())
            {
                ++m_message_request;
                MessageList().Items().Clear();
                m_message_states.clear();
                m_all_messages = nullptr;
                ClearReader();
                FolderTitle().Text(L"");
            }
            else
            {
                LoadMessages(true);
            }
        }

        /* nœuds dépliés, sous leur nouveau chemin */
        std::wstring prefix = std::wstring{ account } + L'\x1F';
        std::vector<std::wstring> expanded;
        for (auto const& [key, node] : m_nodes)
        {
            if (key.size() > prefix.size() && key.compare(0, prefix.size(), prefix) == 0 && node.IsExpanded())
                expanded.push_back(map_name(key.substr(prefix.size())));
        }

        m_core->ListFolders(account, [this, account, new_name, expanded](hstring const& folders, hstring const& error)
        {
            auto account_node = m_nodes.find(NodeKey(account, L""));
            if (!error.empty() || account_node == m_nodes.end())
            {
                if (!error.empty())
                    ShowStatus(L"Dossiers : " + error, InfoBarSeverity::Warning);
                return;
            }

            auto parsed = JsonArray::Parse(folders);
            m_account_folders[std::wstring{ account }] = parsed;

            std::wstring prefix = std::wstring{ account } + L'\x1F';
            for (auto it = m_nodes.begin(); it != m_nodes.end();)
            {
                if (it->first.size() > prefix.size() && it->first.compare(0, prefix.size(), prefix) == 0)
                    it = m_nodes.erase(it);
                else
                    ++it;
            }

            /* vider les enfants replie le nœud du compte : son état est rétabli */
            TreeViewNode root = account_node->second;
            bool root_expanded = root.IsExpanded();
            root.Children().Clear();
            AddFolders(root, parsed, account);
            UpdateMailBadge();
            root.IsExpanded(root_expanded);

            for (auto const& path : expanded)
            {
                if (auto node = m_nodes.find(NodeKey(account, hstring{ path })); node != m_nodes.end())
                    node->second.IsExpanded(true);
            }

            /* parents du dossier créé, renommé ou déplacé : dépliés */
            std::wstring path{ new_name };
            for (auto slash = path.rfind(L'/'); slash != std::wstring::npos; slash = path.rfind(L'/', slash - 1))
            {
                if (auto node = m_nodes.find(NodeKey(account, hstring{ path.substr(0, slash) })); node != m_nodes.end())
                    node->second.IsExpanded(true);
                if (slash == 0)
                    break;
            }

            if (account == m_account && !m_folder.empty())
            {
                if (auto node = m_nodes.find(NodeKey(account, m_folder)); node != m_nodes.end())
                    FolderTree().SelectedNode(node->second);
            }
        });
    }

    void MainWindow::OnFolderInvoked(TreeView const&, TreeViewItemInvokedEventArgs const& args)
    {
        auto node = args.InvokedItem().try_as<TreeViewNode>();
        auto item = node ? node.Content().try_as<EvolutionWin::FolderItem>() : nullptr;
        if (!item)
            return;

        hstring account = item.Account();
        hstring folder = item.Folder();

        /* accounts and non-selectable folders only expand */
        if (folder.empty())
        {
            node.IsExpanded(!node.IsExpanded());
            return;
        }

        m_account = account;
        m_folder = folder;
        ++m_search_request;

        FolderTitle().Text(item.Name());
        LoadFolderSort();
        UpdateRibbon();
        SearchBox().Text(L"");

        LoadMessages(false);
    }

    void MainWindow::LoadMessages(bool keep_selection)
    {
        hstring account = m_account;
        hstring folder = m_folder;
        hstring selected;

        if (account.empty() || folder.empty())
            return;

        if (keep_selection)
        {
            selected = SelectedMessageUid();
        }
        else
        {
            MessageList().Items().Clear();
            m_message_states.clear();
            ListProgress().IsActive(true);
        }

        m_core->ListMessages(account, folder, 500, [this, account, folder, selected](hstring const& json, hstring const& error)
        {
            /* another folder was chosen meanwhile */
            if (account != m_account || folder != m_folder)
                return;

            ListProgress().IsActive(false);

            if (!error.empty())
            {
                ShowStatus(L"Messages : " + error, InfoBarSeverity::Error);
                return;
            }

            m_all_messages = JsonArray::Parse(json);

            if (!SearchBox().Text().empty())
                FilterLocalMessages(SearchBox().Text());
            else
                ShowMessages(m_all_messages);

            /* the reader keeps its message: reselect it silently */
            if (!selected.empty())
            {
                if (auto grid = FindMessageItem(selected))
                {
                    m_reselecting = true;
                    MessageList().SelectedItem(grid);
                    m_reselecting = false;
                }
            }
        });
    }

    /* Construit et affiche les éléments de la liste de messages avec leurs
     * indicateurs (non lu, suivi, pièce jointe) et leur menu contextuel.
     * Paramètres :
     *   messages : tableau JSON des résumés de messages à afficher */
    void MainWindow::ShowMessages(JsonArray const& messages)
    {
        auto items = MessageList().Items();

        /* Rechargement (nouveau message, suppression, filtre) : vider la
         * liste désélectionne et effacerait l'aperçu ; la sélection est
         * rétablie en silence à la fin si le message est encore affiché */
        hstring selected = SelectedMessageUid();
        bool was_reselecting = m_reselecting;
        m_reselecting = true;

        items.Clear();
        m_message_states.clear();

        std::vector<JsonObject> shown;
        for (auto const& value : messages)
        {
            auto message = value.GetObject();
            if (MatchesQuickFilters(message))
                shown.push_back(message);
        }
        SortMessages(shown);

        for (auto const& message : shown)
        {
            hstring uid = message.GetNamedString(L"uid");
            bool seen = message.GetNamedBoolean(L"seen", true);
            bool flagged = message.GetNamedBoolean(L"flagged", false);
            bool attachments = message.GetNamedBoolean(L"attachments", false);

            m_message_states[std::wstring{ uid }] = MessageState{ seen, flagged, attachments };

            Grid item;
            item.Padding(ThicknessHelper::FromLengths(8, 8, 8, 8));
            item.HorizontalAlignment(HorizontalAlignment::Stretch);
            /* Fond transparent explicite pour que le survol (PointerEntered/PointerExited)
             * soit détecté sur toute la surface de la ligne et pas uniquement sur le texte */
            item.Background(SolidColorBrush{ Microsoft::UI::Colors::Transparent() });
            item.Tag(box_value(uid));
            item.ColumnDefinitions().Append(ColumnDefinition{});
            ColumnDefinition date_column;
            date_column.Width(GridLengthHelper::Auto());
            item.ColumnDefinitions().Append(date_column);
            ColumnDefinition action_column;
            action_column.Width(GridLengthHelper::Auto());
            item.ColumnDefinitions().Append(action_column);
            item.RowDefinitions().Append(RowDefinition{});
            item.RowDefinitions().Append(RowDefinition{});

            TextBlock from;
            from.Text(SenderName(message.GetNamedString(L"from", L"")));
            from.TextTrimming(TextTrimming::CharacterEllipsis);
            item.Children().Append(from);

            StackPanel meta;
            meta.Orientation(Orientation::Horizontal);
            meta.Spacing(6);
            meta.VerticalAlignment(VerticalAlignment::Center);
            meta.Margin(ThicknessHelper::FromLengths(8, 0, 0, 0));
            Grid::SetColumn(meta, 1);

            FontIcon flag_icon;
            flag_icon.Glyph(L"\xE7C1");
            flag_icon.FontSize(12);
            flag_icon.Foreground(SolidColorBrush{ Windows::UI::Color{ 255, 247, 99, 12 } });
            flag_icon.Visibility(flagged ? Visibility::Visible : Visibility::Collapsed);
            meta.Children().Append(flag_icon);

            FontIcon attachment_icon;
            attachment_icon.Glyph(L"\xE723");
            attachment_icon.FontSize(12);
            attachment_icon.Visibility(attachments ? Visibility::Visible : Visibility::Collapsed);
            meta.Children().Append(attachment_icon);

            TextBlock date;
            date.Text(FormatDate(message.GetNamedNumber(L"date", 0)));
            date.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            meta.Children().Append(date);

            item.Children().Append(meta);

            TextBlock subject;
            subject.Text(message.GetNamedString(L"subject", L""));
            subject.TextTrimming(TextTrimming::CharacterEllipsis);
            Grid::SetRow(subject, 1);
            Grid::SetColumnSpan(subject, 2);
            item.Children().Append(subject);

            /* Bouton corbeille de suppression rapide au survol (style Outlook) */
            Button delete_button;
            FontIcon trash_icon;
            trash_icon.Glyph(L"\xE74D");
            trash_icon.FontSize(14);
            delete_button.Content(trash_icon);
            delete_button.Width(32);
            delete_button.Height(32);
            delete_button.Padding(ThicknessHelper::FromUniformLength(0));
            delete_button.Margin(ThicknessHelper::FromLengths(6, 0, 0, 0));
            delete_button.VerticalAlignment(VerticalAlignment::Center);
            delete_button.Visibility(Visibility::Collapsed);
            ToolTipService::SetToolTip(delete_button, box_value(L"Supprimer"));
            Grid::SetColumn(delete_button, 2);
            Grid::SetRow(delete_button, 0);
            Grid::SetRowSpan(delete_button, 2);
            delete_button.Click([this, uid](IInspectable const&, RoutedEventArgs const&)
            {
                DeleteMessagesByUids(TargetMessageUids(uid));
            });
            item.Children().Append(delete_button);

            item.PointerEntered([delete_button](IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
            {
                delete_button.Visibility(Visibility::Visible);
            });
            item.PointerExited([delete_button](IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
            {
                delete_button.Visibility(Visibility::Collapsed);
            });

            /* Barre de lecture rapide (style Outlook) dans la marge gauche :
             * couleur d'accent si non lu ; un clic bascule lu / non lu sans
             * ouvrir ni sélectionner le message */
            Grid read_bar;
            read_bar.Tag(box_value(L"read-bar"));
            read_bar.Width(8);
            read_bar.Margin(ThicknessHelper::FromLengths(-8, -8, 0, -8));
            read_bar.HorizontalAlignment(HorizontalAlignment::Left);
            read_bar.Background(SolidColorBrush{ Microsoft::UI::Colors::Transparent() });
            Grid::SetRowSpan(read_bar, 2);
            Border read_mark;
            read_mark.Width(4);
            read_mark.CornerRadius(CornerRadiusHelper::FromUniformRadius(2));
            read_mark.Margin(ThicknessHelper::FromLengths(0, 6, 0, 6));
            read_mark.HorizontalAlignment(HorizontalAlignment::Center);
            read_bar.Children().Append(read_mark);
            read_bar.PointerEntered([this, uid, read_mark](IInspectable const&, Input::PointerRoutedEventArgs const&)
            {
                auto it = m_message_states.find(std::wstring{ uid });
                bool is_seen = it == m_message_states.end() || it->second.seen;
                read_mark.Width(6);
                if (is_seen)
                    read_mark.Background(Application::Current().Resources()
                        .Lookup(box_value(L"ControlStrongStrokeColorDefaultBrush")).as<Brush>());
            });
            read_bar.PointerExited([this, uid, read_mark](IInspectable const&, Input::PointerRoutedEventArgs const&)
            {
                auto it = m_message_states.find(std::wstring{ uid });
                bool is_seen = it == m_message_states.end() || it->second.seen;
                read_mark.Width(4);
                if (is_seen)
                    read_mark.Background(SolidColorBrush{ Microsoft::UI::Colors::Transparent() });
            });
            /* ni sélection ni ouverture : l'appui est consommé ici */
            read_bar.PointerPressed([](IInspectable const&, Input::PointerRoutedEventArgs const& args)
            {
                args.Handled(true);
            });
            read_bar.Tapped([this, uid](IInspectable const&, Input::TappedRoutedEventArgs const& args)
            {
                args.Handled(true);
                auto it = m_message_states.find(std::wstring{ uid });
                bool is_seen = it == m_message_states.end() || it->second.seen;
                SetMessageFlag(uid, L"seen", !is_seen);
            });
            item.Children().Append(read_bar);

            UpdateMessageItemVisual(item, seen, flagged);

            /* Menu contextuel par clic droit sur chaque message (gère un ou plusieurs messages sélectionnés) */
            MenuFlyout context_menu;
            context_menu.Opening([this, uid, context_menu](IInspectable const&, IInspectable const&)
            {
                auto menu_items = context_menu.Items();
                menu_items.Clear();

                auto target_uids = TargetMessageUids(uid);
                bool multi = target_uids.size() > 1;
                std::wstring count_suffix = multi ? L" (" + std::to_wstring(target_uids.size()) + L" messages)" : L"";

                MenuFlyoutItem reply_item;
                reply_item.Text(multi
                    ? hstring{ L"Répondre (1er mail + " + std::to_wstring(target_uids.size() - 1) + L" en PJ)" }
                    : L"Répondre");
                reply_item.Icon(FontIcon{});
                reply_item.Icon().as<FontIcon>().Glyph(L"\xE97A");
                reply_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ReplyToMessages(ReplyKind::Sender, target_uids);
                });
                menu_items.Append(reply_item);

                MenuFlyoutItem reply_all_item;
                reply_all_item.Text(multi
                    ? hstring{ L"Répondre à tous (1er mail + " + std::to_wstring(target_uids.size() - 1) + L" en PJ)" }
                    : L"Répondre à tous");
                reply_all_item.Icon(FontIcon{});
                reply_all_item.Icon().as<FontIcon>().Glyph(L"\xEE35");
                reply_all_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ReplyToMessages(ReplyKind::All, target_uids);
                });
                menu_items.Append(reply_all_item);

                MenuFlyoutItem forward_item;
                forward_item.Text(multi
                    ? hstring{ L"Transférer en pièces jointes" + count_suffix }
                    : L"Transférer");
                forward_item.Icon(FontIcon{});
                forward_item.Icon().as<FontIcon>().Glyph(L"\xE72D");
                forward_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ReplyToMessages(ReplyKind::Forward, target_uids);
                });
                menu_items.Append(forward_item);

                if (!multi)
                {
                    MenuFlyoutItem forward_att_item;
                    forward_att_item.Text(L"Transférer en pièce jointe (.eml)");
                    forward_att_item.Icon(FontIcon{});
                    forward_att_item.Icon().as<FontIcon>().Glyph(L"\xE723");
                    forward_att_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                    {
                        ForwardMessagesAsAttachments(target_uids);
                    });
                    menu_items.Append(forward_att_item);
                }

                menu_items.Append(MenuFlyoutSeparator{});

                auto state_it = m_message_states.find(std::wstring{ target_uids.front() });
                bool is_seen = state_it != m_message_states.end() ? state_it->second.seen : true;
                bool is_flagged = state_it != m_message_states.end() ? state_it->second.flagged : false;

                MenuFlyoutItem read_item;
                read_item.Text(hstring{ (is_seen ? L"Marquer comme non lu" : L"Marquer comme lu") + count_suffix });
                read_item.Icon(FontIcon{});
                read_item.Icon().as<FontIcon>().Glyph(is_seen ? L"\xE715" : L"\xE8C3");
                read_item.Click([this, target_uids, is_seen](IInspectable const&, RoutedEventArgs const&)
                {
                    SetMessagesFlag(target_uids, L"seen", !is_seen);
                });
                menu_items.Append(read_item);

                MenuFlyoutItem flag_item;
                flag_item.Text(hstring{ (is_flagged ? L"Retirer le drapeau" : L"Ajouter un drapeau") + count_suffix });
                flag_item.Icon(FontIcon{});
                flag_item.Icon().as<FontIcon>().Glyph(L"\xE7C1");
                flag_item.Click([this, target_uids, is_flagged](IInspectable const&, RoutedEventArgs const&)
                {
                    SetMessagesFlag(target_uids, L"flagged", !is_flagged);
                });
                menu_items.Append(flag_item);

                MenuFlyoutSubItem move_sub;
                move_sub.Text(hstring{ L"Déplacer vers" + count_suffix });
                move_sub.Icon(FontIcon{});
                move_sub.Icon().as<FontIcon>().Glyph(L"\xE8DE");
                PopulateMoveMenuItems(move_sub.Items(), target_uids);
                menu_items.Append(move_sub);

                menu_items.Append(MenuFlyoutSeparator{});

                MenuFlyoutItem delete_item;
                delete_item.Text(hstring{ L"Supprimer" + count_suffix });
                delete_item.Icon(FontIcon{});
                delete_item.Icon().as<FontIcon>().Glyph(L"\xE74D");
                delete_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    DeleteMessagesByUids(target_uids);
                });
                menu_items.Append(delete_item);
            });
            item.ContextFlyout(context_menu);

            items.Append(item);
        }

        /* message affiché encore présent : resélectionné, l'aperçu reste ;
         * sinon (supprimé, filtré) : aperçu vidé */
        auto still_shown = selected.empty() ? nullptr : FindMessageItem(selected);
        if (still_shown)
            MessageList().SelectedItem(still_shown);
        m_reselecting = was_reselecting;
        if (!selected.empty() && !still_shown && !m_reselecting)
            ClearReader();

        if (messages.Size() == 0)
        {
            if (!SearchBox().Text().empty())
                ShowStatus(L"Aucun message ne correspond à « " + SearchBox().Text() + L" ».", InfoBarSeverity::Informational);
            else
                ShowStatus(L"Ce dossier est vide.", InfoBarSeverity::Informational);
        }
    }

    void MainWindow::OnMessageSelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_reselecting)
            return;

        auto item = MessageList().SelectedItem().try_as<Grid>();
        if (!item)
        {
            ClearReader();
            return;
        }

        hstring uid = unbox_value<hstring>(item.Tag());
        if (uid == m_current_message_uid && m_current_message)
        {
            UpdateActionButtons(uid);
            return;
        }

        uint64_t request = ++m_message_request;

        auto state_it = m_message_states.find(std::wstring{ uid });
        bool flagged = state_it != m_message_states.end() ? state_it->second.flagged : false;
        m_message_states[std::wstring{ uid }].seen = true;
        UpdateMessageItemVisual(item, true, flagged);
        UpdateActionButtons(uid);

        m_core->FetchMessage(m_account, m_folder, uid, [this, request, uid](hstring const& json, hstring const& error)
        {
            if (request != m_message_request)
                return;

            if (!error.empty())
            {
                ShowStatus(L"Message : " + error, InfoBarSeverity::Error);
                return;
            }

            auto message = JsonObject::Parse(json);
            m_current_message = message;
            m_current_message_uid = uid;
            hstring to = message.GetNamedString(L"to", L"");
            hstring cc = message.GetNamedString(L"cc", L"");

            MessageSubject().Text(message.GetNamedString(L"subject", L""));
            MessageFrom().Text(message.GetNamedString(L"from", L""));
            MessageTo().Text(L"À : " + to + (cc.empty() ? hstring{} : L"   Cc : " + cc));
            MessageDate().Text(FormatDate(message.GetNamedNumber(L"date", 0)));
            UpdateSecurityBanner(message.GetNamedObject(L"security", JsonObject{}));
            PopulateAttachments(message.GetNamedArray(L"attachments", JsonArray{}));
            MessageHeader().Visibility(Visibility::Visible);
            ReaderPlaceholder().Visibility(Visibility::Collapsed);
            ReaderContainer().Visibility(Visibility::Visible);
            UpdateActionButtons(uid);

            ShowCurrentMessageInReader(false);
        });
    }

    fire_and_forget MainWindow::InitializeReaderAsync()
    {
        auto strong = get_strong();

        /* writable data folder: next to the executable is not (Program Files) */
        wchar_t local_app_data[MAX_PATH];
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH))
        {
            std::wstring folder = std::wstring{ local_app_data } + L"\\EvolutionWin\\WebView2";
            SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", folder.c_str());
        }

        /* messages are designed for a light background (like Outlook's
           default reading pane), whatever the Windows theme */
        Reader().DefaultBackgroundColor(Microsoft::UI::Colors::White());

        co_await Reader().EnsureCoreWebView2Async();

        Reader().DefaultBackgroundColor(Microsoft::UI::Colors::White());

        auto core = Reader().CoreWebView2();
        /* Force le schéma de couleurs clair dans Chromium pour éviter que le
         * mode sombre de Windows 11 n'active un fond noir sous un texte noir */
        core.Profile().PreferredColorScheme(Microsoft::Web::WebView2::Core::CoreWebView2PreferredColorScheme::Light);

        auto settings = core.Settings();

        /* messages are documents: no scripts, no remote content (privacy,
           as in Evolution), links open in the default browser */
        settings.IsScriptEnabled(false);
        settings.AreDevToolsEnabled(false);
        settings.IsStatusBarEnabled(true);

        core.AddWebResourceRequestedFilter(L"*", Microsoft::Web::WebView2::Core::CoreWebView2WebResourceContext::All);
        core.WebResourceRequested([this](auto const& sender, Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& args)
        {
            OnReaderResourceRequested(sender, args);
        });

        core.NavigationStarting([this](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NavigationStartingEventArgs const& args)
        {
            std::wstring_view uri{ args.Uri() };

            /* le document demandé par ShowInReader (CoreWebView2::Navigate) :
               à tester en premier, WebView2 considère les navigations lancées
               par l'application comme « initiées par l'utilisateur » */
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
                /* lien cliqué : ouvert dans le navigateur ; liens relatifs
                   d'un message : rien à ouvrir */
                args.Cancel(true);
                if (!StartsWith(uri, ReaderOrigin))
                    Windows::System::Launcher::LaunchUriAsync(Uri{ args.Uri() });
                return;
            }

            /* autres navigations non demandées par l'utilisateur : refusées
               (pas de <meta refresh> vers un site distant) */
            if (uri != L"about:blank")
                args.Cancel(true);
        });

        /* le document demandé remplace le précédent : ses requêtes seules
           arrivent désormais, la décision de confiance s'applique */
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

        /* document demandé avant la fin de l'initialisation */
        if (!m_reader_uri.empty())
            core.Navigate(m_reader_uri);
    }

    /* Affiche un document HTML dans le lecteur. Le document reçoit une
     * adresse unique ; tant qu'il n'est pas chargé, aucune requête distante
     * n'est autorisée ni comptée (une requête tardive du message précédent
     * ne peut donc ni charger de contenu ni afficher le bandeau).
     * Paramètres :
     *   html         : document à afficher
     *   allow_remote : autoriser le chargement du contenu distant */
    void MainWindow::ShowInReader(hstring const& html, bool allow_remote)
    {
        ++m_reader_generation;
        m_reader_uri = hstring{ std::wstring{ ReaderOrigin } + L"message-" +
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
            Reader().CoreWebView2().Navigate(m_reader_uri);
        }
        catch (hresult_error const& e)
        {
            ShowStatus(L"Impossible d'afficher le message : " + e.message(), InfoBarSeverity::Warning);
        }
    }

    /* Affiche le message courant (m_current_message) dans le lecteur, avec
     * le contenu distant si l'expéditeur est de confiance ou si demandé.
     * Paramètres :
     *   force_remote : « Afficher les images pour ce message » (cette fois
     *                  seulement, même si l'option globale est « Jamais ») */
    void MainWindow::ShowCurrentMessageInReader(bool force_remote)
    {
        if (!m_current_message)
            return;

        bool allow = force_remote || IsRemoteContentTrusted(m_current_message.GetNamedString(L"from", L""));
        ShowInReader(m_current_message.GetNamedString(L"html", L""), allow);
    }

    /* Toutes les requêtes du lecteur : sert le document courant, bloque
     * (403) le contenu distant non autorisé et compte les blocages pour le
     * bandeau « Contenu externe bloqué ».
     * Paramètres :
     *   sender : moteur WebView2 du lecteur
     *   args   : requête interceptée */
    void MainWindow::OnReaderResourceRequested(
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

    /* Répond à la requête du document du lecteur avec son HTML (UTF-8).
     * Paramètres :
     *   sender : moteur WebView2 du lecteur
     *   args   : requête du document
     *   html   : contenu à servir */
    fire_and_forget MainWindow::ServeReaderDocumentAsync(
        Microsoft::Web::WebView2::Core::CoreWebView2 sender,
        Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs args,
        hstring html)
    {
        auto strong = get_strong();
        auto deferral = args.GetDeferral();

        try
        {
            Windows::Storage::Streams::InMemoryRandomAccessStream stream;
            Windows::Storage::Streams::DataWriter writer{ stream };
            writer.UnicodeEncoding(Windows::Storage::Streams::UnicodeEncoding::Utf8);
            writer.WriteString(html);
            co_await writer.StoreAsync();
            writer.DetachStream();
            stream.Seek(0);

            args.Response(sender.Environment().CreateWebResourceResponse(stream, 200, L"OK",
                L"Content-Type: text/html; charset=utf-8\r\n"
                L"Cache-Control: no-store\r\n"
                L"Referrer-Policy: no-referrer"));
        }
        catch (hresult_error const& e)
        {
            ShowStatus(L"Impossible d'afficher le message : " + e.message(), InfoBarSeverity::Warning);
        }

        deferral.Complete();
    }

    /* Ouvre ou met à jour le bandeau « Contenu externe bloqué » pour le
     * message affiché (boutons de confiance selon l'expéditeur et l'option
     * globale). */
    void MainWindow::UpdateRemoteContentBanner()
    {
        if (!m_current_message || m_reader_blocked_count == 0)
        {
            RemoteContentBar().IsOpen(false);
            return;
        }

        auto address = SenderAddress(m_current_message.GetNamedString(L"from", L""));
        bool never = RemoteContentMode() == L"never";

        std::wstring msg = m_reader_blocked_count == 1
            ? L"1 élément distant (image, style, pixel de suivi…) n'a pas été chargé pour protéger votre vie privée."
            : std::to_wstring(m_reader_blocked_count) +
              L" éléments distants (images, styles, pixels de suivi…) n'ont pas été chargés pour protéger votre vie privée.";
        if (never)
            msg += L" Le chargement est désactivé pour tous les messages (Paramètres › Confidentialité).";
        RemoteContentBar().Message(hstring{ msg });

        bool can_trust = !never && !address.empty();
        TrustSenderButton().Visibility(can_trust ? Visibility::Visible : Visibility::Collapsed);
        TrustDomainButton().Visibility(can_trust ? Visibility::Visible : Visibility::Collapsed);
        if (can_trust)
        {
            TrustSenderButton().Content(box_value(hstring{ L"Toujours faire confiance à " + address }));
            TrustDomainButton().Content(box_value(hstring{ L"Toujours faire confiance au domaine @" + AddressDomain(address) }));
        }

        /* ouvert au premier blocage seulement : une image chargée plus tard
           ne rouvre pas un bandeau fermé par l'utilisateur */
        if (m_reader_blocked_count == 1)
            RemoteContentBar().IsOpen(true);
    }

    /* « Afficher les images pour ce message » : recharge le message courant
     * avec le contenu distant, cette fois seulement. */
    void MainWindow::OnShowRemoteContentClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowCurrentMessageInReader(true);
    }

    void MainWindow::OnTrustSenderClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_current_message)
            return;
        auto address = SenderAddress(m_current_message.GetNamedString(L"from", L""));
        TrustCurrentSender(TrustedSendersKey, address, hstring{ address });
    }

    void MainWindow::OnTrustDomainClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_current_message)
            return;
        auto domain = AddressDomain(SenderAddress(m_current_message.GetNamedString(L"from", L"")));
        TrustCurrentSender(TrustedDomainsKey, domain, hstring{ L"@" + domain });
    }

    /* Ajoute une entrée à une liste de confiance puis recharge le message
     * courant avec son contenu distant.
     * Paramètres :
     *   key   : TrustedSendersKey ou TrustedDomainsKey
     *   entry : adresse ou domaine en minuscules
     *   label : texte affiché dans la barre d'état */
    void MainWindow::TrustCurrentSender(hstring const& key, std::wstring const& entry, hstring const& label)
    {
        if (entry.empty())
            return;

        auto list = ReadTrustList(key.c_str());
        if (std::find(list.begin(), list.end(), entry) == list.end())
        {
            list.push_back(entry);
            WriteTrustList(key.c_str(), list);
        }

        ShowStatus(L"Le contenu externe des messages de « " + label + L" » sera chargé.", InfoBarSeverity::Success);
        ShowCurrentMessageInReader(false);
    }

    void MainWindow::OnRefreshClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowStatus(L"Relève du courrier…", InfoBarSeverity::Informational);
        m_core->Refresh();
    }

    void MainWindow::OnRefreshSplitClick(SplitButton const&, SplitButtonClickEventArgs const&)
    {
        OnRefreshClick(nullptr, nullptr);
    }

    /* F9 : tous les dossiers de tous les comptes (progression dans la barre d'état) */
    void MainWindow::OnRefreshAllClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowStatus(L"Relève de tous les dossiers…", InfoBarSeverity::Informational);
        m_core->RefreshAll();
    }

    /* Maj+F9 : le dossier affiché seulement (la liste interroge le serveur) */
    void MainWindow::OnRefreshFolderClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_folder.empty())
            return;
        ShowStatus(L"Mise à jour de « " + FolderTitle().Text() + L" »…", InfoBarSeverity::Informational);
        LoadMessages(true);
    }

    void MainWindow::OnRefreshAccelerator(Input::KeyboardAccelerator const& sender,
        Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);

        if (sender.Key() == Windows::System::VirtualKey::F5)
            OnRefreshClick(nullptr, nullptr);
        else if ((sender.Modifiers() & Windows::System::VirtualKeyModifiers::Shift) == Windows::System::VirtualKeyModifiers::Shift)
            OnRefreshFolderClick(nullptr, nullptr);
        else
            OnRefreshAllClick(nullptr, nullptr);
    }

    void MainWindow::OnNewMessageClick(IInspectable const&, RoutedEventArgs const&)
    {
        ::EvolutionWin::ComposeData data;

        /* nouveau message : compte par défaut, sinon celui du dossier affiché
         * (les réponses et transferts gardent le compte du message) */
        data.account = m_account;
        if (m_accounts)
        {
            for (auto const& value : m_accounts)
            {
                auto account = value.GetObject();
                if (account.GetNamedBoolean(L"default", false))
                {
                    data.account = account.GetNamedString(L"uid", m_account);
                    break;
                }
            }
        }
        OpenCompose(data);
    }

    void MainWindow::OpenCompose(::EvolutionWin::ComposeData const& data)
    {
        if (!m_accounts)
        {
            ShowStatus(L"Les comptes ne sont pas encore chargés.", InfoBarSeverity::Warning);
            return;
        }

        auto window = make<ComposeWindow>();
        get_self<ComposeWindow>(window)->Setup(m_core, m_accounts, data);
        window.Activate();
    }

    void MainWindow::OnReplyClick(IInspectable const&, RoutedEventArgs const&)
    {
        Reply(ReplyKind::Sender);
    }

    void MainWindow::OnReplyAllClick(IInspectable const&, RoutedEventArgs const&)
    {
        Reply(ReplyKind::All);
    }

    void MainWindow::OnForwardClick(IInspectable const&, RoutedEventArgs const&)
    {
        Reply(ReplyKind::Forward);
    }

    namespace
    {
        bool StartsWithNoCase(std::wstring_view text, std::wstring_view prefix)
        {
            return text.size() >= prefix.size() &&
                CompareStringOrdinal(text.data(), static_cast<int>(prefix.size()),
                    prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
        }

        /* Échappe les caractères spéciaux HTML d'une chaîne brute. */
        std::wstring EscapeHtml(std::wstring_view text)
        {
            std::wstring out;
            out.reserve(text.size() + 32);

            for (wchar_t c : text)
            {
                switch (c)
                {
                case L'&': out += L"&amp;"; break;
                case L'<': out += L"&lt;"; break;
                case L'>': out += L"&gt;"; break;
                case L'"': out += L"&quot;"; break;
                case L'\r': break;
                case L'\n': out += L"<br>"; break;
                default: out += c; break;
                }
            }

            return out;
        }

        /* "> " before each line */
        std::wstring Quote(std::wstring_view text)
        {
            std::wstring quoted = L"> ";

            for (auto c : text)
            {
                if (c == L'\r')
                    continue;
                quoted += c;
                if (c == L'\n')
                    quoted += L"> ";
            }

            return quoted;
        }

        /* comma-separated addresses without those containing own_address */
        std::wstring WithoutAddress(std::wstring_view addresses, std::wstring_view own_address)
        {
            std::wstring result;
            size_t start = 0;

            while (start <= addresses.size())
            {
                size_t end = addresses.find(L',', start);
                if (end == std::wstring_view::npos)
                    end = addresses.size();

                std::wstring part{ addresses.substr(start, end - start) };
                part.erase(0, part.find_first_not_of(L' '));

                std::wstring lower_part = part, lower_own{ own_address };
                CharLowerBuffW(lower_part.data(), static_cast<DWORD>(lower_part.size()));
                CharLowerBuffW(lower_own.data(), static_cast<DWORD>(lower_own.size()));

                if (!part.empty() && (lower_own.empty() || lower_part.find(lower_own) == std::wstring::npos))
                {
                    if (!result.empty())
                        result += L", ";
                    result += part;
                }

                start = end + 1;
            }

            return result;
        }
    }

    /* Construit la structure ComposeData (destinataires, objet, corps cité) à
     * partir d'un objet JSON de message pour une réponse ou un transfert simple.
     * Paramètres :
     *   kind    : type d'action (Sender, All ou Forward)
     *   message : objet JSON complet du message source
     * Retourne : la structure ComposeData prête pour OpenCompose. */
    ::EvolutionWin::ComposeData MainWindow::BuildReplyComposeData(ReplyKind kind, JsonObject const& message)
    {
        hstring subject = message.GetNamedString(L"subject", L"");
        hstring from = message.GetNamedString(L"from", L"");
        hstring to = message.GetNamedString(L"to", L"");
        hstring cc = message.GetNamedString(L"cc", L"");
        hstring date = FormatDate(message.GetNamedNumber(L"date", 0));
        hstring text = message.GetNamedString(L"text", L"");
        hstring message_id = message.GetNamedString(L"message_id", L"");

        ::EvolutionWin::ComposeData data;
        data.account = m_account;
        data.reply = true;

        if (kind == ReplyKind::Forward)
        {
            data.subject = StartsWithNoCase(subject, L"TR:") || StartsWithNoCase(subject, L"Fwd:") ? subject : L"TR: " + subject;
            data.body = L"\r\r-------- Message transféré --------\rDe : " + from + L"\rDate : " + date +
                L"\rObjet : " + subject + L"\rÀ : " + to + (cc.empty() ? hstring{} : L"\rCc : " + cc) +
                L"\r\r" + text;
            data.html_body = hstring{
                L"<div><br></div><hr style=\"border:none;border-top:1px solid #ccc;\">"
                L"<div style=\"color:#444;font-size:10pt;\">"
                L"<b>-------- Message transféré --------</b><br>"
                L"<b>De :</b> " + EscapeHtml(from) + L"<br>"
                L"<b>Date :</b> " + EscapeHtml(date) + L"<br>"
                L"<b>Objet :</b> " + EscapeHtml(subject) + L"<br>"
                L"<b>À :</b> " + EscapeHtml(to) +
                (cc.empty() ? std::wstring{} : L"<br><b>Cc :</b> " + EscapeHtml(cc)) +
                L"</div><br><div>" + EscapeHtml(text) + L"</div>" };
        }
        else
        {
            hstring reply_to = message.GetNamedString(L"reply_to", L"");

            data.to = reply_to.empty() ? from : reply_to;
            data.subject = StartsWithNoCase(subject, L"Re:") ? subject : L"Re: " + subject;
            data.in_reply_to = message_id;

            hstring references = message.GetNamedString(L"references", L"");
            data.references = references.empty() ? message_id : references + L" " + message_id;

            if (kind == ReplyKind::All)
            {
                hstring own_address;
                for (auto const& value : m_accounts)
                {
                    auto account = value.GetObject();
                    if (account.GetNamedString(L"uid") == m_account)
                        own_address = account.GetNamedString(L"address", L"");
                }

                std::wstring others = std::wstring{ to } + (cc.empty() ? L"" : L", " + std::wstring{ cc });
                data.cc = hstring{ WithoutAddress(others, own_address) };
            }

            /* the text box uses \r line ends */
            std::wstring quoted = Quote(text);
            for (auto& c : quoted)
                if (c == L'\n')
                    c = L'\r';

            data.body = L"\r\rLe " + date + L", " + from + L" a écrit :\r" + hstring{ quoted };
            data.html_body = hstring{
                L"<div><br></div><div>Le " + EscapeHtml(date) + L", " + EscapeHtml(from) + L" a écrit :</div>"
                L"<blockquote>" + EscapeHtml(text) + L"</blockquote>" };
        }

        return data;
    }

    /* Transfère un ou plusieurs messages en les exportant sous forme de pièces
     * jointes .eml attachées à un nouveau courriel.
     * Paramètres :
     *   uids : identifiants des messages à joindre en .eml */
    void MainWindow::ForwardMessagesAsAttachments(std::vector<hstring> const& uids)
    {
        if (uids.empty() || m_account.empty() || m_folder.empty())
            return;

        hstring first_subject;
        if (m_current_message && m_current_message_uid == uids.front())
        {
            first_subject = m_current_message.GetNamedString(L"subject", L"");
        }
        else if (m_all_messages)
        {
            for (auto const& entry : m_all_messages)
            {
                auto obj = entry.GetObject();
                if (obj.GetNamedString(L"uid", L"") == uids.front())
                {
                    first_subject = obj.GetNamedString(L"subject", L"");
                    break;
                }
            }
        }

        JsonObject req;
        req.SetNamedValue(L"account", JsonValue::CreateStringValue(m_account));
        req.SetNamedValue(L"folder", JsonValue::CreateStringValue(m_folder));
        JsonArray uid_array;
        for (auto const& u : uids)
            uid_array.Append(JsonValue::CreateStringValue(u));
        req.SetNamedValue(L"uids", uid_array);

        hstring account = m_account;
        ShowStatus(L"Préparation des courriels en pièces jointes…", InfoBarSeverity::Informational);

        m_core->ExportMessagesEml(req.Stringify(),
            [this, account, first_subject](hstring const& json, hstring const& error)
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
                data.subject = L"TR: Messages transférés";
            }

            for (auto const& val : paths)
                data.attachments.push_back(val.GetString());

            StatusBar().IsOpen(false);
            OpenCompose(data);
        });
    }

    /* Répond ou transfère un ou plusieurs messages :
     * - Pour un transfert (Forward), joint tous les courriels sélectionnés en
     *   pièces jointes (.eml) à un nouveau message ;
     * - Pour une réponse (Sender / All), répond au premier courriel sélectionné
     *   et joint tous les autres courriels sélectionnés en pièces jointes (.eml).
     * Paramètres :
     *   kind : type d'action (Sender, All ou Forward)
     *   uids : liste ordonnée des identifiants des messages sélectionnés */
    void MainWindow::ReplyToMessages(ReplyKind kind, std::vector<hstring> const& uids)
    {
        if (uids.empty() || m_account.empty() || m_folder.empty())
            return;

        /* Pour le transfert, on joint le ou les mails sélectionnés en pièces jointes (.eml) */
        if (kind == ReplyKind::Forward)
        {
            ForwardMessagesAsAttachments(uids);
            return;
        }

        hstring first_uid = uids.front();
        std::vector<hstring> extra_uids(uids.begin() + 1, uids.end());

        auto proceed_with_first = [this, kind, extra_uids](JsonObject const& first_msg)
        {
            auto data = BuildReplyComposeData(kind, first_msg);

            if (extra_uids.empty())
            {
                OpenCompose(data);
                return;
            }

            JsonObject req;
            req.SetNamedValue(L"account", JsonValue::CreateStringValue(m_account));
            req.SetNamedValue(L"folder", JsonValue::CreateStringValue(m_folder));
            JsonArray uid_array;
            for (auto const& u : extra_uids)
                uid_array.Append(JsonValue::CreateStringValue(u));
            req.SetNamedValue(L"uids", uid_array);

            ShowStatus(L"Ajout des autres courriels sélectionnés en pièces jointes…", InfoBarSeverity::Informational);

            m_core->ExportMessagesEml(req.Stringify(),
                [this, data](hstring const& json, hstring const& error) mutable
            {
                if (!error.empty())
                {
                    ShowStatus(L"Pièces jointes : " + error, InfoBarSeverity::Error);
                    return;
                }

                JsonArray paths;
                if (JsonArray::TryParse(json, paths))
                {
                    for (auto const& val : paths)
                        data.attachments.push_back(val.GetString());
                }

                StatusBar().IsOpen(false);
                OpenCompose(data);
            });
        };

        if (m_current_message && m_current_message_uid == first_uid)
        {
            proceed_with_first(m_current_message);
        }
        else
        {
            m_core->FetchMessage(m_account, m_folder, first_uid,
                [this, proceed_with_first](hstring const& json, hstring const& error)
            {
                if (!error.empty())
                {
                    ShowStatus(L"Message : " + error, InfoBarSeverity::Error);
                    return;
                }

                JsonObject msg;
                if (JsonObject::TryParse(json, msg))
                    proceed_with_first(msg);
            });
        }
    }

    void MainWindow::Reply(ReplyKind kind)
    {
        auto uids = SelectedMessageUids();
        if (uids.empty() && !m_current_message_uid.empty())
            uids.push_back(m_current_message_uid);

        ReplyToMessages(kind, uids);
    }

    /* Retourne l'UID du premier message sélectionné dans la liste, ou ""
     * s'il n'y a aucune sélection. */
    hstring MainWindow::SelectedMessageUid()
    {
        if (auto item = MessageList().SelectedItem().try_as<Grid>())
            return unbox_value_or<hstring>(item.Tag(), L"");
        return L"";
    }

    /* Retourne la liste ordonnée des UID de tous les messages actuellement
     * sélectionnés dans MessageList. */
    std::vector<hstring> MainWindow::SelectedMessageUids()
    {
        std::vector<hstring> result;
        for (auto const& obj : MessageList().SelectedItems())
        {
            if (auto grid = obj.try_as<Grid>())
            {
                hstring u = unbox_value_or<hstring>(grid.Tag(), L"");
                if (!u.empty())
                    result.push_back(u);
            }
        }
        return result;
    }

    /* Détermine la liste des UID ciblés par une action (barre d'outils ou menu
     * contextuel) : si context_uid fait partie de la sélection courante (ou est
     * vide), retourne toute la sélection ; sinon retourne uniquement context_uid. */
    std::vector<hstring> MainWindow::TargetMessageUids(hstring const& context_uid)
    {
        auto selected = SelectedMessageUids();
        if (context_uid.empty())
            return selected;

        for (auto const& u : selected)
        {
            if (u == context_uid)
                return selected;
        }
        return { context_uid };
    }

    /* Recherche dans la liste affichée l'élément Grid correspondant à un UID.
     * Paramètres :
     *   uid : identifiant du message recherché
     * Retourne : l'élément Grid trouvé, ou nullptr. */
    Grid MainWindow::FindMessageItem(hstring const& uid)
    {
        for (auto const& entry : MessageList().Items())
        {
            auto grid = entry.try_as<Grid>();
            if (grid && unbox_value_or<hstring>(grid.Tag(), L"") == uid)
                return grid;
        }
        return nullptr;
    }

    /* Met à jour l'apparence visuelle d'un élément de la liste de messages
     * (graisse, couleur d'accentuation et icône de suivi).
     * Paramètres :
     *   item    : élément Grid du message dans MessageList
     *   seen    : true si le message est lu, false s'il est non lu
     *   flagged : true si le message porte un indicateur de suivi */
    void MainWindow::UpdateMessageItemVisual(Grid const& item, bool seen, bool flagged)
    {
        if (!item)
            return;

        auto weight = seen ? Microsoft::UI::Text::FontWeights::Normal() : Microsoft::UI::Text::FontWeights::SemiBold();

        for (auto const& child : item.Children())
        {
            /* barre de lecture rapide : accent si non lu, info-bulle de l'action */
            if (auto bar = child.try_as<Grid>(); bar && unbox_value_or<hstring>(bar.Tag(), L"") == L"read-bar")
            {
                if (auto mark = bar.Children().GetAt(0).try_as<Border>())
                    mark.Background(seen
                        ? Brush{ SolidColorBrush{ Microsoft::UI::Colors::Transparent() } }
                        : Application::Current().Resources().Lookup(box_value(L"AccentFillColorDefaultBrush")).as<Brush>());
                ToolTipService::SetToolTip(bar, box_value(seen ? L"Marquer comme non lu" : L"Marquer comme lu"));
            }
            else if (auto text = child.try_as<TextBlock>(); text && Grid::GetColumn(text) == 0)
            {
                text.FontWeight(weight);
                if (!seen && Grid::GetRow(text) == 1)
                {
                    bool dark = (item.ActualTheme() == ElementTheme::Dark);
                    text.Foreground(SolidColorBrush{ dark
                        ? Windows::UI::Color{ 255, 96, 205, 255 }
                        : Windows::UI::Color{ 255, 0, 95, 184 } });
                }
                else
                {
                    text.ClearValue(TextBlock::ForegroundProperty());
                }
            }
            else if (auto meta = child.try_as<StackPanel>(); meta && Grid::GetColumn(meta) == 1 && meta.Children().Size() > 0)
            {
                if (auto icon = meta.Children().GetAt(0).try_as<FontIcon>())
                    icon.Visibility(flagged ? Visibility::Visible : Visibility::Collapsed);
            }
        }
    }

    /* Met à jour le libellé et l'icône des boutons d'action du volet de lecture
     * selon l'état du message sélectionné.
     * Paramètres :
     *   uid : identifiant du message affiché */
    void MainWindow::UpdateActionButtons(hstring const& uid)
    {
        auto it = m_message_states.find(std::wstring{ uid });
        bool seen = it != m_message_states.end() ? it->second.seen : true;
        bool flagged = it != m_message_states.end() ? it->second.flagged : false;

        ToggleReadButton().Label(seen ? L"Non lu" : L"Lu");
        ToggleReadIcon().Glyph(seen ? L"\xE715" : L"\xE8C3");

        ToggleFlagButton().Label(flagged ? L"Retirer suivi" : L"Suivi");
    }

    /* Modifie un indicateur ("seen" ou "flagged") sur un message côté interface
     * et côté moteur evo-core.
     * Paramètres :
     *   uid   : identifiant du message
     *   flag  : nom de l'indicateur ("seen" ou "flagged")
     *   value : nouvelle valeur booléenne de l'indicateur */
    void MainWindow::SetMessageFlag(hstring const& uid, hstring const& flag, bool value)
    {
        if (uid.empty() || m_account.empty() || m_folder.empty())
            return;

        auto& state = m_message_states[std::wstring{ uid }];
        if (flag == L"seen")
            state.seen = value;
        else if (flag == L"flagged")
            state.flagged = value;

        if (auto item = FindMessageItem(uid))
            UpdateMessageItemVisual(item, state.seen, state.flagged);

        if (uid == SelectedMessageUid())
            UpdateActionButtons(uid);

        /* Met aussi à jour la copie en mémoire pour que la recherche locale reste cohérente */
        if (m_all_messages)
        {
            for (auto const& entry : m_all_messages)
            {
                auto obj = entry.GetObject();
                if (obj.GetNamedString(L"uid", L"") == uid)
                {
                    obj.SetNamedValue(flag, JsonValue::CreateBooleanValue(value));
                    break;
                }
            }
        }

        hstring account = m_account;
        hstring folder = m_folder;
        m_core->SetMessageFlags(account, folder, uid, flag, value,
            [this, account, folder, uid](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Indicateur : " + error, InfoBarSeverity::Error);
                return;
            }

            if (account != m_account || folder != m_folder)
                return;

            JsonObject result;
            if (JsonObject::TryParse(json, result))
            {
                auto& current = m_message_states[std::wstring{ uid }];
                current.seen = result.GetNamedBoolean(L"seen", current.seen);
                current.flagged = result.GetNamedBoolean(L"flagged", current.flagged);
                if (auto item = FindMessageItem(uid))
                    UpdateMessageItemVisual(item, current.seen, current.flagged);
                if (uid == SelectedMessageUid())
                    UpdateActionButtons(uid);
            }
        });
    }

    /* Modifie un indicateur ("seen" ou "flagged") sur une liste de messages.
     * Paramètres :
     *   uids  : identifiants des messages
     *   flag  : nom de l'indicateur ("seen" ou "flagged")
     *   value : nouvelle valeur booléenne */
    void MainWindow::SetMessagesFlag(std::vector<hstring> const& uids, hstring const& flag, bool value)
    {
        for (auto const& u : uids)
            SetMessageFlag(u, flag, value);
    }

    /* Vide le volet de lecture lorsqu'aucun message n'est sélectionné. */
    void MainWindow::ClearReader()
    {
        ++m_message_request;
        m_current_message = nullptr;
        m_current_message_uid = L"";
        SecurityBar().IsOpen(false);
        AttachmentBar().Children().Clear();
        AttachmentBar().Visibility(Visibility::Collapsed);
        MessageHeader().Visibility(Visibility::Collapsed);
        ReaderContainer().Visibility(Visibility::Collapsed);
        ReaderPlaceholder().Visibility(Visibility::Visible);
        ShowInReader(L"<!DOCTYPE html><html><body style=\"background:#fff;color:#1a1a1a;\"></body></html>");
    }

    /* Retire un message de la liste affichée et de la liste en mémoire, puis
     * sélectionne le message voisin s'il en reste.
     * Paramètres :
     *   uid : identifiant du message à retirer */
    void MainWindow::RemoveMessageFromList(hstring const& uid)
    {
        auto items = MessageList().Items();
        uint32_t removed_index = 0;
        bool found = false;
        bool was_selected = (SelectedMessageUid() == uid);

        for (uint32_t i = 0; i < items.Size(); ++i)
        {
            auto grid = items.GetAt(i).try_as<Grid>();
            if (grid && unbox_value_or<hstring>(grid.Tag(), L"") == uid)
            {
                removed_index = i;
                found = true;
                items.RemoveAt(i);
                break;
            }
        }

        m_message_states.erase(std::wstring{ uid });

        if (m_all_messages)
        {
            for (uint32_t i = 0; i < m_all_messages.Size(); ++i)
            {
                if (m_all_messages.GetAt(i).GetObject().GetNamedString(L"uid", L"") == uid)
                {
                    m_all_messages.RemoveAt(i);
                    break;
                }
            }
        }

        if (was_selected && MessageList().SelectedItems().Size() == 0)
        {
            if (found && items.Size() > 0)
            {
                uint32_t next_index = removed_index < items.Size() ? removed_index : items.Size() - 1;
                MessageList().SelectedIndex(static_cast<int32_t>(next_index));
            }
            else
            {
                ClearReader();
            }
        }
    }

    /* Supprime un message identifié par son UID (déplacement vers la corbeille
     * ou suppression définitive).
     * Paramètres :
     *   uid : identifiant du message à supprimer */
    void MainWindow::DeleteMessageByUid(hstring const& uid)
    {
        if (uid.empty() || m_account.empty() || m_folder.empty())
            return;

        hstring account = m_account;
        hstring folder = m_folder;

        RemoveMessageFromList(uid);

        m_core->DeleteMessage(account, folder, uid, [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Suppression : " + error, InfoBarSeverity::Error);
                LoadMessages(false);
            }
        });
    }

    /* Supprime un ensemble de messages identifiés par leurs UID.
     * Paramètres :
     *   uids : liste des identifiants des messages à supprimer */
    void MainWindow::DeleteMessagesByUids(std::vector<hstring> const& uids)
    {
        for (auto const& u : uids)
            DeleteMessageByUid(u);
    }

    /* Déplace un message identifié par son UID vers un autre dossier.
     * Paramètres :
     *   uid          : identifiant du message à déplacer
     *   dest_account : identifiant du compte de destination
     *   dest_folder  : chemin complet du dossier de destination */
    void MainWindow::MoveMessageByUid(hstring const& uid, hstring const& dest_account, hstring const& dest_folder)
    {
        if (uid.empty() || m_account.empty() || m_folder.empty() || dest_account.empty() || dest_folder.empty())
            return;

        if (dest_account == m_account && dest_folder == m_folder)
            return;

        hstring src_account = m_account;
        hstring src_folder = m_folder;

        RemoveMessageFromList(uid);

        m_core->MoveMessage(src_account, src_folder, uid, dest_account, dest_folder,
            [this, dest_folder](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Déplacement : " + error, InfoBarSeverity::Error);
                LoadMessages(false);
                return;
            }

            ShowStatus(L"Message déplacé vers « " + dest_folder + L" ».", InfoBarSeverity::Informational);
        });
    }

    /* Déplace un ensemble de messages identifiés par leurs UID vers un autre dossier.
     * Paramètres :
     *   uids         : liste des identifiants des messages à déplacer
     *   dest_account : identifiant du compte de destination
     *   dest_folder  : chemin complet du dossier de destination */
    void MainWindow::MoveMessagesByUids(std::vector<hstring> const& uids, hstring const& dest_account, hstring const& dest_folder)
    {
        for (auto const& u : uids)
            MoveMessageByUid(u, dest_account, dest_folder);
    }

    /* Ajoute récursivement les dossiers d'un compte dans un menu « Déplacer vers ».
     * Paramètres :
     *   items   : collection d'éléments du menu à remplir
     *   folders : tableau JSON des dossiers à parcourir
     *   account : identifiant du compte propriétaire des dossiers
     *   prefix  : préfixe d'affichage pour les sous-dossiers
     *   uids    : identifiants des messages à déplacer */
    void MainWindow::AppendFolderMoveItems(
        Collections::IVector<MenuFlyoutItemBase> const& items,
        JsonArray const& folders,
        hstring const& account,
        hstring const& prefix,
        std::vector<hstring> const& uids)
    {
        for (auto const& value : folders)
        {
            auto folder = value.GetObject();
            hstring full_name = folder.GetNamedString(L"full_name");
            hstring name = folder.GetNamedString(L"name", full_name);
            hstring label = prefix.empty() ? name : prefix + L" / " + name;
            bool selectable = folder.GetNamedBoolean(L"selectable", true);

            if (selectable && !(account == m_account && full_name == m_folder))
            {
                MenuFlyoutItem item;
                item.Text(label);
                item.Icon(FontIcon{});
                item.Icon().as<FontIcon>().Glyph(FolderGlyph(folder.GetNamedString(L"type", L"")));
                item.Click([this, uids, account, full_name](IInspectable const&, RoutedEventArgs const&)
                {
                    MoveMessagesByUids(uids, account, full_name);
                });
                items.Append(item);
            }

            AppendFolderMoveItems(items, folder.GetNamedArray(L"children", JsonArray{}), account, label, uids);
        }
    }

    /* Remplit un menu « Déplacer vers » avec les dossiers du compte courant.
     * Paramètres :
     *   items : collection d'éléments du menu à remplir
     *   uids  : identifiants des messages à déplacer */
    void MainWindow::PopulateMoveMenuItems(Collections::IVector<MenuFlyoutItemBase> const& items, std::vector<hstring> const& uids)
    {
        items.Clear();

        auto it = m_account_folders.find(std::wstring{ m_account });
        if (it != m_account_folders.end() && it->second)
            AppendFolderMoveItems(items, it->second, m_account, L"", uids);

        if (items.Size() == 0)
        {
            MenuFlyoutItem empty_item;
            empty_item.Text(L"Aucun autre dossier disponible");
            empty_item.IsEnabled(false);
            items.Append(empty_item);
        }
    }

    /* Gestionnaire du bouton « Lu / Non lu » du volet de lecture. */
    void MainWindow::OnToggleReadClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto uids = SelectedMessageUids();
        if (uids.empty())
            return;

        auto it = m_message_states.find(std::wstring{ uids.front() });
        bool seen = it != m_message_states.end() ? it->second.seen : true;
        SetMessagesFlag(uids, L"seen", !seen);
    }

    /* Gestionnaire du bouton « Suivi » (drapeau) du volet de lecture. */
    void MainWindow::OnToggleFlagClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto uids = SelectedMessageUids();
        if (uids.empty())
            return;

        auto it = m_message_states.find(std::wstring{ uids.front() });
        bool flagged = it != m_message_states.end() ? it->second.flagged : false;
        SetMessagesFlag(uids, L"flagged", !flagged);
    }

    /* Gestionnaire du bouton « Supprimer » du volet de lecture. */
    void MainWindow::OnDeleteClick(IInspectable const&, RoutedEventArgs const&)
    {
        DeleteMessagesByUids(SelectedMessageUids());
    }

    /* Gestionnaire du bouton « Imprimer » (Ctrl+P) du volet de lecture :
     * ouvre la boîte de dialogue d'impression native de WebView2 pour le
     * message actuellement affiché. */
    void MainWindow::OnPrintClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_current_message || !m_reader_ready || !Reader().CoreWebView2())
        {
            ShowStatus(L"Sélectionnez d'abord un message à imprimer.", InfoBarSeverity::Informational);
            return;
        }

        try
        {
            Reader().CoreWebView2().ShowPrintUI(Microsoft::Web::WebView2::Core::CoreWebView2PrintDialogKind::Browser);
        }
        catch (hresult_error const& e)
        {
            ShowStatus(L"Impossible d'ouvrir l'impression : " + e.message(), InfoBarSeverity::Error);
        }
    }

    /* Gestionnaire d'ouverture du menu « Déplacer » du volet de lecture. */
    void MainWindow::OnMoveMenuOpening(IInspectable const&, IInspectable const&)
    {
        PopulateMoveMenuItems(MoveMenu().Items(), SelectedMessageUids());
    }

    /* Filtre instantanément en mémoire les messages chargés du dossier selon
     * l'objet et l'expéditeur.
     * Paramètres :
     *   query : texte saisi dans la zone de recherche */
    void MainWindow::FilterLocalMessages(hstring const& query)
    {
        if (!m_all_messages)
            return;

        if (query.empty())
        {
            ShowMessages(m_all_messages);
            return;
        }

        std::wstring needle{ query };
        CharLowerBuffW(needle.data(), static_cast<DWORD>(needle.size()));

        JsonArray filtered;
        for (auto const& value : m_all_messages)
        {
            auto msg = value.GetObject();
            std::wstring haystack = std::wstring{ msg.GetNamedString(L"subject", L"") } + L" " +
                std::wstring{ msg.GetNamedString(L"from", L"") };
            CharLowerBuffW(haystack.data(), static_cast<DWORD>(haystack.size()));

            if (haystack.find(needle) != std::wstring::npos)
                filtered.Append(msg);
        }

        ShowMessages(filtered);
    }

    /* Filtres rapides au-dessus de la liste (Tous / Non lus / Lus, indicateur,
     * pièces jointes, cette semaine), appliqués par ShowMessages à toute liste
     * affichée, donc combinés avec la recherche.
     * Paramètres :
     *   message : résumé JSON d'un message
     * Retourne : true si le message passe tous les filtres actifs. */
    bool MainWindow::MatchesQuickFilters(JsonObject const& message)
    {
        bool seen = message.GetNamedBoolean(L"seen", true);

        if (auto item = ReadFilterBar().SelectedItem())
        {
            hstring read_filter = unbox_value_or<hstring>(item.Tag(), L"all");
            if ((read_filter == L"unread" && seen) || (read_filter == L"read" && !seen))
                return false;
        }

        if (FlaggedFilterButton().IsChecked().GetBoolean() && !message.GetNamedBoolean(L"flagged", false))
            return false;
        if (AttachmentFilterButton().IsChecked().GetBoolean() && !message.GetNamedBoolean(L"attachments", false))
            return false;

        if (ThisWeekFilterButton().IsChecked().GetBoolean())
        {
            /* depuis lundi 0 h (heure locale) */
            std::time_t now = std::time(nullptr);
            std::tm local{};
            localtime_s(&local, &now);
            local.tm_mday -= (local.tm_wday + 6) % 7;
            local.tm_hour = local.tm_min = local.tm_sec = 0;
            local.tm_isdst = -1;
            if (message.GetNamedNumber(L"date", 0) < static_cast<double>(std::mktime(&local)))
                return false;
        }

        return true;
    }

    /* Réaffiche la liste courante avec les filtres rapides et la recherche locale. */
    void MainWindow::ReapplyMessageFilters()
    {
        if (!m_all_messages)
            return;
        if (!SearchBox().Text().empty())
            FilterLocalMessages(SearchBox().Text());
        else
            ShowMessages(m_all_messages);
    }

    void MainWindow::OnQuickFilterChanged(SelectorBar const&, SelectorBarSelectionChangedEventArgs const&)
    {
        ReapplyMessageFilters();
    }

    void MainWindow::OnQuickFilterToggled(IInspectable const&, RoutedEventArgs const&)
    {
        ReapplyMessageFilters();
    }

    /* Gestionnaire de modification du texte de recherche : filtre la liste en
     * direct ou la restaure lorsque le champ est vidé. */
    void MainWindow::OnSearchTextChanged(AutoSuggestBox const& sender, AutoSuggestBoxTextChangedEventArgs const& args)
    {
        if (args.Reason() == AutoSuggestionBoxTextChangeReason::UserInput)
        {
            ++m_search_request;
            FilterLocalMessages(sender.Text());
        }
    }

    /* Gestionnaire de validation de la recherche (touche Entrée ou icône
     * loupe) : lance une recherche complète dans le dossier (en-têtes et corps
     * des messages) via evo-core. */
    void MainWindow::OnSearchQuerySubmitted(AutoSuggestBox const&, AutoSuggestBoxQuerySubmittedEventArgs const& args)
    {
        hstring query = args.QueryText();
        if (query.empty() || m_account.empty() || m_folder.empty())
        {
            FilterLocalMessages(query);
            return;
        }

        hstring account = m_account;
        hstring folder = m_folder;
        uint64_t request = ++m_search_request;

        ListProgress().IsActive(true);

        m_core->SearchMessages(account, folder, query, 500,
            [this, account, folder, request](hstring const& json, hstring const& error)
        {
            if (request != m_search_request || account != m_account || folder != m_folder)
                return;

            ListProgress().IsActive(false);

            if (!error.empty())
            {
                ShowStatus(L"Recherche : " + error, InfoBarSeverity::Error);
                return;
            }

            ShowMessages(JsonArray::Parse(json));
        });
    }

    /* -------------------------------------------------------------------- */
    /* Étape 1 : Notifications Windows (toasts) et pièces jointes            */
    /* -------------------------------------------------------------------- */

    /* Affiche une notification système Windows (toast) ainsi qu'un message
     * dans la barre d'état à l'arrivée de nouveaux courriers non lus.
     * Paramètres :
     *   object : objet JSON décrivant les nouveaux messages ("from", "subject", "count") */
    void MainWindow::ShowNewMailNotification(JsonObject const& object)
    {
        int32_t count = static_cast<int32_t>(object.GetNamedNumber(L"count", 1));
        hstring from = SenderName(object.GetNamedString(L"from", L"Nouveau message"));
        hstring subject = object.GetNamedString(L"subject", L"(Sans objet)");

        hstring title = count > 1
            ? hstring{ std::to_wstring(count) + L" nouveaux messages" }
            : L"Nouveau message de " + from;
        hstring body = count > 1
            ? from + L" — " + subject
            : subject;

        /* mode confidentiel : ni expéditeur ni objet (présentation, partage d'écran) */
        if (::EvolutionWin::Settings::Instance().GetBoolean(L"notifications.confidential", false))
        {
            title = count > 1 ? hstring{ std::to_wstring(count) + L" nouveaux messages" } : hstring{ L"Nouveau message" };
            body = count > 1 ? L"Ouvrez Evolution pour les lire." : L"Ouvrez Evolution pour le lire.";
        }

        ShowStatus(title + L" : " + body, InfoBarSeverity::Informational);

        try
        {
            Microsoft::Windows::AppNotifications::Builder::AppNotificationBuilder builder;
            builder.AddText(title);
            builder.AddText(body);
            Microsoft::Windows::AppNotifications::AppNotificationManager::Default().Show(builder.BuildNotification());
        }
        catch (...)
        {
            /* Si l'application non empaquetée n'a pas pu enregistrer de canal toast,
               l'InfoBar reste visible pour prévenir l'utilisateur. */
        }
    }

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
            ToolTipService::SetToolTip(button, box_value(L"Enregistrer « " + filename + L" »"));
            button.Click([this, index, filename](IInspectable const&, RoutedEventArgs const&)
            {
                SaveAttachmentAsync(index, filename);
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

    /* Met à jour le bandeau de sécurité cryptographique (OpenPGP / S/MIME) du
     * volet de lecture selon l'objet JSON "security" renvoyé par evo-core.
     * Paramètres :
     *   security : objet JSON ("encrypted", "signed", "valid", "protocol", "signer", "description") */
    void MainWindow::UpdateSecurityBanner(JsonObject const& security)
    {
        bool encrypted = security.GetNamedBoolean(L"encrypted", false);
        bool signed_msg = security.GetNamedBoolean(L"signed", false);

        if (!encrypted && !signed_msg)
        {
            SecurityBar().IsOpen(false);
            return;
        }

        bool valid = security.GetNamedBoolean(L"valid", false);
        hstring protocol = security.GetNamedString(L"protocol", L"");
        hstring signer = security.GetNamedString(L"signer", L"");
        hstring desc = security.GetNamedString(L"description", L"");

        std::wstring title = protocol.empty() ? L"Sécurité" : std::wstring{ protocol } + L" : ";
        if (encrypted && signed_msg)
            title += valid ? L"Message déchiffré et signature vérifiée" : L"Message chiffré / signé (vérification incomplète)";
        else if (encrypted)
            title += valid ? L"Message déchiffré" : L"Échec du déchiffrement";
        else
            title += valid ? L"Signature numérique valide" : L"Signature numérique non vérifiée ou invalide";

        std::wstring msg;
        if (!signer.empty())
            msg = L"Signataire : " + std::wstring{ signer };
        if (!desc.empty())
        {
            if (!msg.empty())
                msg += L" — ";
            msg += desc;
        }

        SecurityBar().Title(hstring{ title });
        SecurityBar().Message(hstring{ msg });
        SecurityBar().Severity(valid ? InfoBarSeverity::Success : InfoBarSeverity::Warning);
        SecurityBar().IsOpen(true);
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

    /* -------------------------------------------------------------------- */
    /* Étape 2 : Gestion des comptes et boîtes de dialogue d'authentification */
    /* -------------------------------------------------------------------- */

    /* Affiche une boîte de dialogue modale demandant le mot de passe d'une
     * source EDS lorsque celui-ci est absent ou refusé par le serveur, puis
     * l'enregistre et relance la synchronisation.
     * Paramètres :
     *   source_uid : identifiant de la source EDS concernée
     *   name       : nom d'affichage du compte ou du serveur
     *   user       : identifiant utilisateur associé
     *   rejected   : true si le mot de passe précédent a été refusé */
    fire_and_forget MainWindow::PromptPasswordAsync(hstring source_uid, hstring name, hstring user, bool rejected)
    {
        auto strong = get_strong();
        std::wstring banner_id = L"auth:" + std::wstring{ source_uid };
        auto remind = [this, banner_id, source_uid, name, user]()
        {
            /* fenêtre fermée ou déjà une autre boîte ouverte : rappel dans le bandeau */
            ShowBanner(banner_id, L"Connexion requise",
                L"Le compte « " + name + L" » attend son mot de passe.",
                InfoBarSeverity::Warning, L"Saisir le mot de passe",
                [this, source_uid, name, user]() { PromptPasswordAsync(source_uid, name, user, false); });
        };

        if (m_dialog_open || !Content() || !Content().XamlRoot())
        {
            remind();
            co_return;
        }

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Authentification requise"));
        dialog.PrimaryButtonText(L"Valider");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);

        TextBlock description;
        description.TextWrapping(TextWrapping::Wrap);
        description.Text((rejected
            ? L"Le serveur a refusé le mot de passe pour « "
            : L"Veuillez saisir le mot de passe pour « ")
            + name + (user.empty() ? L" » :" : L" » (" + user + L") :"));
        panel.Children().Append(description);

        PasswordBox password_box;
        password_box.PlaceholderText(L"Mot de passe");
        panel.Children().Append(password_box);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        hstring password = password_box.Password();
        if (result != ContentDialogResult::Primary || password.empty())
        {
            remind();
            co_return;
        }

        m_core->SavePassword(source_uid, password, [this, name, banner_id](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Mot de passe : " + error, InfoBarSeverity::Error);
                return;
            }
            ClearBanner(banner_id);
            ShowStatus(L"Mot de passe enregistré pour « " + name + L" ».", InfoBarSeverity::Success);
            m_core->Refresh();
        });
    }

    /* Affiche ou remplace un avis du bandeau d'information (vues Courrier et
     * Calendrier). Le dernier avis est visible ; la croix le masque.
     * Paramètres :
     *   id           : identifiant de l'avis (ex. "auth:<uid de la source>")
     *   title        : titre en gras
     *   message      : texte de l'avis
     *   severity     : gravité (couleur et icône)
     *   action_label : libellé du bouton d'action ("" : aucun bouton)
     *   action       : action du bouton (l'avis reste affiché jusqu'à ClearBanner) */
    void MainWindow::ShowBanner(std::wstring const& id, hstring const& title, hstring const& message,
        InfoBarSeverity severity, hstring const& action_label, std::function<void()> action)
    {
        ClearBanner(id);
        m_banners.push_back(Banner{ id, title, message, severity, action_label, std::move(action) });
        RefreshBanner();
    }

    /* Retire un avis du bandeau (problème réglé ou avis fermé).
     * Paramètres :
     *   id : identifiant de l'avis */
    void MainWindow::ClearBanner(std::wstring const& id)
    {
        auto it = std::remove_if(m_banners.begin(), m_banners.end(),
            [&id](Banner const& banner) { return banner.id == id; });
        if (it == m_banners.end())
            return;
        m_banners.erase(it, m_banners.end());
        RefreshBanner();
    }

    /* Affiche le dernier avis, seulement dans les vues Courrier et Calendrier. */
    void MainWindow::RefreshBanner()
    {
        bool visible_view = m_view_tag == L"mail" || m_view_tag == L"calendar";
        if (m_banners.empty() || !visible_view)
        {
            InfoBanner().IsOpen(false);
            return;
        }

        Banner const& banner = m_banners.back();
        InfoBanner().Title(banner.title);
        InfoBanner().Message(banner.message);
        InfoBanner().Severity(banner.severity);
        InfoBanner().Tag(box_value(hstring{ banner.id }));

        if (!banner.action_label.empty() && banner.action)
        {
            Button action;
            action.Content(box_value(banner.action_label));
            auto run = banner.action;
            action.Click([run](IInspectable const&, RoutedEventArgs const&) { run(); });
            InfoBanner().ActionButton(action);
        }
        else
        {
            InfoBanner().ActionButton(nullptr);
        }

        InfoBanner().IsOpen(true);
    }

    /* Ruban des vues Courrier et Calendrier : onglets visibles selon la vue,
     * barre de commandes de l'onglet choisi, commandes de l'onglet Dossier
     * activées selon le dossier affiché (dossiers spéciaux protégés). */
    void MainWindow::UpdateRibbon()
    {
        bool mail = m_view_tag == L"mail";
        bool calendar = m_view_tag == L"calendar";

        Ribbon().Visibility(mail || calendar ? Visibility::Visible : Visibility::Collapsed);
        if (!mail && !calendar)
            return;

        RibbonSendReceiveTab().Visibility(mail ? Visibility::Visible : Visibility::Collapsed);
        RibbonFolderTab().Visibility(mail ? Visibility::Visible : Visibility::Collapsed);

        hstring tab = L"home";
        if (auto selected = RibbonTabs().SelectedItem())
            tab = unbox_value_or<hstring>(selected.Tag(), L"home");
        if (calendar && (tab == L"sendreceive" || tab == L"folder"))
        {
            tab = L"home";
            for (auto const& item : RibbonTabs().Items())
                if (unbox_value_or<hstring>(item.Tag(), L"") == L"home")
                    RibbonTabs().SelectedItem(item);
        }

        auto show = [](CommandBar const& bar, bool visible)
        {
            bar.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
        };
        show(RibbonMailFile(), mail && tab == L"file");
        show(RibbonMailHome(), mail && tab == L"home");
        show(RibbonSendReceive(), mail && tab == L"sendreceive");
        show(RibbonFolder(), mail && tab == L"folder");
        show(RibbonMailView(), mail && tab == L"view");
        show(RibbonCalendarFile(), calendar && tab == L"file");
        show(RibbonCalendarHome(), calendar && tab == L"home");
        show(RibbonCalendarView(), calendar && tab == L"view");

        /* onglet Dossier : selon le dossier affiché */
        auto item = CurrentFolderItem();
        JsonObject folder{ nullptr };
        if (item)
        {
            if (auto folders = m_account_folders.find(std::wstring{ m_account }); folders != m_account_folders.end() && folders->second)
                folder = FindFolderJson(folders->second, m_folder);
        }
        hstring type = folder ? folder.GetNamedString(L"type", L"") : hstring{};
        bool special = !type.empty() && type != L"archive";
        bool has_children = folder && folder.GetNamedArray(L"children", JsonArray{}).Size() > 0;

        RibbonRenameFolder().IsEnabled(item && !special);
        RibbonFolderMore().IsEnabled(item != nullptr);
        RibbonEmptyFolder().IsEnabled(item != nullptr);
        RibbonEmptyFolder().Label(type == L"trash" ? L"Vider la Corbeille" : type == L"junk" ? L"Vider les Indésirables" : L"Vider le dossier");
        RibbonDeleteFolder().IsEnabled(item && !special && !has_children);
    }

    /* Élément de l'arborescence du dossier affiché (nullptr sans dossier). */
    EvolutionWin::FolderItem MainWindow::CurrentFolderItem()
    {
        if (m_account.empty() || m_folder.empty())
            return nullptr;
        auto node = m_nodes.find(NodeKey(m_account, m_folder));
        return node != m_nodes.end() ? node->second.Content().try_as<EvolutionWin::FolderItem>() : nullptr;
    }

    void MainWindow::OnRibbonTabChanged(SelectorBar const&, SelectorBarSelectionChangedEventArgs const&)
    {
        UpdateRibbon();
    }

    void MainWindow::OnRibbonSettingsClick(IInspectable const&, RoutedEventArgs const&)
    {
        Navigation().SelectedItem(Navigation().SettingsItem());
    }

    void MainWindow::OnRibbonAboutClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowAboutDialogAsync();
    }

    void MainWindow::OnRibbonQuitClick(IInspectable const&, RoutedEventArgs const&)
    {
        Close();
    }

    /* onglet Dossier : commandes sur le dossier affiché (ou au premier niveau
     * du compte affiché pour Nouveau dossier sans dossier) */
    void MainWindow::OnRibbonNewFolderClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (auto item = CurrentFolderItem())
            CreateFolderAsync(m_account, m_folder, item.Name());
        else if (!m_account.empty())
            CreateFolderAsync(m_account, L"", L"");
    }

    void MainWindow::OnRibbonRenameFolderClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (auto item = CurrentFolderItem())
            RenameFolderAsync(m_account, m_folder, item.Name());
    }

    /* Déplacer / Copier : mêmes entrées que le menu contextuel du dossier */
    void MainWindow::OnRibbonFolderMenuOpening(IInspectable const&, IInspectable const&)
    {
        RibbonFolderMenu().Items().Clear();
        if (auto item = CurrentFolderItem())
            AppendFolderManagementItems(RibbonFolderMenu(), item, m_folder);
    }

    void MainWindow::OnRibbonMarkAllReadClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (auto item = CurrentFolderItem())
            RunFolderActionAsync(item, L"mark-all-read");
    }

    void MainWindow::OnRibbonEmptyFolderClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (auto item = CurrentFolderItem())
            RunFolderActionAsync(item, L"empty");
    }

    void MainWindow::OnRibbonDeleteFolderClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto item = CurrentFolderItem();
        if (!item)
            return;
        JsonObject folder{ nullptr };
        if (auto folders = m_account_folders.find(std::wstring{ m_account }); folders != m_account_folders.end() && folders->second)
            folder = FindFolderJson(folders->second, m_folder);
        DeleteFolderAsync(m_account, m_folder, item.Name(),
            folder ? static_cast<int32_t>(folder.GetNamedNumber(L"total", 0)) : 0);
    }

    /* onglet Affichage (Courrier) : mêmes filtres que la barre au-dessus de la liste */
    void MainWindow::OnRibbonReadFilterClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        hstring tag = unbox_value_or<hstring>(sender.as<FrameworkElement>().Tag(), L"all");
        for (auto const& item : ReadFilterBar().Items())
        {
            if (unbox_value_or<hstring>(item.Tag(), L"") == tag)
                ReadFilterBar().SelectedItem(item);
        }
    }

    void MainWindow::OnRibbonFolderPaneToggled(IInspectable const&, RoutedEventArgs const&)
    {
        bool visible = RibbonFolderPaneToggle().IsChecked().GetBoolean();
        ::EvolutionWin::Settings::Instance().SetBoolean(L"layout.folders-visible", visible);
        ApplyFolderPaneVisibility(visible);
    }

    /* Volet des dossiers affiché ou masqué (largeur mémorisée conservée).
     * Paramètres :
     *   visible : true pour afficher le volet */
    void MainWindow::ApplyFolderPaneVisibility(bool visible)
    {
        RibbonFolderPaneToggle().IsChecked(visible);
        FoldersSplitter().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
        if (visible)
            RestorePaneWidths();
        else
            FoldersColumn().Width(GridLengthHelper::FromPixels(0));
    }

    /* largeurs par défaut des panneaux, volet des dossiers réaffiché */
    void MainWindow::OnRibbonResetLayoutClick(IInspectable const&, RoutedEventArgs const&)
    {
        for (PaneLayout const* pane : { &FoldersPane, &ListPane })
            ::EvolutionWin::Settings::Instance().SetNumber(pane->key, pane->default_width);
        ::EvolutionWin::Settings::Instance().SetBoolean(L"layout.folders-visible", true);
        ApplyFolderPaneVisibility(true);
    }

    /* onglet Affichage (Calendrier) */
    void MainWindow::OnRibbonTodayClick(IInspectable const&, RoutedEventArgs const&)
    {
        MonthCalendar().SelectedDates().Clear();
        MonthCalendar().SelectedDates().Append(winrt::clock::now());
        MonthCalendar().SetDisplayDate(winrt::clock::now());
    }

    void MainWindow::OnRibbonNextDaysClick(IInspectable const&, RoutedEventArgs const&)
    {
        MonthCalendar().SelectedDates().Clear();
    }

    /* Commandes proposées dans la barre d'accès rapide (identifiant mémorisé,
     * libellé, glyphe, action) */
    struct QuickAccessCommand
    {
        wchar_t const* id;
        wchar_t const* label;
        wchar_t const* glyph;
        void (MainWindow::*handler)(IInspectable const&, RoutedEventArgs const&);
    };

    static QuickAccessCommand const QuickAccessCommands[] = {
        { L"sendreceive", L"Envoyer/Recevoir (F5)", L"\xE72C", &MainWindow::OnRefreshClick },
        { L"sendreceive-all", L"Envoyer/Recevoir tous les dossiers (F9)", L"\xE895", &MainWindow::OnRefreshAllClick },
        { L"new-message", L"Nouveau message", L"\xE710", &MainWindow::OnNewMessageClick },
        { L"reply", L"Répondre", L"\xE97A", &MainWindow::OnReplyClick },
        { L"delete", L"Supprimer", L"\xE74D", &MainWindow::OnDeleteClick },
        { L"print", L"Imprimer", L"\xE749", &MainWindow::OnPrintClick },
        { L"confidential", L"Notifications confidentielles (activer / désactiver)", L"\xE72E", &MainWindow::OnConfidentialToggled },
        { L"new-event", L"Nouvel événement", L"\xE787", &MainWindow::OnNewEventClick },
        { L"new-task", L"Nouvelle tâche", L"\xE9D5", &MainWindow::OnNewTaskClick },
    };

    /* Commande présente dans la barre d'accès rapide (préférence
     * "quick-access.commands" ; par défaut Envoyer/Recevoir et Nouveau message). */
    static bool IsQuickAccessChosen(hstring const& id)
    {
        auto& settings = ::EvolutionWin::Settings::Instance();
        if (!settings.GetBoolean(L"quick-access.customized", false))
            return id == L"sendreceive" || id == L"new-message";

        for (auto const& value : settings.GetArray(L"quick-access.commands"))
            if (value.ValueType() == JsonValueType::String && value.GetString() == id)
                return true;
        return false;
    }

    /* Barre d'accès rapide de la barre de titre : boutons des commandes
     * choisies puis bouton de personnalisation (menu à cocher). */
    void MainWindow::RebuildQuickAccess()
    {
        auto is_chosen = [](wchar_t const* id) { return IsQuickAccessChosen(id); };

        auto panel = QuickAccessBar().Children();
        panel.Clear();

        auto small_button = [](Controls::Primitives::ButtonBase const& button, wchar_t const* glyph, hstring const& tip)
        {
            FontIcon icon;
            icon.Glyph(glyph);
            icon.FontSize(13);
            button.Content(icon);
            button.Width(32);
            button.Height(28);
            button.Padding(ThicknessHelper::FromUniformLength(0));
            button.Background(SolidColorBrush{ Microsoft::UI::Colors::Transparent() });
            button.BorderThickness(ThicknessHelper::FromUniformLength(0));
            ToolTipService::SetToolTip(button, box_value(tip));
        };

        /* dans l'ordre de la liste des commandes */
        for (auto const& command : QuickAccessCommands)
        {
            if (!is_chosen(command.id))
                continue;
            Button button;
            small_button(button, command.glyph, command.label);
            auto handler = command.handler;
            button.Click([this, handler](IInspectable const&, RoutedEventArgs const&) { (this->*handler)(nullptr, nullptr); });
            panel.Append(button);
        }

        /* personnalisation : commandes à cocher */
        DropDownButton customize;
        small_button(customize, L"\xE70D", L"Personnaliser la barre d'accès rapide");
        /* le DropDownButton dessine déjà son chevron */
        customize.Content(nullptr);
        customize.Width(28);
        MenuFlyout menu;
        for (auto const& command : QuickAccessCommands)
        {
            ToggleMenuFlyoutItem entry;
            entry.Text(command.label);
            entry.IsChecked(is_chosen(command.id));
            hstring id = command.id;
            entry.Click([this, id](IInspectable const& sender, RoutedEventArgs const&)
            {
                /* la commande cochée / décochée, les autres inchangées */
                bool checked = sender.as<ToggleMenuFlyoutItem>().IsChecked();
                JsonArray current;
                for (auto const& candidate : QuickAccessCommands)
                {
                    if (candidate.id == id ? checked : IsQuickAccessChosen(candidate.id))
                        current.Append(JsonValue::CreateStringValue(candidate.id));
                }
                auto& prefs = ::EvolutionWin::Settings::Instance();
                prefs.Set(L"quick-access.commands", current);
                prefs.SetBoolean(L"quick-access.customized", true);
                RebuildQuickAccess();
            });
            menu.Items().Append(entry);
        }
        customize.Flyout(menu);
        panel.Append(customize);

        UpdateTitleBarPassthrough();
    }

    void MainWindow::OnQuickAccessSizeChanged(IInspectable const&, SizeChangedEventArgs const&)
    {
        UpdateTitleBarPassthrough();
    }

    /* La barre de titre (SetTitleBar) sert à déplacer la fenêtre : la zone de
     * la barre d'accès rapide y est déclarée « passthrough » pour que ses
     * boutons reçoivent les clics (pixels physiques, relatifs à la fenêtre). */
    void MainWindow::UpdateTitleBarPassthrough()
    {
        if (!QuickAccessBar().XamlRoot() || QuickAccessBar().ActualWidth() <= 0)
            return;

        double scale = QuickAccessBar().XamlRoot().RasterizationScale();
        auto bounds = QuickAccessBar().TransformToVisual(nullptr).TransformBounds(
            Rect{ 0, 0, static_cast<float>(QuickAccessBar().ActualWidth()), static_cast<float>(QuickAccessBar().ActualHeight()) });

        Windows::Graphics::RectInt32 rect{
            static_cast<int32_t>(bounds.X * scale), static_cast<int32_t>(bounds.Y * scale),
            static_cast<int32_t>(bounds.Width * scale), static_cast<int32_t>(bounds.Height * scale) };

        auto source = Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(AppWindow().Id());
        source.SetRegionRects(Microsoft::UI::Input::NonClientRegionKind::Passthrough, { rect });
    }

    /* Critères de tri de la liste des messages : clé mémorisée, libellé,
     * ordre par défaut (décroissant pour la date et la taille). */
    struct SortCriterion
    {
        wchar_t const* key;
        wchar_t const* label;
        bool descending;
    };

    static SortCriterion const SortCriteria[] = {
        { L"date", L"Date", true },
        { L"from", L"Expéditeur", false },
        { L"to", L"Destinataire", false },
        { L"subject", L"Objet", false },
        { L"size", L"Taille", true },
        { L"attachments", L"Pièces jointes", true },
        { L"flagged", L"Indicateur", true },
        { L"seen", L"Lu / non lu", false },
        { L"categories", L"Catégories", false },
    };

    /* Préférence de tri du dossier affiché ("sort.<compte>/<dossier>" =
     * "<critère>:asc|desc", par défaut la date décroissante). */
    void MainWindow::LoadFolderSort()
    {
        hstring saved = ::EvolutionWin::Settings::Instance().GetString(
            L"sort." + m_account + L"/" + m_folder, L"date:desc");
        std::wstring value{ saved };
        auto colon = value.find(L':');

        m_sort_key = L"date";
        m_sort_descending = true;
        for (auto const& criterion : SortCriteria)
        {
            if (value.substr(0, colon) == criterion.key)
            {
                m_sort_key = criterion.key;
                m_sort_descending = colon == std::wstring::npos || value.substr(colon + 1) != L"asc";
            }
        }
        UpdateSortHeader();
    }

    void MainWindow::SaveFolderSort()
    {
        if (!m_account.empty() && !m_folder.empty())
            ::EvolutionWin::Settings::Instance().SetString(L"sort." + m_account + L"/" + m_folder,
                m_sort_key + (m_sort_descending ? L":desc" : L":asc"));
        UpdateSortHeader();
        ReapplyMessageFilters();
    }

    /* Ligne « Trier par » : critère et sens (flèche). */
    void MainWindow::UpdateSortHeader()
    {
        for (auto const& criterion : SortCriteria)
            if (m_sort_key == criterion.key)
                SortButton().Content(box_value(hstring{ criterion.label }));
        SortDirectionIcon().Glyph(m_sort_descending ? L"\xE74B" : L"\xE74A");
        ToolTipService::SetToolTip(SortDirectionButton(), box_value(m_sort_descending
            ? L"Ordre décroissant (cliquer pour inverser)"
            : L"Ordre croissant (cliquer pour inverser)"));
    }

    /* Menu « Trier par » (ligne au-dessus de la liste et ruban Affichage) :
     * critères puis sens, à choix unique. */
    void MainWindow::OnSortMenuOpening(IInspectable const& sender, IInspectable const&)
    {
        auto menu = sender.as<MenuFlyout>();
        menu.Items().Clear();

        for (auto const& criterion : SortCriteria)
        {
            RadioMenuFlyoutItem entry;
            entry.Text(criterion.label);
            entry.GroupName(L"sort-key");
            entry.IsChecked(m_sort_key == criterion.key);
            hstring key = criterion.key;
            bool descending = criterion.descending;
            entry.Click([this, key, descending](IInspectable const&, RoutedEventArgs const&)
            {
                if (m_sort_key != key)
                {
                    m_sort_key = key;
                    m_sort_descending = descending;
                }
                SaveFolderSort();
            });
            menu.Items().Append(entry);
        }

        menu.Items().Append(MenuFlyoutSeparator{});
        for (bool descending : { false, true })
        {
            RadioMenuFlyoutItem entry;
            entry.Text(descending ? L"Décroissant" : L"Croissant");
            entry.GroupName(L"sort-direction");
            entry.IsChecked(m_sort_descending == descending);
            entry.Click([this, descending](IInspectable const&, RoutedEventArgs const&)
            {
                m_sort_descending = descending;
                SaveFolderSort();
            });
            menu.Items().Append(entry);
        }
    }

    void MainWindow::OnSortDirectionClick(IInspectable const&, RoutedEventArgs const&)
    {
        m_sort_descending = !m_sort_descending;
        SaveFolderSort();
    }

    /* Objet sans préfixes de réponse / transfert, pour le tri. */
    static std::wstring NormalizedSubject(hstring const& subject)
    {
        std::wstring text{ subject };
        for (bool stripped = true; stripped;)
        {
            stripped = false;
            for (wchar_t const* prefix : { L"re:", L"tr:", L"fw:", L"fwd:", L"aw:", L"wg:" })
            {
                size_t len = wcslen(prefix);
                if (text.size() >= len && _wcsnicmp(text.c_str(), prefix, len) == 0)
                {
                    text.erase(0, len);
                    while (!text.empty() && iswspace(text.front()))
                        text.erase(0, 1);
                    stripped = true;
                }
            }
        }
        return text;
    }

    /* Comparaison de textes selon les règles de la langue de l'utilisateur
     * (accents, casse ignorée) ; négatif, 0 ou positif. */
    static int CompareText(std::wstring const& a, std::wstring const& b)
    {
        int result = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | LINGUISTIC_IGNOREDIACRITIC | SORT_DIGITSASNUMBERS,
            a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), nullptr, nullptr, 0);
        return result == 0 ? 0 : result - CSTR_EQUAL;
    }

    /* Trie les messages affichés selon le critère et le sens choisis ; à
     * égalité, du plus récent au plus ancien.
     * Paramètres :
     *   messages : résumés JSON à trier (sur place) */
    void MainWindow::SortMessages(std::vector<JsonObject>& messages)
    {
        hstring key = m_sort_key;
        bool descending = m_sort_descending;

        auto first_label = [](JsonObject const& message)
        {
            auto labels = message.GetNamedArray(L"labels", JsonArray{});
            return labels.Size() > 0 ? std::wstring{ labels.GetStringAt(0) } : std::wstring{};
        };

        auto compare = [&](JsonObject const& a, JsonObject const& b) -> int
        {
            if (key == L"from")
                return CompareText(std::wstring{ SenderName(a.GetNamedString(L"from", L"")) },
                                   std::wstring{ SenderName(b.GetNamedString(L"from", L"")) });
            if (key == L"to")
                return CompareText(std::wstring{ SenderName(a.GetNamedString(L"to", L"")) },
                                   std::wstring{ SenderName(b.GetNamedString(L"to", L"")) });
            if (key == L"subject")
                return CompareText(NormalizedSubject(a.GetNamedString(L"subject", L"")),
                                   NormalizedSubject(b.GetNamedString(L"subject", L"")));
            if (key == L"size")
            {
                double sa = a.GetNamedNumber(L"size", 0), sb = b.GetNamedNumber(L"size", 0);
                return sa < sb ? -1 : sa > sb ? 1 : 0;
            }
            if (key == L"attachments" || key == L"flagged" || key == L"seen")
                return static_cast<int>(a.GetNamedBoolean(key, false)) - static_cast<int>(b.GetNamedBoolean(key, false));
            if (key == L"categories")
            {
                /* sans catégorie : toujours à la fin */
                auto la = first_label(a), lb = first_label(b);
                if (la.empty() != lb.empty())
                    return la.empty() ? (descending ? -1 : 1) : (descending ? 1 : -1);
                return CompareText(la, lb);
            }
            double da = a.GetNamedNumber(L"date", 0), db = b.GetNamedNumber(L"date", 0);
            return da < db ? -1 : da > db ? 1 : 0;
        };

        std::stable_sort(messages.begin(), messages.end(), [&](JsonObject const& a, JsonObject const& b)
        {
            int result = compare(a, b);
            if (result != 0)
                return descending ? result > 0 : result < 0;
            /* à égalité : du plus récent au plus ancien */
            return a.GetNamedNumber(L"date", 0) > b.GetNamedNumber(L"date", 0);
        });
    }

    /* Mode confidentiel des notifications : interrupteur des Paramètres,
     * bouton du ruban (Affichage) ou commande de la barre d'accès rapide
     * (sender nul : bascule). */
    void MainWindow::OnConfidentialToggled(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (m_updating_confidential)
            return;

        auto& settings = ::EvolutionWin::Settings::Instance();
        bool enabled = !settings.GetBoolean(L"notifications.confidential", false);

        if (auto toggle = sender.try_as<ToggleSwitch>())
            enabled = toggle.IsOn();
        else if (auto button = sender.try_as<AppBarToggleButton>())
            enabled = button.IsChecked() && button.IsChecked().GetBoolean();

        settings.SetBoolean(L"notifications.confidential", enabled);
        SyncConfidentialControls();
        ShowStatus(enabled
            ? L"Notifications confidentielles : ni expéditeur ni objet dans les notifications."
            : L"Notifications confidentielles désactivées.", InfoBarSeverity::Informational);
    }

    void MainWindow::SyncConfidentialControls()
    {
        bool enabled = ::EvolutionWin::Settings::Instance().GetBoolean(L"notifications.confidential", false);

        m_updating_confidential = true;
        ConfidentialNotificationsSwitch().IsOn(enabled);
        RibbonConfidentialToggle().IsChecked(enabled);
        m_updating_confidential = false;
    }

    /* Croix du bandeau : l'avis affiché est retiré, le suivant apparaît. */
    void MainWindow::OnInfoBannerClosed(InfoBar const& sender, InfoBarClosedEventArgs const&)
    {
        hstring id = unbox_value_or<hstring>(sender.Tag(), L"");
        if (!id.empty())
            ClearBanner(std::wstring{ id });
    }

    /* Affiche une boîte de dialogue modale permettant d'inspecter et d'accepter
     * (pour la session ou de façon permanente) ou de refuser un certificat TLS
     * présentant des erreurs de validation.
     * Paramètres :
     *   object : objet JSON de l'événement "tls-trust-required" */
    fire_and_forget MainWindow::PromptTlsCertificateAsync(JsonObject object)
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;
        hstring trust_id = object.GetNamedString(L"id", L"");
        if (trust_id.empty())
            co_return;

        /* Si une autre boîte de dialogue est ouverte, attend sa fermeture sans perdre la demande */
        while (m_dialog_open)
        {
            co_await winrt::resume_after(std::chrono::milliseconds(150));
            co_await ui_thread;
        }

        if (!Content() || !Content().XamlRoot() || !m_core)
        {
            if (m_core)
                m_core->RespondTlsTrust(trust_id, 0);
            co_return;
        }

        m_dialog_open = true;

        hstring name = object.GetNamedString(L"name", L"Serveur");
        hstring host = object.GetNamedString(L"host", L"");
        hstring subject = object.GetNamedString(L"subject", host);
        hstring issuer = object.GetNamedString(L"issuer", L"(inconnu)");
        hstring fingerprint = object.GetNamedString(L"fingerprint", L"");
        hstring errors = object.GetNamedString(L"errors", L"");

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Certificat TLS non vérifié"));
        dialog.PrimaryButtonText(L"Toujours approuver");
        dialog.SecondaryButtonText(L"Accepter (session)");
        dialog.CloseButtonText(L"Refuser");
        dialog.DefaultButton(ContentDialogButton::Close);

        StackPanel panel;
        panel.Spacing(8);
        panel.MaxWidth(480);

        TextBlock intro;
        intro.TextWrapping(TextWrapping::Wrap);
        intro.Text(L"Le serveur « " + host + L" » (" + name + L") a présenté un certificat TLS qui n'a pas pu être validé automatiquement :");
        panel.Children().Append(intro);

        TextBlock err_block;
        err_block.TextWrapping(TextWrapping::Wrap);
        err_block.Text(errors);
        err_block.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
        panel.Children().Append(err_block);

        TextBlock details;
        details.TextWrapping(TextWrapping::Wrap);
        details.IsTextSelectionEnabled(true);
        details.Text(
            L"Objet : " + subject + L"\n" +
            L"Émetteur : " + issuer + L"\n" +
            L"Empreinte SHA-256 :\n" + fingerprint);
        details.FontFamily(Microsoft::UI::Xaml::Media::FontFamily{ L"Cascadia Mono, Consolas" });
        details.FontSize(12);
        panel.Children().Append(details);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (!m_core)
            co_return;

        if (result == ContentDialogResult::Primary)
        {
            m_core->RespondTlsTrust(trust_id, 2);
            ShowStatus(L"Certificat TLS approuvé de façon permanente pour « " + host + L" ».", InfoBarSeverity::Success);
            m_core->Refresh();
        }
        else if (result == ContentDialogResult::Secondary)
        {
            m_core->RespondTlsTrust(trust_id, 1);
            ShowStatus(L"Certificat TLS accepté pour cette session (« " + host + L" »).", InfoBarSeverity::Informational);
            m_core->Refresh();
        }
        else
        {
            m_core->RespondTlsTrust(trust_id, 0);
            ShowStatus(L"Certificat TLS refusé pour « " + host + L" ».", InfoBarSeverity::Warning);
        }
    }

    /* Exécute un flux d'authentification OAuth2 interactif avec PKCE dans un
     * contrôle WebView2 intégré pour Microsoft 365 / Outlook.com ou Google / Gmail,
     * puis échange le code d'autorisation contre un jeton d'accès et un jeton de
     * rafraîchissement.
     * Paramètres :
     *   provider_index : 1 = Microsoft 365 / Outlook.com, 2 = Google / Gmail
     *   client_id      : identifiant d'application OAuth2
     *   tenant_id      : locataire Entra ID ("common" par défaut)
     *   login_hint     : adresse électronique de l'utilisateur
     * Retourne : chaîne JSON contenant access_token, refresh_token, etc., ou "" si annulé. */
    IAsyncOperation<hstring> MainWindow::StartOAuth2SignInAsync(
        int provider_index, hstring client_id, hstring tenant_id, hstring login_hint)
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;

        if (!Content() || !Content().XamlRoot() || client_id.empty())
            co_return L"";

        /* Génération du code_verifier et du code_challenge PKCE (S256) */
        auto rnd_buf = Windows::Security::Cryptography::CryptographicBuffer::GenerateRandom(32);
        std::wstring verifier{ Windows::Security::Cryptography::CryptographicBuffer::EncodeToBase64String(rnd_buf) };
        for (auto& c : verifier)
        {
            if (c == L'+') c = L'-';
            else if (c == L'/') c = L'_';
        }
        verifier.erase(std::remove(verifier.begin(), verifier.end(), L'='), verifier.end());

        auto utf8_ver = Windows::Security::Cryptography::CryptographicBuffer::ConvertStringToBinary(
            verifier, Windows::Security::Cryptography::BinaryStringEncoding::Utf8);
        auto sha256 = Windows::Security::Cryptography::Core::HashAlgorithmProvider::OpenAlgorithm(
            Windows::Security::Cryptography::Core::HashAlgorithmNames::Sha256());
        auto hash_buf = sha256.HashData(utf8_ver);
        std::wstring challenge{ Windows::Security::Cryptography::CryptographicBuffer::EncodeToBase64String(hash_buf) };
        for (auto& c : challenge)
        {
            if (c == L'+') c = L'-';
            else if (c == L'/') c = L'_';
        }
        challenge.erase(std::remove(challenge.begin(), challenge.end(), L'='), challenge.end());

        std::wstring redirect_uri = L"http://localhost/oauth2callback";
        std::wstring tenant = tenant_id.empty() ? L"common" : std::wstring{ tenant_id };
        std::wstring auth_url;
        std::wstring token_endpoint;
        std::wstring scope;

        if (provider_index == 1)
        {
            /* Microsoft 365 / Outlook.com */
            scope = L"https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/SMTP.Send offline_access";
            auth_url = L"https://login.microsoftonline.com/" + tenant + L"/oauth2/v2.0/authorize"
                L"?client_id=" + std::wstring{ Uri::EscapeComponent(client_id) } +
                L"&response_type=code"
                L"&redirect_uri=" + std::wstring{ Uri::EscapeComponent(redirect_uri) } +
                L"&response_mode=query"
                L"&scope=" + std::wstring{ Uri::EscapeComponent(scope) } +
                L"&code_challenge=" + challenge +
                L"&code_challenge_method=S256" +
                (login_hint.empty() ? L"" : L"&login_hint=" + std::wstring{ Uri::EscapeComponent(login_hint) });
            token_endpoint = L"https://login.microsoftonline.com/" + tenant + L"/oauth2/v2.0/token";
        }
        else
        {
            /* Google / Gmail */
            scope = L"https://mail.google.com/";
            auth_url = L"https://accounts.google.com/o/oauth2/v2/auth"
                L"?client_id=" + std::wstring{ Uri::EscapeComponent(client_id) } +
                L"&response_type=code"
                L"&redirect_uri=" + std::wstring{ Uri::EscapeComponent(redirect_uri) } +
                L"&access_type=offline&prompt=consent"
                L"&scope=" + std::wstring{ Uri::EscapeComponent(scope) } +
                L"&code_challenge=" + challenge +
                L"&code_challenge_method=S256" +
                (login_hint.empty() ? L"" : L"&login_hint=" + std::wstring{ Uri::EscapeComponent(login_hint) });
            token_endpoint = L"https://oauth2.googleapis.com/token";
        }

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(provider_index == 1
            ? L"Connexion OAuth2 — Microsoft 365"
            : L"Connexion OAuth2 — Google / Gmail"));
        dialog.CloseButtonText(L"Annuler");

        WebView2 oauth_web;
        oauth_web.MinWidth(480);
        oauth_web.MinHeight(540);
        dialog.Content(oauth_web);

        auto captured_code = std::make_shared<std::wstring>();
        auto captured_err = std::make_shared<std::wstring>();

        co_await oauth_web.EnsureCoreWebView2Async();
        auto core_wv = oauth_web.CoreWebView2();

        core_wv.NavigationStarting([dialog, captured_code, captured_err, redirect_uri](
            auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NavigationStartingEventArgs const& args)
        {
            std::wstring uri{ args.Uri() };
            if (uri.rfind(redirect_uri, 0) == 0)
            {
                args.Cancel(true);
                try
                {
                    Uri parsed{ uri };
                    auto decoder = parsed.QueryParsed();
                    for (uint32_t i = 0; i < decoder.Size(); ++i)
                    {
                        auto entry = decoder.GetAt(i);
                        if (entry.Name() == L"code")
                            *captured_code = entry.Value();
                        else if (entry.Name() == L"error_description" || entry.Name() == L"error")
                            *captured_err = entry.Value();
                    }
                }
                catch (...) {}
                dialog.Hide();
            }
        });

        oauth_web.Source(Uri{ auth_url });
        co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (captured_code->empty())
        {
            if (!captured_err->empty())
                ShowStatus(L"Authentification OAuth2 refusée : " + hstring{ *captured_err }, InfoBarSeverity::Error);
            co_return L"";
        }

        /* Échange du code d'autorisation contre access_token + refresh_token
         * (co_await interdit dans un catch : l'erreur est affichée après) */
        hstring network_error;
        try
        {
            Windows::Web::Http::HttpClient http;
            std::map<hstring, hstring> form_fields{
                { L"client_id", client_id },
                { L"grant_type", L"authorization_code" },
                { L"code", hstring{ *captured_code } },
                { L"redirect_uri", hstring{ redirect_uri } },
                { L"code_verifier", hstring{ verifier } }
            };
            Windows::Web::Http::HttpFormUrlEncodedContent form_content{ form_fields };
            auto response = co_await http.PostAsync(Uri{ token_endpoint }, form_content);
            hstring body = co_await response.Content().ReadAsStringAsync();
            co_await ui_thread;

            JsonObject tok_obj;
            if (JsonObject::TryParse(body, tok_obj))
            {
                hstring access_tok = tok_obj.GetNamedString(L"access_token", L"");
                if (!access_tok.empty())
                {
                    int64_t exp_in = static_cast<int64_t>(tok_obj.GetNamedNumber(L"expires_in", 3600));
                    int64_t now_s = static_cast<int64_t>(std::time(nullptr));
                    tok_obj.SetNamedValue(L"expires_at", JsonValue::CreateNumberValue(static_cast<double>(now_s + exp_in)));
                    tok_obj.SetNamedValue(L"client_id", JsonValue::CreateStringValue(client_id));
                    tok_obj.SetNamedValue(L"token_endpoint", JsonValue::CreateStringValue(token_endpoint));
                    co_return tok_obj.Stringify();
                }
                hstring err_desc = tok_obj.GetNamedString(L"error_description", tok_obj.GetNamedString(L"error", L"Jeton invalide"));
                ShowStatus(L"Échange du jeton OAuth2 : " + err_desc, InfoBarSeverity::Error);
            }
        }
        catch (hresult_error const& e)
        {
            network_error = e.message();
        }

        if (!network_error.empty())
        {
            co_await ui_thread;
            ShowStatus(L"Erreur réseau OAuth2 : " + network_error, InfoBarSeverity::Error);
        }

        co_return L"";
    }

    /* Connexion OAuth2 d'un compte géré par EDS (Microsoft 365...), à l'ajout
     * du compte ou sur l'événement "oauth2-required" : la page de connexion du
     * fournisseur s'ouvre dans un WebView2 ; la navigation vers l'adresse de
     * redirection (code d'autorisation) est interceptée et transmise au
     * moteur, qui enregistre les jetons (renouvelés ensuite par EDS).
     * Paramètres :
     *   source_uid : uid du compte ou de sa collection
     *   name       : nom du compte, pour le titre et les messages */
    fire_and_forget MainWindow::SignInOAuth2Async(hstring source_uid, hstring name)
    {
        auto strong = get_strong();
        winrt::apartment_context ui_thread;

        if (source_uid.empty() || !m_core)
            co_return;

        /* une autre boîte de dialogue est ouverte : attendre sa fermeture */
        while (m_dialog_open)
        {
            co_await winrt::resume_after(std::chrono::milliseconds(150));
            co_await ui_thread;
        }
        if (!Content() || !Content().XamlRoot())
            co_return;
        m_dialog_open = true;

        /* étape 1 : adresse de connexion (rappel reçu sur ce thread) */
        auto begin_json = std::make_shared<hstring>();
        auto begin_error = std::make_shared<hstring>();
        winrt::handle begun{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        HANDLE begun_event = begun.get();
        m_core->OAuth2Begin(source_uid, [begin_json, begin_error, begun_event](hstring const& json, hstring const& error)
        {
            *begin_json = json;
            *begin_error = error;
            SetEvent(begun_event);
        });
        co_await winrt::resume_on_signal(begun_event);
        co_await ui_thread;

        JsonObject begin;
        if (!begin_error->empty() || !JsonObject::TryParse(*begin_json, begin))
        {
            m_dialog_open = false;
            ShowStatus(L"Connexion à « " + name + L" » : " + *begin_error, InfoBarSeverity::Error);
            co_return;
        }

        hstring credentials_uid = begin.GetNamedString(L"source_uid", source_uid);
        Uri redirect{ begin.GetNamedString(L"redirect_uri", L"") };
        hstring service = begin.GetNamedString(L"service", L"OAuth2");

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Connexion " + service + L" — " + name));
        dialog.CloseButtonText(L"Annuler");

        WebView2 web;
        web.MinWidth(480);
        web.MinHeight(600);
        dialog.Content(web);

        /* même hôte et même chemin que l'adresse de redirection : fin */
        auto reached = std::make_shared<hstring>();
        web.NavigationStarting([dialog, reached, redirect](WebView2 const&,
            Microsoft::Web::WebView2::Core::CoreWebView2NavigationStartingEventArgs const& args)
        {
            try
            {
                Uri target{ args.Uri() };
                if (_wcsicmp(target.Host().c_str(), redirect.Host().c_str()) == 0 &&
                    _wcsicmp(target.Path().c_str(), redirect.Path().c_str()) == 0)
                {
                    args.Cancel(true);
                    *reached = args.Uri();
                    dialog.Hide();
                }
            }
            catch (...) {}
        });
        web.Source(Uri{ begin.GetNamedString(L"uri", L"") });

        co_await dialog.ShowAsync();
        m_dialog_open = false;

        std::wstring banner_id = L"auth:" + std::wstring{ source_uid };
        if (reached->empty())
        {
            ShowStatus(L"Connexion à « " + name + L" » annulée.", InfoBarSeverity::Warning);
            ShowBanner(banner_id, L"Connexion requise",
                L"Le compte « " + name + L" » n'est pas connecté.",
                InfoBarSeverity::Warning, L"Se connecter",
                [this, source_uid, name]() { SignInOAuth2Async(source_uid, name); });
            co_return;
        }

        /* étape 2 : échange du code et enregistrement des jetons */
        ShowStatus(L"Connexion à « " + name + L" »…", InfoBarSeverity::Informational);
        m_core->OAuth2Finish(credentials_uid, *reached, [this, name, banner_id, source_uid](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Connexion à « " + name + L" » : " + error, InfoBarSeverity::Error);
                ShowBanner(banner_id, L"Connexion requise",
                    L"La connexion au compte « " + name + L" » a échoué : " + error,
                    InfoBarSeverity::Error, L"Réessayer",
                    [this, source_uid, name]() { SignInOAuth2Async(source_uid, name); });
                return;
            }
            ClearBanner(banner_id);
            ShowStatus(L"Connecté à « " + name + L" ».", InfoBarSeverity::Success);
            LoadAccounts();
        });
    }

    /* Met à jour la section « Confidentialité » de la vue Paramètres :
     * option globale de contenu externe et listes de confiance. */
    void MainWindow::RefreshPrivacyView()
    {
        auto mode = RemoteContentMode();
        m_updating_privacy_view = true;
        RemoteContentModeChoice().SelectedIndex(mode == L"never" ? 0 : mode == L"always" ? 2 : 1);
        m_updating_privacy_view = false;

        auto items = TrustedSenderList().Items();
        items.Clear();

        auto add_rows = [&](wchar_t const* key, bool domains)
        {
            for (auto const& entry : ReadTrustList(key))
            {
                Grid row;
                row.Padding(ThicknessHelper::FromLengths(12, 6, 12, 6));
                row.ColumnDefinitions().Append(ColumnDefinition{});
                ColumnDefinition action_col;
                action_col.Width(GridLengthHelper::Auto());
                row.ColumnDefinitions().Append(action_col);

                StackPanel info;
                info.Spacing(2);
                info.VerticalAlignment(VerticalAlignment::Center);
                TextBlock title;
                title.Text(hstring{ domains ? L"@" + entry : entry });
                title.IsTextSelectionEnabled(true);
                info.Children().Append(title);
                TextBlock kind;
                kind.Text(domains ? L"Domaine" : L"Expéditeur");
                kind.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                info.Children().Append(kind);
                row.Children().Append(info);

                Button remove_btn;
                remove_btn.Content(box_value(L"Retirer"));
                remove_btn.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetColumn(remove_btn, 1);
                remove_btn.Click([this, key, entry](IInspectable const&, RoutedEventArgs const&)
                {
                    auto list = ReadTrustList(key);
                    list.erase(std::remove(list.begin(), list.end(), entry), list.end());
                    WriteTrustList(key, list);
                    RefreshPrivacyView();
                    /* le message affiché perd la confiance immédiatement */
                    ShowCurrentMessageInReader(false);
                });
                row.Children().Append(remove_btn);

                items.Append(row);
            }
        };
        add_rows(TrustedSendersKey, false);
        add_rows(TrustedDomainsKey, true);

        bool empty = items.Size() == 0;
        TrustedSendersEmpty().Visibility(empty ? Visibility::Visible : Visibility::Collapsed);
        TrustedSenderList().Visibility(empty ? Visibility::Collapsed : Visibility::Visible);
    }

    /* Option globale « Charger le contenu externe des messages ». */
    void MainWindow::OnRemoteContentModeChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        int32_t index = RemoteContentModeChoice().SelectedIndex();
        if (m_updating_privacy_view || index < 0)
            return;

        hstring mode = index == 0 ? L"never" : index == 2 ? L"always" : L"trusted";
        if (std::wstring_view{ mode } == RemoteContentMode())
            return;

        ::EvolutionWin::Settings::Instance().SetString(RemoteContentKey, mode);
        /* le message affiché suit la nouvelle option */
        ShowCurrentMessageInReader(false);
    }

    /* Met à jour la liste des comptes affichée dans la vue Paramètres. */
    void MainWindow::RefreshAccountsView()
    {
        auto items = AccountList().Items();
        items.Clear();

        if (!m_accounts)
            return;

        for (auto const& value : m_accounts)
        {
            auto account = value.GetObject();
            hstring uid = account.GetNamedString(L"uid", L"");
            hstring name = account.GetNamedString(L"name", uid);
            hstring address = account.GetNamedString(L"address", L"");
            hstring backend = account.GetNamedString(L"backend", L"");
            bool local = account.GetNamedBoolean(L"local", false);
            bool is_default = account.GetNamedBoolean(L"default", false);

            Grid row;
            row.Padding(ThicknessHelper::FromLengths(12, 10, 12, 10));
            row.ColumnDefinitions().Append(ColumnDefinition{});
            ColumnDefinition action_col;
            action_col.Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().Append(action_col);

            StackPanel info;
            info.Spacing(2);

            StackPanel title_line;
            title_line.Orientation(Orientation::Horizontal);
            title_line.Spacing(8);
            TextBlock title;
            title.Text(name);
            title.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
            title_line.Children().Append(title);
            if (is_default)
            {
                /* badge du compte par défaut */
                Border badge;
                badge.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
                badge.Padding(ThicknessHelper::FromLengths(6, 1, 6, 1));
                badge.VerticalAlignment(VerticalAlignment::Center);
                badge.Background(Application::Current().Resources().Lookup(box_value(L"AccentFillColorDefaultBrush")).as<Brush>());
                TextBlock badge_text;
                badge_text.Text(L"Par défaut");
                badge_text.FontSize(11);
                badge_text.Foreground(Application::Current().Resources().Lookup(box_value(L"TextOnAccentFillColorPrimaryBrush")).as<Brush>());
                badge.Child(badge_text);
                title_line.Children().Append(badge);
            }
            info.Children().Append(title_line);

            TextBlock subtitle;
            subtitle.Text((address.empty() ? L"Compte local" : address) +
                (backend.empty() ? hstring{} : L"  •  Protocole : " + backend));
            subtitle.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            info.Children().Append(subtitle);

            row.Children().Append(info);

            if (!local)
            {
                StackPanel actions;
                actions.Orientation(Orientation::Horizontal);
                actions.Spacing(8);
                actions.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetColumn(actions, 1);

                if (!is_default)
                {
                    Button default_btn;
                    default_btn.Content(box_value(L"Définir par défaut"));
                    ToolTipService::SetToolTip(default_btn, box_value(L"Compte utilisé pour les nouveaux messages"));
                    default_btn.Click([this, uid, name](IInspectable const&, RoutedEventArgs const&)
                    {
                        m_core->SetDefaultAccount(uid, [this, name](hstring const&, hstring const& error)
                        {
                            if (!error.empty())
                            {
                                ShowStatus(L"Compte par défaut : " + error, InfoBarSeverity::Error);
                                return;
                            }
                            ShowStatus(L"« " + name + L" » est le compte par défaut.", InfoBarSeverity::Success);
                            LoadAccounts();
                        });
                    });
                    actions.Children().Append(default_btn);
                }

                Button remove_btn;
                remove_btn.Content(box_value(L"Supprimer"));
                remove_btn.Click([this, uid, name](IInspectable const&, RoutedEventArgs const&)
                {
                    m_core->RemoveAccount(uid, [this, name](hstring const&, hstring const& error)
                    {
                        if (!error.empty())
                        {
                            ShowStatus(L"Suppression du compte : " + error, InfoBarSeverity::Error);
                            return;
                        }
                        ShowStatus(L"Compte « " + name + L" » supprimé.", InfoBarSeverity::Informational);
                        LoadAccounts();
                    });
                });
                actions.Children().Append(remove_btn);
                row.Children().Append(actions);
            }

            items.Append(row);
        }
    }

    /* Gestionnaire du bouton « Ajouter un compte ». */
    void MainWindow::OnAddAccountClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowAccountDetectionDialogAsync();
    }

    /* Première étape de l'ajout d'un compte : adresse et nom, puis détection
     * du type de compte et des serveurs par evo-core (fournisseurs connus,
     * MX, Autodiscover, autoconfig, SRV) pendant que la boîte reste ouverte.
     * « Configuration manuelle » passe directement au formulaire. */
    fire_and_forget MainWindow::ShowAccountDetectionDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Ajouter un compte"));
        dialog.PrimaryButtonText(L"Suivant");
        dialog.SecondaryButtonText(L"Configuration manuelle");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(8);
        panel.MinWidth(440);

        TextBlock intro;
        intro.Text(L"Le type de compte (Microsoft 365, Exchange, Gmail, IMAP / SMTP) et les serveurs "
                   L"sont détectés à partir de votre adresse ; vous pourrez les vérifier ensuite.");
        intro.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(intro);

        TextBox email_box;
        email_box.Header(box_value(L"Adresse électronique"));
        email_box.PlaceholderText(L"pierre@exemple.fr");
        panel.Children().Append(email_box);

        TextBox full_name_box;
        full_name_box.Header(box_value(L"Votre nom complet"));
        full_name_box.PlaceholderText(L"Pierre Dupont");
        panel.Children().Append(full_name_box);

        TextBlock invalid;
        invalid.Text(L"Saisissez une adresse électronique complète (nom@domaine).");
        invalid.Foreground(Application::Current().Resources().Lookup(box_value(L"SystemFillColorCriticalBrush")).as<Microsoft::UI::Xaml::Media::Brush>());
        invalid.Visibility(Visibility::Collapsed);
        panel.Children().Append(invalid);

        StackPanel progress;
        progress.Orientation(Orientation::Horizontal);
        progress.Spacing(8);
        progress.Visibility(Visibility::Collapsed);
        ProgressRing ring;
        ring.IsActive(true);
        ring.Width(16);
        ring.Height(16);
        progress.Children().Append(ring);
        TextBlock progress_text;
        progress_text.Text(L"Détection du type de compte…");
        progress.Children().Append(progress_text);
        panel.Children().Append(progress);

        dialog.Content(panel);

        /* détection pendant que la boîte reste ouverte (report du clic) */
        auto detected = std::make_shared<JsonObject>(nullptr);
        dialog.PrimaryButtonClick([this, email_box, full_name_box, invalid, progress, detected](
            ContentDialog const& sender, ContentDialogButtonClickEventArgs const& args)
        {
            std::wstring email{ Trim(email_box.Text()) };
            auto at = email.find(L'@');
            if (at == std::wstring::npos || at == 0 || email.find(L'.', at) == std::wstring::npos)
            {
                invalid.Visibility(Visibility::Visible);
                email_box.Focus(FocusState::Programmatic);
                args.Cancel(true);
                return;
            }

            invalid.Visibility(Visibility::Collapsed);
            progress.Visibility(Visibility::Visible);
            email_box.IsEnabled(false);
            full_name_box.IsEnabled(false);
            sender.IsPrimaryButtonEnabled(false);
            sender.IsSecondaryButtonEnabled(false);

            auto deferral = args.GetDeferral();
            m_core->DetectAccount(hstring{ email }, [deferral, detected](hstring const& json, hstring const& error)
            {
                JsonObject result;
                if (error.empty() && JsonObject::TryParse(json, result))
                    *detected = result;
                /* boîte déjà fermée entre-temps : rien à terminer */
                try
                {
                    deferral.Complete();
                }
                catch (...)
                {
                    ::EvolutionWin::LogCurrentException(L"détection du compte");
                }
            });
        });

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result == ContentDialogResult::None)
            co_return;

        JsonObject prefill = result == ContentDialogResult::Primary && *detected ? *detected : JsonObject{};
        prefill.SetNamedValue(L"email", JsonValue::CreateStringValue(hstring{ Trim(email_box.Text()) }));
        prefill.SetNamedValue(L"full_name", JsonValue::CreateStringValue(full_name_box.Text()));
        ShowAddAccountDialogAsync(prefill);
    }

    /* Libellés de la détection (evo_core_detect_account) */
    static hstring DetectedTypeLabel(hstring const& type)
    {
        if (type == L"m365") return L"Microsoft 365 / Outlook.com";
        if (type == L"exchange") return L"Exchange (serveur de l'entreprise)";
        if (type == L"google") return L"Google / Gmail";
        return L"IMAP / SMTP";
    }

    static hstring DetectedSourceLabel(hstring const& source)
    {
        if (source == L"known") return L"base des fournisseurs connus (ISPDB de Thunderbird)";
        if (source == L"mx") return L"serveurs de messagerie (MX) du domaine";
        if (source == L"autodiscover") return L"Autodiscover Exchange / Microsoft 365";
        if (source == L"autoconfig") return L"fichier autoconfig publié par le domaine";
        if (source == L"ispdb") return L"base ISPDB en ligne de Thunderbird";
        if (source == L"srv") return L"enregistrements DNS SRV du domaine";
        return source;
    }

    /* Formulaire d'ajout d'un compte, prérempli par la détection : type
     * proposé (modifiable) et paramètres à confirmer ; crée ensuite les
     * sources EDS correspondantes via evo-core.
     * Paramètres :
     *   prefill : {"email", "full_name"} et le résultat de la détection
     *             (evo_core_detect_account), vide en configuration manuelle */
    fire_and_forget MainWindow::ShowAddAccountDialogAsync(JsonObject prefill)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Ajouter un compte"));
        dialog.PrimaryButtonText(L"Ajouter");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(8);
        panel.MinWidth(440);

        hstring detected_type = prefill.GetNamedString(L"type", L"");
        if (!detected_type.empty())
        {
            hstring source = prefill.GetNamedString(L"source", L"");
            hstring provider = prefill.GetNamedString(L"provider", L"");
            InfoBar detection;
            detection.IsOpen(true);
            detection.IsClosable(false);
            if (source == L"guess")
            {
                detection.Severity(InfoBarSeverity::Warning);
                detection.Title(L"Aucune configuration trouvée");
                detection.Message(L"Ce domaine ne publie pas sa configuration : les serveurs proposés "
                                  L"sont supposés. Vérifiez-les auprès de votre fournisseur.");
            }
            else
            {
                detection.Severity(InfoBarSeverity::Success);
                detection.Title(L"Type détecté : " + DetectedTypeLabel(detected_type));
                detection.Message((provider.empty() ? hstring{} : provider + L" — ") + L"d'après : " +
                                  DetectedSourceLabel(source) + L". Vérifiez les paramètres proposés ou "
                                  L"choisissez un autre type de compte.");
            }
            panel.Children().Append(detection);
        }

        ComboBox provider_box;
        provider_box.Header(box_value(L"Type de compte"));
        provider_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        provider_box.Items().Append(box_value(L"IMAP / SMTP standard (mot de passe)"));
        provider_box.Items().Append(box_value(L"Microsoft 365 / Outlook.com (OAuth2 XOAUTH2)"));
        provider_box.Items().Append(box_value(L"Google / Gmail (OAuth2 XOAUTH2)"));
        /* index 3 : compte evolution-ews (Microsoft Graph), sans IMAP/SMTP */
        provider_box.Items().Append(box_value(L"Microsoft 365 / Exchange Online (courrier, agenda, contacts)"));
        /* index 4 : compte evolution-ews (EWS) d'un serveur Exchange de l'entreprise */
        provider_box.Items().Append(box_value(L"Exchange de l'entreprise (EWS, mot de passe)"));
        provider_box.SelectedIndex(0);
        panel.Children().Append(provider_box);

        TextBox display_name_box;
        display_name_box.Header(box_value(L"Nom du compte"));
        display_name_box.PlaceholderText(L"Personnel, Travail…");
        panel.Children().Append(display_name_box);

        TextBox full_name_box;
        full_name_box.Header(box_value(L"Votre nom complet"));
        full_name_box.PlaceholderText(L"Pierre Dupont");
        full_name_box.Text(prefill.GetNamedString(L"full_name", L""));
        panel.Children().Append(full_name_box);

        TextBox email_box;
        email_box.Header(box_value(L"Adresse électronique"));
        email_box.PlaceholderText(L"pierre@exemple.fr");
        email_box.Text(prefill.GetNamedString(L"email", L""));
        panel.Children().Append(email_box);

        TextBox user_box;
        user_box.Header(box_value(L"Nom d'utilisateur"));
        user_box.PlaceholderText(L"Adresse électronique par défaut");
        user_box.Text(prefill.GetNamedString(L"user", L""));
        panel.Children().Append(user_box);

        TextBox ews_url_box;
        ews_url_box.Header(box_value(L"Adresse du serveur EWS"));
        ews_url_box.PlaceholderText(L"https://mail.exemple.fr/EWS/Exchange.asmx");
        ews_url_box.Text(prefill.GetNamedString(L"ews_url", L""));
        ews_url_box.Visibility(Visibility::Collapsed);
        panel.Children().Append(ews_url_box);

        TextBox imap_host_box;
        imap_host_box.Header(box_value(L"Serveur entrant (IMAP)"));
        imap_host_box.PlaceholderText(L"imap.exemple.fr");
        panel.Children().Append(imap_host_box);

        TextBox imap_port_box;
        imap_port_box.Header(box_value(L"Port IMAP (993 = TLS, 143 = STARTTLS)"));
        imap_port_box.Text(L"993");
        panel.Children().Append(imap_port_box);

        TextBox smtp_host_box;
        smtp_host_box.Header(box_value(L"Serveur sortant (SMTP)"));
        smtp_host_box.PlaceholderText(L"smtp.exemple.fr");
        panel.Children().Append(smtp_host_box);

        TextBox smtp_port_box;
        smtp_port_box.Header(box_value(L"Port SMTP (465 = TLS, 587 = STARTTLS)"));
        smtp_port_box.Text(L"465");
        panel.Children().Append(smtp_port_box);

        StackPanel oauth_panel;
        oauth_panel.Spacing(8);
        oauth_panel.Visibility(Visibility::Collapsed);

        wchar_t env_cid[256]{};
        wchar_t env_tid[256]{};
        GetEnvironmentVariableW(L"EVO_OAUTH2_CLIENT_ID", env_cid, 256);
        GetEnvironmentVariableW(L"EVO_OAUTH2_TENANT_ID", env_tid, 256);

        TextBox client_id_box;
        client_id_box.Header(box_value(L"Client ID OAuth2 (Application ID)"));
        client_id_box.PlaceholderText(L"Identifiant d'application Entra ID ou Google Cloud");
        client_id_box.Text(env_cid);
        oauth_panel.Children().Append(client_id_box);

        TextBox tenant_id_box;
        tenant_id_box.Header(box_value(L"Tenant ID Microsoft 365 (ex. common ou ID d'organisation)"));
        tenant_id_box.Text(env_tid[0] ? env_tid : L"common");
        oauth_panel.Children().Append(tenant_id_box);

        panel.Children().Append(oauth_panel);

        PasswordBox password_box;
        password_box.Header(box_value(L"Mot de passe (ou jeton d'accès OAuth2 si déjà obtenu)"));
        panel.Children().Append(password_box);

        provider_box.SelectionChanged([imap_host_box, imap_port_box, smtp_host_box, smtp_port_box, oauth_panel, tenant_id_box, client_id_box,
            password_box, user_box, ews_url_box](IInspectable const& sender, SelectionChangedEventArgs const&)
        {
            int idx = sender.as<ComboBox>().SelectedIndex();

            /* Microsoft Graph : ni serveurs ni mot de passe, connexion OAuth2 ensuite ;
             * Exchange : adresse EWS et mot de passe */
            auto server_visibility = idx == 3 || idx == 4 ? Visibility::Collapsed : Visibility::Visible;
            imap_host_box.Visibility(server_visibility);
            imap_port_box.Visibility(server_visibility);
            smtp_host_box.Visibility(server_visibility);
            smtp_port_box.Visibility(server_visibility);
            password_box.Visibility(idx == 3 ? Visibility::Collapsed : Visibility::Visible);
            user_box.Visibility(idx == 3 ? Visibility::Collapsed : Visibility::Visible);
            ews_url_box.Visibility(idx == 4 ? Visibility::Visible : Visibility::Collapsed);
            client_id_box.PlaceholderText(idx == 3
                ? L"Facultatif : application d'Evolution par défaut"
                : L"Identifiant d'application Entra ID ou Google Cloud");

            if (idx == 3)
            {
                oauth_panel.Visibility(Visibility::Visible);
                tenant_id_box.Visibility(Visibility::Visible);
            }
            else if (idx == 1)
            {
                imap_host_box.Text(L"outlook.office365.com");
                imap_port_box.Text(L"993");
                smtp_host_box.Text(L"smtp.office365.com");
                smtp_port_box.Text(L"587");
                oauth_panel.Visibility(Visibility::Visible);
                tenant_id_box.Visibility(Visibility::Visible);
                password_box.PlaceholderText(L"Laisser vide pour ouvrir la fenêtre OAuth2 WebView2");
            }
            else if (idx == 2)
            {
                imap_host_box.Text(L"imap.gmail.com");
                imap_port_box.Text(L"993");
                smtp_host_box.Text(L"smtp.gmail.com");
                smtp_port_box.Text(L"465");
                oauth_panel.Visibility(Visibility::Visible);
                tenant_id_box.Visibility(Visibility::Collapsed);
                password_box.PlaceholderText(L"Laisser vide pour ouvrir la fenêtre OAuth2 WebView2");
            }
            else
            {
                oauth_panel.Visibility(Visibility::Collapsed);
                password_box.PlaceholderText(idx == 4 ? L"Mot de passe du compte Windows / Exchange" : L"Mot de passe du compte");
            }
        });

        /* type et serveurs détectés */
        provider_box.SelectedIndex(detected_type == L"m365" ? 3
            : detected_type == L"google" ? 2
            : detected_type == L"exchange" ? 4 : 0);
        if (!prefill.GetNamedString(L"imap_host", L"").empty())
        {
            imap_host_box.Text(prefill.GetNamedString(L"imap_host"));
            imap_port_box.Text(to_hstring(static_cast<int>(prefill.GetNamedNumber(L"imap_port", 993))));
            smtp_host_box.Text(prefill.GetNamedString(L"smtp_host", L""));
            smtp_port_box.Text(to_hstring(static_cast<int>(prefill.GetNamedNumber(L"smtp_port", 465))));
        }

        ScrollViewer scroll;
        scroll.Content(panel);
        scroll.MaxHeight(540);
        dialog.Content(scroll);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result != ContentDialogResult::Primary)
            co_return;

        int provider_idx = provider_box.SelectedIndex();
        hstring email{ Trim(email_box.Text()) };
        hstring user = user_box.Text().empty() ? email : hstring{ Trim(user_box.Text()) };
        hstring imap_host{ Trim(imap_host_box.Text()) };
        hstring smtp_host{ Trim(smtp_host_box.Text()) };
        hstring account_name = display_name_box.Text().empty() ? email : display_name_box.Text();

        if (provider_idx == 3)
        {
            if (email.empty())
            {
                ShowStatus(L"Veuillez renseigner l'adresse électronique du compte Microsoft 365.", InfoBarSeverity::Warning);
                co_return;
            }

            JsonObject m365;
            m365.SetNamedValue(L"email", JsonValue::CreateStringValue(email));
            m365.SetNamedValue(L"display_name", JsonValue::CreateStringValue(account_name));
            m365.SetNamedValue(L"full_name", JsonValue::CreateStringValue(
                full_name_box.Text().empty() ? email : full_name_box.Text()));
            m365.SetNamedValue(L"client_id", JsonValue::CreateStringValue(client_id_box.Text()));
            /* "common" est la valeur par défaut d'evolution-ews */
            m365.SetNamedValue(L"tenant", JsonValue::CreateStringValue(
                tenant_id_box.Text() == L"common" ? L"" : tenant_id_box.Text()));

            m_core->AddMicrosoft365Account(m365.Stringify(), [this, account_name](hstring const& json, hstring const& error)
            {
                JsonObject created;
                if (!error.empty() || !JsonObject::TryParse(json, created))
                {
                    ShowStatus(L"Ajout du compte : " + error, InfoBarSeverity::Error);
                    return;
                }
                LoadAccounts();
                SignInOAuth2Async(created.GetNamedString(L"collection_uid", L""), account_name);
            });
            co_return;
        }

        if (provider_idx == 4)
        {
            hstring ews_url{ Trim(ews_url_box.Text()) };
            if (email.empty() || ews_url.empty())
            {
                ShowStatus(L"Veuillez renseigner l'adresse électronique et l'adresse du serveur EWS.", InfoBarSeverity::Warning);
                co_return;
            }

            JsonObject ews;
            ews.SetNamedValue(L"backend", JsonValue::CreateStringValue(L"ews"));
            ews.SetNamedValue(L"email", JsonValue::CreateStringValue(email));
            ews.SetNamedValue(L"display_name", JsonValue::CreateStringValue(account_name));
            ews.SetNamedValue(L"full_name", JsonValue::CreateStringValue(
                full_name_box.Text().empty() ? email : full_name_box.Text()));
            ews.SetNamedValue(L"hosturl", JsonValue::CreateStringValue(ews_url));
            ews.SetNamedValue(L"user", JsonValue::CreateStringValue(user));
            ews.SetNamedValue(L"password", JsonValue::CreateStringValue(password_box.Password()));

            m_core->AddMicrosoft365Account(ews.Stringify(), [this, account_name](hstring const&, hstring const& error)
            {
                if (!error.empty())
                {
                    ShowStatus(L"Ajout du compte : " + error, InfoBarSeverity::Error);
                    return;
                }
                ShowStatus(L"Compte « " + account_name + L" » ajouté.", InfoBarSeverity::Success);
                LoadAccounts();
            });
            co_return;
        }

        if (email.empty() || imap_host.empty() || smtp_host.empty())
        {
            ShowStatus(L"Veuillez renseigner l'adresse électronique et les serveurs IMAP / SMTP.", InfoBarSeverity::Warning);
            co_return;
        }

        hstring secret = password_box.Password();
        if ((provider_idx == 1 || provider_idx == 2) && secret.empty())
        {
            if (client_id_box.Text().empty())
            {
                ShowStatus(L"Veuillez renseigner le Client ID OAuth2 (ou saisir un jeton d'accès).", InfoBarSeverity::Warning);
                co_return;
            }
            secret = co_await StartOAuth2SignInAsync(provider_idx, client_id_box.Text(), tenant_id_box.Text(), email);
            if (secret.empty())
                co_return;
        }

        int imap_port = _wtoi(imap_port_box.Text().c_str());
        if (imap_port <= 0) imap_port = 993;
        int smtp_port = _wtoi(smtp_port_box.Text().c_str());
        if (smtp_port <= 0) smtp_port = 465;

        /* chiffrement détecté tant que le port proposé n'a pas changé, sinon
         * déduit du port */
        auto security = [&prefill](int port, wchar_t const* port_key, wchar_t const* security_key, int starttls_port) -> hstring
        {
            if (prefill.HasKey(port_key) && static_cast<int>(prefill.GetNamedNumber(port_key)) == port)
                return prefill.GetNamedString(security_key, L"tls");
            return port == starttls_port ? L"starttls" : L"tls";
        };

        JsonObject payload;
        payload.SetNamedValue(L"display_name", JsonValue::CreateStringValue(account_name));
        payload.SetNamedValue(L"full_name", JsonValue::CreateStringValue(
            full_name_box.Text().empty() ? email : full_name_box.Text()));
        payload.SetNamedValue(L"email", JsonValue::CreateStringValue(email));
        payload.SetNamedValue(L"user", JsonValue::CreateStringValue(user));
        payload.SetNamedValue(L"imap_host", JsonValue::CreateStringValue(imap_host));
        payload.SetNamedValue(L"imap_port", JsonValue::CreateNumberValue(imap_port));
        payload.SetNamedValue(L"imap_security", JsonValue::CreateStringValue(
            security(imap_port, L"imap_port", L"imap_security", 143)));
        payload.SetNamedValue(L"smtp_host", JsonValue::CreateStringValue(smtp_host));
        payload.SetNamedValue(L"smtp_port", JsonValue::CreateNumberValue(smtp_port));
        payload.SetNamedValue(L"smtp_security", JsonValue::CreateStringValue(
            security(smtp_port, L"smtp_port", L"smtp_security", 587)));
        if (provider_idx == 1 || provider_idx == 2)
            payload.SetNamedValue(L"auth_mechanism", JsonValue::CreateStringValue(L"XOAUTH2"));
        payload.SetNamedValue(L"password", JsonValue::CreateStringValue(secret));

        m_core->AddAccount(payload.Stringify(), [this, email](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Ajout du compte : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Compte « " + email + L" » ajouté.", InfoBarSeverity::Success);
            LoadAccounts();
        });
    }

    /* -------------------------------------------------------------------- */
    /* Étape 3 : Calendrier (ECalClient) et Contacts (EBookClient)          */
    /* -------------------------------------------------------------------- */

    /* Charge et affiche les événements des calendriers EDS autour de la date
     * sélectionnée (ou les 30 prochains jours par défaut). */
    void MainWindow::LoadEvents()
    {
        int64_t start_unix = m_selected_day_start;
        int64_t end_unix = 0;

        if (start_unix <= 0)
        {
            start_unix = static_cast<int64_t>(std::time(nullptr)) - 86400;
            end_unix = start_unix + 30LL * 86400;
            CalendarHeader().Text(L"Événements (30 prochains jours)");
        }
        else
        {
            end_unix = start_unix + 86400;
            CalendarHeader().Text(L"Événements du " + FormatDate(static_cast<double>(start_unix)));
        }

        m_core->ListEvents(start_unix, end_unix, [this](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Calendrier : " + error, InfoBarSeverity::Error);
                return;
            }

            auto items = EventList().Items();
            items.Clear();

            auto events = JsonArray::Parse(json);
            for (auto const& value : events)
            {
                auto ev = value.GetObject();
                hstring uid = ev.GetNamedString(L"uid", L"");
                hstring calendar_uid = ev.GetNamedString(L"calendar_uid", L"");
                hstring summary = ev.GetNamedString(L"summary", L"(Sans titre)");
                hstring location = ev.GetNamedString(L"location", L"");
                hstring description = ev.GetNamedString(L"description", L"");
                double start_t = ev.GetNamedNumber(L"start", 0);
                double end_t = ev.GetNamedNumber(L"end", 0);

                Grid row;
                row.Padding(ThicknessHelper::FromLengths(12, 10, 12, 10));
                row.ColumnDefinitions().Append(ColumnDefinition{});
                ColumnDefinition del_col;
                del_col.Width(GridLengthHelper::Auto());
                row.ColumnDefinitions().Append(del_col);

                StackPanel details;
                details.Spacing(2);

                TextBlock title;
                title.Text(summary);
                title.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
                details.Children().Append(title);

                hstring time_str = FormatDate(start_t) + (end_t > start_t ? L" → " + FormatDate(end_t) : hstring{});
                if (!location.empty())
                    time_str = time_str + L"  •  Lieu : " + location;

                TextBlock sub;
                sub.Text(time_str);
                sub.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                details.Children().Append(sub);

                if (!description.empty())
                {
                    TextBlock desc;
                    desc.Text(description);
                    desc.TextWrapping(TextWrapping::Wrap);
                    details.Children().Append(desc);
                }

                row.Children().Append(details);

                Button del_btn;
                FontIcon del_icon;
                del_icon.Glyph(L"\xE74D");
                del_icon.FontSize(14);
                del_btn.Content(del_icon);
                del_btn.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetColumn(del_btn, 1);
                ToolTipService::SetToolTip(del_btn, box_value(L"Supprimer l'événement"));
                del_btn.Click([this, calendar_uid, uid](IInspectable const&, RoutedEventArgs const&)
                {
                    m_core->DeleteEvent(calendar_uid, uid, [this](hstring const&, hstring const& err)
                    {
                        if (!err.empty())
                        {
                            ShowStatus(L"Suppression de l'événement : " + err, InfoBarSeverity::Error);
                            return;
                        }
                        LoadEvents();
                    });
                });
                row.Children().Append(del_btn);

                items.Append(row);
            }

            if (events.Size() == 0)
            {
                TextBlock empty;
                empty.Text(L"Aucun événement sur cette période.");
                empty.Padding(ThicknessHelper::FromLengths(12, 12, 12, 12));
                items.Append(empty);
            }
        });
    }

    /* Gestionnaire de sélection d'une date sur le calendrier mensuel. */
    void MainWindow::OnCalendarDateSelected(CalendarView const& sender, CalendarViewSelectedDatesChangedEventArgs const&)
    {
        auto dates = sender.SelectedDates();
        if (dates.Size() == 0)
        {
            m_selected_day_start = 0;
        }
        else
        {
            time_t raw = winrt::clock::to_time_t(dates.GetAt(0));
            struct tm local_tm{};
            localtime_s(&local_tm, &raw);
            local_tm.tm_hour = 0;
            local_tm.tm_min = 0;
            local_tm.tm_sec = 0;
            m_selected_day_start = static_cast<int64_t>(mktime(&local_tm));
        }
        LoadEvents();
    }

    /* Gestionnaire du bouton d'actualisation du calendrier. */
    void MainWindow::OnRefreshCalendarClick(IInspectable const&, RoutedEventArgs const&)
    {
        m_selected_day_start = 0;
        MonthCalendar().SelectedDates().Clear();
        LoadEvents();
    }

    /* Gestionnaire du bouton « Nouvel événement ». */
    void MainWindow::OnNewEventClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowNewEventDialogAsync();
    }

    /* Affiche une boîte de dialogue permettant de créer un nouvel événement
     * dans le calendrier EDS et l'enregistre via evo-core. */
    fire_and_forget MainWindow::ShowNewEventDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Nouvel événement"));
        dialog.PrimaryButtonText(L"Enregistrer");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(380);

        TextBox summary_box;
        summary_box.Header(box_value(L"Titre"));
        summary_box.PlaceholderText(L"Réunion, Rendez-vous…");
        panel.Children().Append(summary_box);

        TextBox location_box;
        location_box.Header(box_value(L"Lieu"));
        panel.Children().Append(location_box);

        CalendarDatePicker date_picker;
        date_picker.Header(box_value(L"Date"));
        date_picker.Date(m_selected_day_start > 0
            ? winrt::clock::from_time_t(static_cast<time_t>(m_selected_day_start + 3600))
            : winrt::clock::now());
        panel.Children().Append(date_picker);

        TimePicker start_picker;
        start_picker.Header(box_value(L"Heure de début"));
        start_picker.Time( std::chrono::hours(9) );
        panel.Children().Append(start_picker);

        TimePicker end_picker;
        end_picker.Header(box_value(L"Heure de fin"));
        end_picker.Time( std::chrono::hours(10) );
        panel.Children().Append(end_picker);

        TextBox desc_box;
        desc_box.Header(box_value(L"Description"));
        desc_box.AcceptsReturn(true);
        desc_box.Height(80);
        panel.Children().Append(desc_box);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result != ContentDialogResult::Primary || summary_box.Text().empty())
            co_return;

        auto picked_date = date_picker.Date() ? date_picker.Date().Value() : winrt::clock::now();
        time_t raw_date = winrt::clock::to_time_t(picked_date);
        struct tm day_tm{};
        localtime_s(&day_tm, &raw_date);
        day_tm.tm_hour = 0;
        day_tm.tm_min = 0;
        day_tm.tm_sec = 0;
        int64_t day_base = static_cast<int64_t>(mktime(&day_tm));

        int64_t start_secs = std::chrono::duration_cast<std::chrono::seconds>(start_picker.Time()).count();
        int64_t end_secs = std::chrono::duration_cast<std::chrono::seconds>(end_picker.Time()).count();
        if (end_secs <= start_secs)
            end_secs = start_secs + 3600;

        JsonObject payload;
        payload.SetNamedValue(L"summary", JsonValue::CreateStringValue(summary_box.Text()));
        payload.SetNamedValue(L"location", JsonValue::CreateStringValue(location_box.Text()));
        payload.SetNamedValue(L"description", JsonValue::CreateStringValue(desc_box.Text()));
        payload.SetNamedValue(L"start", JsonValue::CreateNumberValue(static_cast<double>(day_base + start_secs)));
        payload.SetNamedValue(L"end", JsonValue::CreateNumberValue(static_cast<double>(day_base + end_secs)));

        m_core->CreateEvent(payload.Stringify(), [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Création de l'événement : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Événement enregistré.", InfoBarSeverity::Success);
            LoadEvents();
        });
    }

    /* Remplace les caractères interdits dans un nom de fichier Windows.
     * Paramètres :
     *   name     : nom proposé (nom d'agenda, de carnet d'adresses…)
     *   fallback : nom utilisé si le résultat est vide */
    static hstring SanitizeFileName(hstring const& name, hstring const& fallback)
    {
        std::wstring clean{ name };
        for (auto& c : clean)
            if (wcschr(L"\\/:*?\"<>|", c) || c < 32)
                c = L'_';
        return clean.empty() ? fallback : hstring{ clean };
    }

    /* Gestionnaire du bouton « Exporter » du calendrier : liste les agendas et
     * listes de tâches, puis propose l'exportation de l'un d'eux. */
    void MainWindow::OnExportCalendarClick(IInspectable const&, RoutedEventArgs const&)
    {
        m_core->ListSources(L"calendar,tasks", [this](hstring const& json, hstring const& error)
        {
            JsonArray sources;
            if (!error.empty() || !JsonArray::TryParse(json, sources))
            {
                ShowStatus(L"Exportation : " + error, InfoBarSeverity::Error);
                return;
            }
            if (sources.Size() == 0)
            {
                ShowStatus(L"Aucun agenda ni liste de tâches à exporter.", InfoBarSeverity::Warning);
                return;
            }
            ExportCalendarAsync(sources);
        });
    }

    /* Demande l'agenda ou la liste de tâches à exporter, puis le fichier .ics
     * de destination, et lance l'exportation iCalendar par evo-core.
     * Paramètres :
     *   sources : tableau JSON [{"uid", "name", "kind"}] des agendas et listes de tâches */
    fire_and_forget MainWindow::ExportCalendarAsync(JsonArray sources)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Exporter au format iCalendar"));
        dialog.PrimaryButtonText(L"Exporter");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(380);

        ComboBox source_box;
        source_box.Header(box_value(L"Agenda ou liste de tâches"));
        source_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        for (auto const& value : sources)
        {
            auto source = value.GetObject();
            hstring label = source.GetNamedString(L"kind", L"") == L"tasks" ? L"Tâches" : L"Agenda";
            source_box.Items().Append(box_value(label + L" : " + source.GetNamedString(L"name", L"")));
        }
        source_box.SelectedIndex(0);
        panel.Children().Append(source_box);

        TextBlock hint;
        hint.Text(L"Tous les événements ou toutes les tâches sont enregistrés dans un fichier .ics, avec leurs fuseaux horaires.");
        hint.TextWrapping(TextWrapping::Wrap);
        hint.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
        panel.Children().Append(hint);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        int index = source_box.SelectedIndex();
        if (result != ContentDialogResult::Primary || index < 0)
            co_return;

        auto source = sources.GetObjectAt(static_cast<uint32_t>(index));
        hstring uid = source.GetNamedString(L"uid", L"");
        hstring name = source.GetNamedString(L"name", L"");
        bool tasks = source.GetNamedString(L"kind", L"") == L"tasks";

        Windows::Storage::Pickers::FileSavePicker picker;
        InitializePicker(picker);
        picker.SuggestedFileName(SanitizeFileName(name, tasks ? L"taches" : L"agenda"));
        picker.FileTypeChoices().Insert(L"Calendrier iCalendar", single_threaded_vector<hstring>({ L".ics" }));

        auto file = co_await picker.PickSaveFileAsync();
        if (!file)
            co_return;
        hstring dest = file.Path();

        JsonObject request;
        request.SetNamedValue(L"source_uid", JsonValue::CreateStringValue(uid));
        request.SetNamedValue(L"path", JsonValue::CreateStringValue(dest));

        ShowStatus(L"Exportation de « " + name + L" »…", InfoBarSeverity::Informational);
        m_core->ExportCalendar(request.Stringify(), [this, dest, tasks](hstring const& json, hstring const& error)
        {
            JsonObject answer;
            if (!error.empty() || !JsonObject::TryParse(json, answer))
            {
                ShowStatus(L"Exportation : " + error, InfoBarSeverity::Error);
                return;
            }
            auto count = static_cast<uint32_t>(answer.GetNamedNumber(L"count", 0));
            ShowStatus(hstring{ std::to_wstring(count) + (tasks ? L" tâche(s) exportée(s)" : L" événement(s) exporté(s)") +
                L" dans « " + std::wstring{ dest } + L" »." }, InfoBarSeverity::Success);
        });
    }

    /* Charge la liste des contacts depuis les carnets d'adresses EDS.
     * Paramètres :
     *   query : texte de filtrage optionnel sur le nom ou l'adresse e-mail */
    void MainWindow::LoadContacts(hstring const& query)
    {
        m_core->ListContacts(query, [this](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Contacts : " + error, InfoBarSeverity::Error);
                return;
            }

            auto items = ContactList().Items();
            items.Clear();
            m_selected_contact = nullptr;
            ContactDetailPane().Visibility(Visibility::Collapsed);
            ContactPlaceholder().Visibility(Visibility::Visible);

            auto contacts = JsonArray::Parse(json);
            for (auto const& value : contacts)
            {
                auto contact = value.GetObject();
                hstring full_name = contact.GetNamedString(L"full_name", L"(Sans nom)");
                hstring email = contact.GetNamedString(L"email", L"");

                StackPanel row;
                row.Padding(ThicknessHelper::FromLengths(8, 6, 8, 6));
                row.Tag(box_value(contact.Stringify()));

                TextBlock name_text;
                name_text.Text(full_name);
                name_text.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
                row.Children().Append(name_text);

                if (!email.empty())
                {
                    TextBlock email_text;
                    email_text.Text(email);
                    email_text.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                    row.Children().Append(email_text);
                }

                items.Append(row);
            }
        });
    }

    /* Gestionnaire de modification du champ de recherche de contacts. */
    void MainWindow::OnContactSearchChanged(AutoSuggestBox const& sender, AutoSuggestBoxTextChangedEventArgs const& args)
    {
        if (args.Reason() == AutoSuggestionBoxTextChangeReason::UserInput)
            LoadContacts(sender.Text());
    }

    /* Gestionnaire de validation de la recherche de contacts. */
    void MainWindow::OnContactSearchSubmitted(AutoSuggestBox const&, AutoSuggestBoxQuerySubmittedEventArgs const& args)
    {
        LoadContacts(args.QueryText());
    }

    /* Gestionnaire de sélection d'un contact dans la liste : affiche sa fiche détaillée. */
    void MainWindow::OnContactSelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        auto panel = ContactList().SelectedItem().try_as<StackPanel>();
        if (!panel)
        {
            m_selected_contact = nullptr;
            ContactDetailPane().Visibility(Visibility::Collapsed);
            ContactPlaceholder().Visibility(Visibility::Visible);
            return;
        }

        hstring raw = unbox_value_or<hstring>(panel.Tag(), L"");
        JsonObject contact;
        if (!JsonObject::TryParse(raw, contact))
            return;

        m_selected_contact = contact;
        ContactNameText().Text(contact.GetNamedString(L"full_name", L"(Sans nom)"));
        ContactEmailText().Text(contact.GetNamedString(L"email", L"—"));
        ContactPhoneText().Text(contact.GetNamedString(L"phone", L"—"));
        ContactOrgText().Text(contact.GetNamedString(L"org", L"—"));
        ContactNotesText().Text(contact.GetNamedString(L"note", L""));

        ContactPlaceholder().Visibility(Visibility::Collapsed);
        ContactDetailPane().Visibility(Visibility::Visible);
    }

    /* Gestionnaire du bouton « Nouveau contact ». */
    void MainWindow::OnNewContactClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowNewContactDialogAsync();
    }

    /* Affiche une boîte de dialogue permettant d'ajouter un nouveau contact
     * dans le carnet d'adresses EDS et l'enregistre via evo-core. */
    fire_and_forget MainWindow::ShowNewContactDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Nouveau contact"));
        dialog.PrimaryButtonText(L"Enregistrer");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(380);

        TextBox name_box;
        name_box.Header(box_value(L"Nom complet"));
        name_box.PlaceholderText(L"Jean Martin");
        panel.Children().Append(name_box);

        TextBox email_box;
        email_box.Header(box_value(L"Adresse électronique"));
        email_box.PlaceholderText(L"jean.martin@exemple.fr");
        panel.Children().Append(email_box);

        TextBox phone_box;
        phone_box.Header(box_value(L"Téléphone"));
        panel.Children().Append(phone_box);

        TextBox org_box;
        org_box.Header(box_value(L"Organisation"));
        panel.Children().Append(org_box);

        TextBox note_box;
        note_box.Header(box_value(L"Notes"));
        note_box.AcceptsReturn(true);
        note_box.Height(72);
        panel.Children().Append(note_box);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result != ContentDialogResult::Primary || name_box.Text().empty())
            co_return;

        JsonObject payload;
        payload.SetNamedValue(L"full_name", JsonValue::CreateStringValue(name_box.Text()));
        payload.SetNamedValue(L"email", JsonValue::CreateStringValue(email_box.Text()));
        payload.SetNamedValue(L"phone", JsonValue::CreateStringValue(phone_box.Text()));
        payload.SetNamedValue(L"org", JsonValue::CreateStringValue(org_box.Text()));
        payload.SetNamedValue(L"note", JsonValue::CreateStringValue(note_box.Text()));

        m_core->CreateContact(payload.Stringify(), [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Création du contact : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Contact enregistré.", InfoBarSeverity::Success);
            LoadContacts(ContactSearchBox().Text());
        });
    }

    /* Gestionnaire du bouton « Exporter » des contacts : liste les carnets
     * d'adresses, puis propose l'exportation vCard. */
    void MainWindow::OnExportContactsClick(IInspectable const&, RoutedEventArgs const&)
    {
        m_core->ListSources(L"contacts", [this](hstring const& json, hstring const& error)
        {
            JsonArray books;
            if (!error.empty() || !JsonArray::TryParse(json, books))
            {
                ShowStatus(L"Exportation : " + error, InfoBarSeverity::Error);
                return;
            }
            if (books.Size() == 0)
            {
                ShowStatus(L"Aucun carnet d'adresses à exporter.", InfoBarSeverity::Warning);
                return;
            }
            ExportContactsAsync(books);
        });
    }

    /* Demande le carnet d'adresses et le format (un seul fichier .vcf ou un
     * fichier par contact), puis la destination, et lance l'exportation vCard.
     * Paramètres :
     *   books : tableau JSON [{"uid", "name", "kind"}] des carnets d'adresses */
    fire_and_forget MainWindow::ExportContactsAsync(JsonArray books)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Exporter les contacts"));
        dialog.PrimaryButtonText(L"Exporter");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(380);

        ComboBox book_box;
        book_box.Header(box_value(L"Carnet d'adresses"));
        book_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        book_box.Items().Append(box_value(L"Tous les carnets d'adresses"));
        for (auto const& value : books)
            book_box.Items().Append(box_value(value.GetObject().GetNamedString(L"name", L"")));
        book_box.SelectedIndex(0);
        panel.Children().Append(book_box);

        RadioButtons format;
        format.Header(box_value(L"Format vCard"));
        format.Items().Append(box_value(L"Un seul fichier .vcf"));
        format.Items().Append(box_value(L"Un fichier .vcf par contact, dans un dossier"));
        format.SelectedIndex(0);
        panel.Children().Append(format);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result != ContentDialogResult::Primary)
            co_return;

        int index = book_box.SelectedIndex();
        hstring book_uid;
        hstring name = L"Contacts";
        if (index > 0)
        {
            auto book = books.GetObjectAt(static_cast<uint32_t>(index - 1));
            book_uid = book.GetNamedString(L"uid", L"");
            name = book.GetNamedString(L"name", L"Contacts");
        }
        bool per_contact = format.SelectedIndex() == 1;

        hstring dest;
        if (per_contact)
        {
            Windows::Storage::Pickers::FolderPicker picker;
            InitializePicker(picker);
            picker.FileTypeFilter().Append(L"*");

            auto folder = co_await picker.PickSingleFolderAsync();
            if (!folder)
                co_return;
            dest = folder.Path();
        }
        else
        {
            Windows::Storage::Pickers::FileSavePicker picker;
            InitializePicker(picker);
            picker.SuggestedFileName(SanitizeFileName(name, L"Contacts"));
            picker.FileTypeChoices().Insert(L"Carte de visite vCard", single_threaded_vector<hstring>({ L".vcf" }));

            auto file = co_await picker.PickSaveFileAsync();
            if (!file)
                co_return;
            dest = file.Path();
        }

        JsonObject request;
        request.SetNamedValue(L"book_uid", JsonValue::CreateStringValue(book_uid));
        request.SetNamedValue(L"path", JsonValue::CreateStringValue(dest));
        request.SetNamedValue(L"per_contact", JsonValue::CreateBooleanValue(per_contact));

        ShowStatus(L"Exportation des contacts…", InfoBarSeverity::Informational);
        m_core->ExportContacts(request.Stringify(), [this, dest](hstring const& json, hstring const& error)
        {
            JsonObject answer;
            if (!error.empty() || !JsonObject::TryParse(json, answer))
            {
                ShowStatus(L"Exportation des contacts : " + error, InfoBarSeverity::Error);
                return;
            }
            auto count = static_cast<uint32_t>(answer.GetNamedNumber(L"count", 0));
            ShowStatus(hstring{ std::to_wstring(count) + L" contact(s) exporté(s) dans « " + std::wstring{ dest } + L" »." },
                InfoBarSeverity::Success);
        });
    }

    /* Gestionnaire du bouton « Écrire un message » sur la fiche d'un contact :
     * ouvre la fenêtre de rédaction pré-remplie avec l'adresse du contact. */
    void MainWindow::OnComposeToContactClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_selected_contact)
            return;

        hstring email = m_selected_contact.GetNamedString(L"email", L"");
        hstring name = m_selected_contact.GetNamedString(L"full_name", L"");
        if (email.empty())
        {
            ShowStatus(L"Ce contact n'a pas d'adresse électronique.", InfoBarSeverity::Warning);
            return;
        }

        ::EvolutionWin::ComposeData data;
        data.account = m_account;
        data.to = name.empty() ? email : name + L" <" + email + L">";
        OpenCompose(data);
    }

    /* Gestionnaire du bouton « Supprimer le contact » sur la fiche d'un contact. */
    void MainWindow::OnDeleteContactClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_selected_contact)
            return;

        hstring book_uid = m_selected_contact.GetNamedString(L"book_uid", L"");
        hstring uid = m_selected_contact.GetNamedString(L"uid", L"");
        if (book_uid.empty() || uid.empty())
            return;

        m_core->DeleteContact(book_uid, uid, [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Suppression du contact : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Contact supprimé.", InfoBarSeverity::Informational);
            LoadContacts(ContactSearchBox().Text());
        });
    }

    /* -------------------------------------------------------------------- */
    /* Tâches (ECalClient, VTODO)                                           */
    /* -------------------------------------------------------------------- */

    /* clés des préférences de la vue Tâches (voir Settings.h) */
    static constexpr wchar_t TasksShowCompletedKey[] = L"tasks.show-completed";
    static constexpr wchar_t TasksFilterKey[] = L"tasks.filter";
    static constexpr wchar_t TasksSortKey[] = L"tasks.sort";

    /* états d'une tâche : valeur "status" d'evo-core et libellé */
    struct TaskStatusInfo
    {
        wchar_t const* id;
        wchar_t const* label;
    };

    static constexpr TaskStatusInfo TaskStatuses[] = {
        { L"needs-action", L"Non commencée" },
        { L"in-process", L"En cours" },
        { L"completed", L"Terminée" },
        { L"waiting", L"En attente" },
        { L"deferred", L"Différée" },
        { L"cancelled", L"Annulée" },
    };
    static constexpr int TaskStatusNotStarted = 0;
    static constexpr int TaskStatusInProcess = 1;
    static constexpr int TaskStatusCompleted = 2;

    /* priorités proposées (0 non définie, 1 haute, 5 normale, 9 basse) */
    static constexpr int TaskPriorityValues[] = { 0, 1, 5, 9 };
    static constexpr wchar_t const* TaskPriorityLabels[] = { L"Non définie", L"Haute", L"Normale", L"Basse" };

    /* Indice de TaskStatuses pour une valeur "status" (Non commencée sinon). */
    static int TaskStatusIndex(hstring const& id)
    {
        for (int i = 0; i < static_cast<int>(std::size(TaskStatuses)); i++)
            if (id == TaskStatuses[i].id)
                return i;
        return TaskStatusNotStarted;
    }

    /* Indice de TaskPriorityValues pour une priorité iCalendar : 1 à 4 haute,
     * 5 normale, 6 à 9 basse, 0 non définie. */
    static int TaskPriorityIndex(int priority)
    {
        if (priority <= 0)
            return 0;
        return priority < 5 ? 1 : (priority == 5 ? 2 : 3);
    }

    /* Minuit heure locale du jour de unix_time, décalé de day_offset jours. */
    static int64_t LocalMidnight(int64_t unix_time, int day_offset = 0)
    {
        time_t raw = static_cast<time_t>(unix_time);
        struct tm local_tm{};
        localtime_s(&local_tm, &raw);
        local_tm.tm_mday += day_offset;
        local_tm.tm_hour = 0;
        local_tm.tm_min = 0;
        local_tm.tm_sec = 0;
        local_tm.tm_isdst = -1;
        return static_cast<int64_t>(mktime(&local_tm));
    }

    /* Date d'une tâche : jour seul, ou jour et heure. */
    static hstring FormatTaskDate(double unix_time, bool date_only)
    {
        if (unix_time <= 0)
            return L"";

        Windows::Globalization::DateTimeFormatting::DateTimeFormatter formatter{ date_only ? L"shortdate" : L"shortdate shorttime" };
        return formatter.Format(winrt::clock::from_time_t(static_cast<time_t>(unix_time)));
    }

    /* Tâche non terminée dont l'échéance est passée (avant aujourd'hui pour
     * une date seule, avant maintenant pour une échéance avec heure). */
    static bool IsTaskOverdue(JsonObject const& task, int64_t now)
    {
        hstring status = task.GetNamedString(L"status", L"");
        int64_t due = static_cast<int64_t>(task.GetNamedNumber(L"due", 0));

        if (due <= 0 || status == L"completed" || status == L"cancelled")
            return false;

        return due < (task.GetNamedBoolean(L"due_date_only", true) ? LocalMidnight(now) : now);
    }

    /* Charge les listes de tâches (choix de la liste affichée) et toutes les
     * tâches, puis les affiche avec les filtres courants. */
    void MainWindow::LoadTasks()
    {
        RestoreTaskViewSettings();

        auto request = ++m_task_request;
        TaskProgress().IsActive(true);

        m_core->ListSources(L"tasks", [this](hstring const& json, hstring const& error)
        {
            JsonArray lists;
            if (!error.empty() || !JsonArray::TryParse(json, lists))
                return;

            m_task_lists = lists;
            m_updating_task_view = true;

            auto items = TaskListChoice().Items();
            items.Clear();
            items.Append(box_value(L"Toutes les listes"));
            int selected = 0;
            for (uint32_t i = 0; i < lists.Size(); i++)
            {
                auto list = lists.GetObjectAt(i);
                items.Append(box_value(list.GetNamedString(L"name", L"")));
                if (list.GetNamedString(L"uid", L"") == m_task_list_uid)
                    selected = static_cast<int>(i) + 1;
            }
            if (selected == 0)
                m_task_list_uid = L"";
            TaskListChoice().SelectedIndex(selected);

            m_updating_task_view = false;
            ShowTasks();
        });

        m_core->ListTasks(L"", [this, request](hstring const& json, hstring const& error)
        {
            if (request != m_task_request)
                return;

            TaskProgress().IsActive(false);

            JsonArray tasks;
            if (!error.empty() || !JsonArray::TryParse(json, tasks))
            {
                ShowStatus(L"Tâches : " + error, InfoBarSeverity::Error);
                return;
            }

            m_all_tasks = tasks;
            ShowTasks();
        });
    }

    /* Rétablit au premier affichage de la vue Tâches le filtre, le tri et
     * l'affichage des tâches terminées mémorisés (tasks.filter, tasks.sort,
     * tasks.show-completed). */
    void MainWindow::RestoreTaskViewSettings()
    {
        if (m_task_view_restored)
            return;

        auto& settings = ::EvolutionWin::Settings::Instance();
        m_updating_task_view = true;

        ShowCompletedTasksBox().IsChecked(settings.GetBoolean(TasksShowCompletedKey, true));
        TaskSortChoice().SelectedIndex(settings.GetString(TasksSortKey, L"due") == L"priority" ? 1 : 0);

        hstring filter = settings.GetString(TasksFilterKey, L"all");
        for (auto const& item : TaskFilterBar().Items())
        {
            if (unbox_value_or<hstring>(item.Tag(), L"") == filter)
                TaskFilterBar().SelectedItem(item);
        }

        m_updating_task_view = false;
        m_task_view_restored = true;
    }

    /* La tâche correspond-elle au filtre rapide ?
     * Paramètres :
     *   task   : tâche au format JSON d'evo-core
     *   filter : "all", "today", "overdue", "week" (lundi à dimanche) ou "nodate" */
    bool MainWindow::MatchesTaskFilter(JsonObject const& task, hstring const& filter)
    {
        int64_t due = static_cast<int64_t>(task.GetNamedNumber(L"due", 0));
        int64_t now = static_cast<int64_t>(std::time(nullptr));

        if (filter == L"nodate")
            return due <= 0;
        if (filter == L"overdue")
            return IsTaskOverdue(task, now);
        if (filter == L"today")
            return due > 0 && due >= LocalMidnight(now) && due < LocalMidnight(now, 1);
        if (filter == L"week")
        {
            time_t raw = static_cast<time_t>(now);
            struct tm local_tm{};
            localtime_s(&local_tm, &raw);
            int since_monday = (local_tm.tm_wday + 6) % 7;
            return due > 0 && due >= LocalMidnight(now, -since_monday) && due < LocalMidnight(now, 7 - since_monday);
        }

        return true;
    }

    /* Affiche les tâches chargées selon la liste choisie, le filtre rapide,
     * l'affichage des tâches terminées et le tri (terminées à la fin). */
    void MainWindow::ShowTasks()
    {
        auto items = TaskList().Items();
        items.Clear();

        if (!m_all_tasks)
            return;

        hstring filter = L"all";
        if (auto item = TaskFilterBar().SelectedItem())
            filter = unbox_value_or<hstring>(item.Tag(), L"all");
        bool show_completed = unbox_value_or<bool>(ShowCompletedTasksBox().IsChecked(), true);
        bool by_priority = TaskSortChoice().SelectedIndex() == 1;

        std::vector<JsonObject> tasks;
        for (auto const& value : m_all_tasks)
        {
            auto task = value.GetObject();
            if (!m_task_list_uid.empty() && task.GetNamedString(L"list_uid", L"") != m_task_list_uid)
                continue;
            if (!show_completed && task.GetNamedString(L"status", L"") == L"completed")
                continue;
            if (MatchesTaskFilter(task, filter))
                tasks.push_back(task);
        }

        /* evo-core trie déjà par échéance, priorité puis objet */
        auto due_key = [](JsonObject const& task)
        {
            double due = task.GetNamedNumber(L"due", 0);
            return due > 0 ? due : 1e18;
        };
        auto priority_key = [](JsonObject const& task)
        {
            double priority = task.GetNamedNumber(L"priority", 0);
            return priority > 0 ? priority : 10.0;
        };
        std::stable_sort(tasks.begin(), tasks.end(), [&](JsonObject const& a, JsonObject const& b)
        {
            bool done_a = a.GetNamedString(L"status", L"") == L"completed";
            bool done_b = b.GetNamedString(L"status", L"") == L"completed";
            if (done_a != done_b)
                return done_b;
            if (by_priority && priority_key(a) != priority_key(b))
                return priority_key(a) < priority_key(b);
            if (due_key(a) != due_key(b))
                return due_key(a) < due_key(b);
            return priority_key(a) < priority_key(b);
        });

        bool show_list = m_task_list_uid.empty() && m_task_lists && m_task_lists.Size() > 1;
        for (auto const& task : tasks)
            items.Append(BuildTaskRow(task, show_list));

        if (tasks.empty())
        {
            TextBlock empty;
            empty.Text(m_all_tasks.Size() == 0
                ? L"Aucune tâche. Créez-en une avec « Nouvelle tâche » (Ctrl+Maj+K)."
                : L"Aucune tâche ne correspond à ce filtre.");
            empty.Padding(ThicknessHelper::FromLengths(12, 12, 12, 12));
            empty.Foreground(Application::Current().Resources().Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Brush>());
            items.Append(empty);
        }
    }

    /* Ligne d'une tâche : case « terminée », objet (barré si terminée),
     * échéance (en rouge si en retard), priorité, état, catégories, liste,
     * et bouton de suppression. Un clic sur la ligne ouvre l'éditeur.
     * Paramètres :
     *   task      : tâche au format JSON d'evo-core
     *   show_list : afficher le nom de la liste de tâches */
    UIElement MainWindow::BuildTaskRow(JsonObject const& task, bool show_list)
    {
        auto resources = Application::Current().Resources();
        auto caption_style = resources.Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>();
        auto secondary_brush = resources.Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Brush>();
        hstring status = task.GetNamedString(L"status", L"needs-action");
        bool completed = status == L"completed";
        bool readonly = task.GetNamedBoolean(L"readonly", false);

        Grid row;
        row.Padding(ThicknessHelper::FromLengths(4, 8, 4, 8));
        row.ColumnSpacing(8);
        row.Tag(box_value(task.Stringify()));
        ColumnDefinition check_col;
        check_col.Width(GridLengthHelper::Auto());
        row.ColumnDefinitions().Append(check_col);
        row.ColumnDefinitions().Append(ColumnDefinition{});
        ColumnDefinition delete_col;
        delete_col.Width(GridLengthHelper::Auto());
        row.ColumnDefinitions().Append(delete_col);

        CheckBox done;
        done.IsChecked(completed);
        done.IsEnabled(!readonly);
        done.MinWidth(0);
        done.Padding(ThicknessHelper::FromUniformLength(0));
        done.VerticalAlignment(VerticalAlignment::Center);
        ToolTipService::SetToolTip(done, box_value(completed ? L"Marquer comme non terminée" : L"Marquer comme terminée"));
        done.Click([this, task](IInspectable const& sender, RoutedEventArgs const&)
        {
            SetTaskCompleted(task, unbox_value_or<bool>(sender.as<CheckBox>().IsChecked(), false));
        });
        row.Children().Append(done);

        StackPanel details;
        details.Spacing(2);
        details.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(details, 1);

        TextBlock title;
        title.Text(task.GetNamedString(L"summary", L"(Sans titre)"));
        title.Style(resources.Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
        title.TextTrimming(TextTrimming::CharacterEllipsis);
        if (completed || status == L"cancelled")
        {
            title.TextDecorations(Windows::UI::Text::TextDecorations::Strikethrough);
            title.Foreground(secondary_brush);
        }
        details.Children().Append(title);

        StackPanel line;
        line.Orientation(Orientation::Horizontal);

        double due = task.GetNamedNumber(L"due", 0);
        if (due > 0)
        {
            bool overdue = IsTaskOverdue(task, static_cast<int64_t>(std::time(nullptr)));
            TextBlock due_text;
            due_text.Style(caption_style);
            due_text.Text((overdue ? L"En retard, échéance : " : L"Échéance : ") +
                FormatTaskDate(due, task.GetNamedBoolean(L"due_date_only", true)));
            if (overdue)
                due_text.Foreground(resources.Lookup(box_value(L"SystemFillColorCriticalBrush")).as<Brush>());
            line.Children().Append(due_text);
        }

        std::wstring info;
        auto append = [&info](std::wstring const& part)
        {
            if (!info.empty())
                info += L"  •  ";
            info += part;
        };

        double start = task.GetNamedNumber(L"start", 0);
        if (start > 0)
            append(L"Début : " + std::wstring{ FormatTaskDate(start, task.GetNamedBoolean(L"start_date_only", true)) });

        int priority_index = TaskPriorityIndex(static_cast<int>(task.GetNamedNumber(L"priority", 0)));
        if (priority_index > 0)
            append(L"Priorité " + ToLower(TaskPriorityLabels[priority_index]));

        int percent = static_cast<int>(task.GetNamedNumber(L"percent", 0));
        std::wstring state{ TaskStatuses[TaskStatusIndex(status)].label };
        if (percent > 0 && percent < 100)
            state += L" (" + std::to_wstring(percent) + L" %)";
        append(state);

        std::wstring categories;
        for (auto const& value : task.GetNamedArray(L"categories", JsonArray{}))
        {
            if (value.ValueType() != JsonValueType::String)
                continue;
            if (!categories.empty())
                categories += L", ";
            categories += value.GetString();
        }
        if (!categories.empty())
            append(L"Catégories : " + categories);

        if (show_list)
            append(L"Liste : " + std::wstring{ task.GetNamedString(L"list_name", L"") });

        TextBlock info_text;
        info_text.Style(caption_style);
        info_text.TextTrimming(TextTrimming::CharacterEllipsis);
        info_text.Text(line.Children().Size() > 0 ? L"  •  " + info : info);
        line.Children().Append(info_text);
        details.Children().Append(line);

        hstring description = task.GetNamedString(L"description", L"");
        if (!description.empty())
        {
            TextBlock desc;
            desc.Text(description);
            desc.Style(caption_style);
            desc.Foreground(secondary_brush);
            desc.MaxLines(1);
            desc.TextTrimming(TextTrimming::CharacterEllipsis);
            details.Children().Append(desc);
        }

        row.Children().Append(details);

        Button delete_button;
        FontIcon delete_icon;
        delete_icon.Glyph(L"\xE74D");
        delete_icon.FontSize(14);
        delete_button.Content(delete_icon);
        delete_button.VerticalAlignment(VerticalAlignment::Center);
        delete_button.IsEnabled(!readonly);
        Grid::SetColumn(delete_button, 2);
        ToolTipService::SetToolTip(delete_button, box_value(L"Supprimer la tâche"));
        delete_button.Click([this, task](IInspectable const&, RoutedEventArgs const&)
        {
            DeleteTaskItem(task);
        });
        row.Children().Append(delete_button);

        return row;
    }

    /* Marque une tâche comme terminée (100 %, date d'achèvement) ou la rouvre.
     * Paramètres :
     *   task      : tâche au format JSON d'evo-core
     *   completed : true pour terminée, false pour non commencée */
    void MainWindow::SetTaskCompleted(JsonObject const& task, bool completed)
    {
        JsonObject request;
        request.SetNamedValue(L"list_uid", JsonValue::CreateStringValue(task.GetNamedString(L"list_uid", L"")));
        request.SetNamedValue(L"uid", JsonValue::CreateStringValue(task.GetNamedString(L"uid", L"")));
        request.SetNamedValue(L"status", JsonValue::CreateStringValue(completed ? L"completed" : L"needs-action"));

        m_core->ModifyTask(request.Stringify(), [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
                ShowStatus(L"Modification de la tâche : " + error, InfoBarSeverity::Error);
            LoadTasks();
        });
    }

    /* Supprime une tâche, puis recharge la liste.
     * Paramètres :
     *   task : tâche au format JSON d'evo-core */
    void MainWindow::DeleteTaskItem(JsonObject const& task)
    {
        hstring summary = task.GetNamedString(L"summary", L"");

        m_core->DeleteTask(task.GetNamedString(L"list_uid", L""), task.GetNamedString(L"uid", L""),
            [this, summary](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Suppression de la tâche : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Tâche « " + summary + L" » supprimée.", InfoBarSeverity::Informational);
            if (TasksViewPane().Visibility() == Visibility::Visible)
                LoadTasks();
        });
    }

    /* Gestionnaire du bouton « Nouvelle tâche ». */
    void MainWindow::OnNewTaskClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowTaskEditorAsync(nullptr);
    }

    /* Raccourci Ctrl+Maj+K, actif dans toute la fenêtre : nouvelle tâche. */
    void MainWindow::OnNewTaskAccelerator(Input::KeyboardAccelerator const&, Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        ShowTaskEditorAsync(nullptr);
    }

    /* Gestionnaire du bouton d'actualisation des tâches. */
    void MainWindow::OnRefreshTasksClick(IInspectable const&, RoutedEventArgs const&)
    {
        LoadTasks();
    }

    /* Choix de la liste de tâches affichée (« Toutes les listes » en tête). */
    void MainWindow::OnTaskListChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_updating_task_view)
            return;

        int index = TaskListChoice().SelectedIndex();
        m_task_list_uid = (index > 0 && m_task_lists && static_cast<uint32_t>(index) <= m_task_lists.Size())
            ? m_task_lists.GetObjectAt(static_cast<uint32_t>(index - 1)).GetNamedString(L"uid", L"")
            : hstring{};
        ShowTasks();
    }

    /* Filtre rapide (toutes, aujourd'hui, en retard, cette semaine, sans date). */
    void MainWindow::OnTaskFilterChanged(SelectorBar const& sender, SelectorBarSelectionChangedEventArgs const&)
    {
        /* aussi déclenché au chargement du XAML, avant la lecture des préférences */
        if (m_updating_task_view || !m_task_view_restored)
            return;

        if (auto item = sender.SelectedItem())
            ::EvolutionWin::Settings::Instance().SetString(TasksFilterKey, unbox_value_or<hstring>(item.Tag(), L"all"));
        ShowTasks();
    }

    /* Tri par échéance ou par priorité. */
    void MainWindow::OnTaskSortChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_updating_task_view || !m_task_view_restored)
            return;

        ::EvolutionWin::Settings::Instance().SetString(TasksSortKey, TaskSortChoice().SelectedIndex() == 1 ? L"priority" : L"due");
        ShowTasks();
    }

    /* Afficher ou masquer les tâches terminées. */
    void MainWindow::OnShowCompletedTasksClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_task_view_restored)
            return;

        ::EvolutionWin::Settings::Instance().SetBoolean(TasksShowCompletedKey,
            unbox_value_or<bool>(ShowCompletedTasksBox().IsChecked(), true));
        ShowTasks();
    }

    /* Clic sur une tâche de la liste : ouvre l'éditeur. */
    void MainWindow::OnTaskItemClick(IInspectable const&, ItemClickEventArgs const& args)
    {
        auto row = args.ClickedItem().try_as<Grid>();
        JsonObject task;
        if (row && JsonObject::TryParse(unbox_value_or<hstring>(row.Tag(), L""), task))
            ShowTaskEditorAsync(task);
    }

    /* Éditeur de tâche (création si task est nul) : liste de tâches (création
     * seulement), objet, description, début, échéance, priorité, état,
     * pourcentage d'achèvement et catégories. Les dates d'une tâche existante
     * ne sont réécrites (en date seule) que si l'utilisateur les a changées.
     * Paramètres :
     *   task : tâche au format JSON d'evo-core, ou nullptr pour une nouvelle tâche */
    fire_and_forget MainWindow::ShowTaskEditorAsync(JsonObject task)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        bool is_new = !task;
        bool readonly = !is_new && task.GetNamedBoolean(L"readonly", false);
        auto caption_style = Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>();

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(is_new ? L"Nouvelle tâche" : L"Modifier la tâche"));
        dialog.PrimaryButtonText(L"Enregistrer");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.IsPrimaryButtonEnabled(!readonly);
        if (!is_new && !readonly)
            dialog.SecondaryButtonText(L"Supprimer");

        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(440);

        /* liste de tâches : choisie à la création, affichée ensuite */
        ComboBox list_box;
        std::vector<hstring> list_uids{ hstring{} };
        if (is_new)
        {
            list_box.Header(box_value(L"Liste de tâches"));
            list_box.HorizontalAlignment(HorizontalAlignment::Stretch);
            list_box.Items().Append(box_value(L"Liste par défaut"));
            int selected = 0;
            if (m_task_lists)
            {
                for (auto const& value : m_task_lists)
                {
                    auto list = value.GetObject();
                    list_uids.push_back(list.GetNamedString(L"uid", L""));
                    list_box.Items().Append(box_value(list.GetNamedString(L"name", L"")));
                    if (list_uids.back() == m_task_list_uid)
                        selected = static_cast<int>(list_uids.size()) - 1;
                }
            }
            list_box.SelectedIndex(selected);
            if (list_uids.size() > 1)
                panel.Children().Append(list_box);
        }
        else
        {
            TextBlock list_text;
            list_text.Text(L"Liste de tâches : " + task.GetNamedString(L"list_name", L"") +
                (readonly ? L" (lecture seule)" : L""));
            list_text.Style(caption_style);
            panel.Children().Append(list_text);
        }

        TextBox summary_box;
        summary_box.Header(box_value(L"Objet"));
        summary_box.PlaceholderText(L"Que faut-il faire ?");
        if (!is_new)
            summary_box.Text(task.GetNamedString(L"summary", L""));
        panel.Children().Append(summary_box);

        TextBox desc_box;
        desc_box.Header(box_value(L"Description"));
        desc_box.AcceptsReturn(true);
        desc_box.TextWrapping(TextWrapping::Wrap);
        desc_box.Height(90);
        if (!is_new)
            desc_box.Text(task.GetNamedString(L"description", L""));
        panel.Children().Append(desc_box);

        /* dates (jour seul) avec bouton d'effacement */
        Grid dates;
        dates.ColumnSpacing(16);
        dates.ColumnDefinitions().Append(ColumnDefinition{});
        dates.ColumnDefinitions().Append(ColumnDefinition{});
        auto add_date = [&dates, &task, is_new](wchar_t const* header, wchar_t const* key, int column)
        {
            CalendarDatePicker picker;
            picker.Header(box_value(header));
            picker.PlaceholderText(L"Aucune");
            picker.HorizontalAlignment(HorizontalAlignment::Stretch);
            double value = is_new ? 0 : task.GetNamedNumber(key, 0);
            if (value > 0)
                picker.Date(winrt::clock::from_time_t(static_cast<time_t>(value)));

            Button clear;
            FontIcon clear_icon;
            clear_icon.Glyph(L"\xE894");
            clear_icon.FontSize(12);
            clear.Content(clear_icon);
            clear.VerticalAlignment(VerticalAlignment::Bottom);
            ToolTipService::SetToolTip(clear, box_value(L"Aucune date"));
            clear.Click([weak = make_weak(picker)](IInspectable const&, RoutedEventArgs const&)
            {
                if (auto picker = weak.get())
                    picker.Date(nullptr);
            });

            Grid cell;
            cell.ColumnSpacing(4);
            cell.ColumnDefinitions().Append(ColumnDefinition{});
            ColumnDefinition clear_col;
            clear_col.Width(GridLengthHelper::Auto());
            cell.ColumnDefinitions().Append(clear_col);
            Grid::SetColumn(clear, 1);
            cell.Children().Append(picker);
            cell.Children().Append(clear);
            Grid::SetColumn(cell, column);
            dates.Children().Append(cell);
            return picker;
        };
        auto start_picker = add_date(L"Date de début", L"start", 0);
        auto due_picker = add_date(L"Échéance", L"due", 1);
        panel.Children().Append(dates);

        Grid state;
        state.ColumnSpacing(12);
        for (int i = 0; i < 3; i++)
            state.ColumnDefinitions().Append(ColumnDefinition{});

        ComboBox priority_box;
        priority_box.Header(box_value(L"Priorité"));
        priority_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        for (auto label : TaskPriorityLabels)
            priority_box.Items().Append(box_value(label));
        int priority_index = is_new ? 0 : TaskPriorityIndex(static_cast<int>(task.GetNamedNumber(L"priority", 0)));
        priority_box.SelectedIndex(priority_index);
        state.Children().Append(priority_box);

        ComboBox status_box;
        status_box.Header(box_value(L"État"));
        status_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        for (auto const& info : TaskStatuses)
            status_box.Items().Append(box_value(info.label));
        status_box.SelectedIndex(is_new ? TaskStatusNotStarted : TaskStatusIndex(task.GetNamedString(L"status", L"")));
        Grid::SetColumn(status_box, 1);
        state.Children().Append(status_box);

        NumberBox percent_box;
        percent_box.Header(box_value(L"Achèvement (%)"));
        percent_box.Minimum(0);
        percent_box.Maximum(100);
        percent_box.SmallChange(10);
        percent_box.LargeChange(25);
        percent_box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);
        percent_box.Value(is_new ? 0 : task.GetNamedNumber(L"percent", 0));
        Grid::SetColumn(percent_box, 2);
        state.Children().Append(percent_box);
        panel.Children().Append(state);

        /* état et pourcentage cohérents : terminée = 100 %, non commencée = 0 % */
        auto syncing = std::make_shared<bool>(false);
        status_box.SelectionChanged([syncing, weak_percent = make_weak(percent_box)](IInspectable const& sender, SelectionChangedEventArgs const&)
        {
            auto percent = weak_percent.get();
            int index = sender.as<ComboBox>().SelectedIndex();
            if (*syncing || !percent || index < 0)
                return;
            *syncing = true;
            if (index == TaskStatusCompleted)
                percent.Value(100);
            else if (index == TaskStatusNotStarted || percent.Value() >= 100)
                percent.Value(0);
            *syncing = false;
        });
        percent_box.ValueChanged([syncing, weak_status = make_weak(status_box)](NumberBox const& sender, NumberBoxValueChangedEventArgs const&)
        {
            auto status = weak_status.get();
            double value = sender.Value();
            if (*syncing || !status || std::isnan(value))
                return;
            *syncing = true;
            int index = status.SelectedIndex();
            if (value >= 100)
                status.SelectedIndex(TaskStatusCompleted);
            else if (value > 0 && (index == TaskStatusNotStarted || index == TaskStatusCompleted))
                status.SelectedIndex(TaskStatusInProcess);
            else if (value <= 0 && index == TaskStatusCompleted)
                status.SelectedIndex(TaskStatusNotStarted);
            *syncing = false;
        });

        std::wstring categories;
        if (!is_new)
        {
            for (auto const& value : task.GetNamedArray(L"categories", JsonArray{}))
            {
                if (value.ValueType() != JsonValueType::String)
                    continue;
                if (!categories.empty())
                    categories += L", ";
                categories += value.GetString();
            }
        }
        TextBox categories_box;
        categories_box.Header(box_value(L"Catégories"));
        categories_box.PlaceholderText(L"Travail, Personnel");
        categories_box.Text(categories);
        panel.Children().Append(categories_box);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result == ContentDialogResult::Secondary && !is_new)
        {
            DeleteTaskItem(task);
            co_return;
        }
        if (result != ContentDialogResult::Primary || readonly)
            co_return;
        if (is_new && Trim(summary_box.Text()).empty())
            co_return;

        JsonObject payload;
        if (is_new)
        {
            int index = list_box.SelectedIndex();
            if (index > 0 && static_cast<size_t>(index) < list_uids.size())
                payload.SetNamedValue(L"list_uid", JsonValue::CreateStringValue(list_uids[index]));
        }
        else
        {
            payload.SetNamedValue(L"list_uid", JsonValue::CreateStringValue(task.GetNamedString(L"list_uid", L"")));
            payload.SetNamedValue(L"uid", JsonValue::CreateStringValue(task.GetNamedString(L"uid", L"")));
        }
        payload.SetNamedValue(L"summary", JsonValue::CreateStringValue(summary_box.Text()));
        payload.SetNamedValue(L"description", JsonValue::CreateStringValue(desc_box.Text()));

        /* date choisie (minuit local) ; non envoyée si inchangée */
        auto set_date = [&](wchar_t const* key, wchar_t const* date_only_key, CalendarDatePicker const& picker)
        {
            int64_t value = picker.Date() ? LocalMidnight(static_cast<int64_t>(winrt::clock::to_time_t(picker.Date().Value()))) : 0;
            int64_t original = is_new ? 0 : static_cast<int64_t>(task.GetNamedNumber(key, 0));
            if (value == (original > 0 ? LocalMidnight(original) : 0))
                return;
            payload.SetNamedValue(key, JsonValue::CreateNumberValue(static_cast<double>(value)));
            payload.SetNamedValue(date_only_key, JsonValue::CreateBooleanValue(true));
        };
        set_date(L"start", L"start_date_only", start_picker);
        set_date(L"due", L"due_date_only", due_picker);

        /* priorité réécrite seulement si changée (garde par ex. 3 = haute) */
        int new_priority_index = priority_box.SelectedIndex();
        if (new_priority_index >= 0 && (is_new || new_priority_index != priority_index))
            payload.SetNamedValue(L"priority", JsonValue::CreateNumberValue(TaskPriorityValues[new_priority_index]));

        int status_index = (std::max)(status_box.SelectedIndex(), 0);
        payload.SetNamedValue(L"status", JsonValue::CreateStringValue(TaskStatuses[status_index].id));
        double percent = std::isnan(percent_box.Value()) ? 0 : std::clamp(std::round(percent_box.Value()), 0.0, 100.0);
        payload.SetNamedValue(L"percent", JsonValue::CreateNumberValue(percent));

        JsonArray category_list;
        std::wstring_view rest{ categories_box.Text() };
        while (!rest.empty())
        {
            auto end = rest.find_first_of(L",;");
            auto category = Trim(rest.substr(0, end));
            if (!category.empty())
                category_list.Append(JsonValue::CreateStringValue(category));
            rest = end == std::wstring_view::npos ? std::wstring_view{} : rest.substr(end + 1);
        }
        payload.SetNamedValue(L"categories", category_list);

        auto done = [this, is_new](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus((is_new ? L"Création de la tâche : " : L"Modification de la tâche : ") + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(is_new ? L"Tâche enregistrée." : L"Tâche modifiée.", InfoBarSeverity::Success);
            if (TasksViewPane().Visibility() == Visibility::Visible)
                LoadTasks();
        };

        if (is_new)
            m_core->CreateTask(payload.Stringify(), done);
        else
            m_core->ModifyTask(payload.Stringify(), done);
    }
}
