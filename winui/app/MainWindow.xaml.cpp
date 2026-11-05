#include "pch.h"
#include "MainWindow.xaml.h"
#include "WindowIcon.h"
#include "Settings.h"
#include "MainWindow.Helpers.h"
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

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    /**
     * @brief Constructeur de la fenêtre principale. Initialise sur premier événement Activated.
     */
    MainWindow::MainWindow()
    {
        /* Les éléments nommés XAML sont disponibles une fois activés */
        m_activated_token = Activated([this](IInspectable const&, WindowActivatedEventArgs const&)
        {
            if (!m_initialized)
            {
                m_initialized = true;
                ::EvolutionWin::Helpers::PurgeDirectoryFiles(::EvolutionWin::Helpers::GetDragDropTempDir());
                Initialize();
            }
            UpdateTitleBarPassthrough();
        });
    }

    /**
     * @brief Destructeur de la fenêtre principale. Libère l'écran de démarrage et le pont CoreBridge.
     */
    MainWindow::~MainWindow()
    {
        ::EvolutionWin::Helpers::PurgeDirectoryFiles(::EvolutionWin::Helpers::GetDragDropTempDir());
        CloseSplash();
        m_core.reset();
    }

    /**
     * @brief Démarre le pont de messagerie CoreBridge s'il n'est pas déjà instancié.
     */
    void MainWindow::StartCore()
    {
        if (!m_core)
        {
            m_core = std::make_shared<::EvolutionWin::CoreBridge>(
                DispatcherQueue(),
                [this](hstring const& event, hstring const& json) { OnCoreEvent(event, json); });
        }
    }

    /**
     * @brief Associe la fenêtre d'écran de démarrage (Splash screen) et arme le minuteur de secours.
     * @param splash Instance de la SplashWindow affichée au lancement.
     */
    void MainWindow::SetSplashWindow(winrt::EvolutionWin::SplashWindow const& splash)
    {
        m_splash = splash;
        if (m_splash)
        {
            StartCore();

            m_splash_timer = DispatcherTimer();
            m_splash_timer.Interval(std::chrono::seconds(6));
            m_splash_timer.Tick([this](IInspectable const&, IInspectable const&)
            {
                if (m_splash)
                {
                    Activate();
                    CloseSplash();
                    if (!m_initialized)
                    {
                        m_initialized = true;
                        Initialize();
                    }
                }
            });
            m_splash_timer.Start();
        }
    }

    /**
     * @brief Ferme l'écran de démarrage et désactive le minuteur de secours.
     */
    void MainWindow::CloseSplash()
    {
        if (m_splash_timer)
        {
            m_splash_timer.Stop();
            m_splash_timer = nullptr;
        }
        if (m_splash)
        {
            auto splash = m_splash;
            m_splash = nullptr;
            splash.Close();
        }
    }

    void MainWindow::Initialize()
    {
        ExtendsContentIntoTitleBar(true);
        /* barre de titre haute (48 px) : place pour la recherche */
        AppWindow().TitleBar().PreferredHeightOption(Microsoft::UI::Windowing::TitleBarHeightOption::Tall);
        SetTitleBar(AppTitleBar());
        AppWindow().Resize({ 1500, 950 });
        ::EvolutionWin::SetWindowIcon(AppWindow());
        UpdateSearchPlaceholder();
        RebuildQuickAccess();
        SyncConfidentialControls();
        AppTitleBar().SizeChanged([this](IInspectable const&, SizeChangedEventArgs const&) { UpdateTitleBarPassthrough(); });
        ApplyTheme(::EvolutionWin::Settings::Instance().GetString(L"appearance.theme", L"system"));

        /* Accélérateurs clavier globaux (Ctrl+, pour Paramètres, Alt+F2 pour To-Do bar, navigation Ctrl+1..4 et Ctrl+Alt+1..5) */
        if (auto root = this->Content().try_as<FrameworkElement>())
        {
            Microsoft::UI::Xaml::Input::KeyboardAccelerator accel;
            accel.Modifiers(Windows::System::VirtualKeyModifiers::Control);
            accel.Key(static_cast<Windows::System::VirtualKey>(188)); // VK_OEM_COMMA
            accel.Invoked({ this, &MainWindow::OnSettingsAccelerator });
            root.KeyboardAccelerators().Append(accel);

            Microsoft::UI::Xaml::Input::KeyboardAccelerator todobar_accel;
            todobar_accel.Modifiers(Windows::System::VirtualKeyModifiers::Menu); // Alt
            todobar_accel.Key(Windows::System::VirtualKey::F2);
            todobar_accel.Invoked({ this, &MainWindow::OnTodoBarAccelerator });
            root.KeyboardAccelerators().Append(todobar_accel);

            Microsoft::UI::Xaml::Input::KeyboardAccelerator gotodate_accel;
            gotodate_accel.Modifiers(Windows::System::VirtualKeyModifiers::Control);
            gotodate_accel.Key(Windows::System::VirtualKey::G);
            gotodate_accel.Invoked([this](auto const&, auto const& args)
            {
                args.Handled(true);
                ShowGoToDateDialogAsync();
            });
            root.KeyboardAccelerators().Append(gotodate_accel);

            // Raccourcis de navigation globale (Ctrl+1 à Ctrl+4) et vues calendrier (Ctrl+Alt+1 à Ctrl+Alt+5)
            auto add_nav_accel = [root, this](Windows::System::VirtualKey key, winrt::hstring const& module_tag, winrt::hstring const& cal_period, Windows::System::VirtualKeyModifiers mod)
            {
                Microsoft::UI::Xaml::Input::KeyboardAccelerator nav_accel;
                nav_accel.Modifiers(mod);
                nav_accel.Key(key);
                nav_accel.Invoked([this, module_tag, cal_period](Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
                {
                    args.Handled(true);
                    if (m_view_tag == L"calendar" && !cal_period.empty())
                    {
                        SetCalendarPeriod(cal_period);
                    }
                    else if (!module_tag.empty())
                    {
                        SelectNavigationModule(module_tag);
                    }
                });
                root.KeyboardAccelerators().Append(nav_accel);
            };

            // Ctrl+1..4 (Courrier, Calendrier, Contacts, Tâches)
            add_nav_accel(Windows::System::VirtualKey::Number1, L"mail", L"", Windows::System::VirtualKeyModifiers::Control);
            add_nav_accel(Windows::System::VirtualKey::Number2, L"calendar", L"", Windows::System::VirtualKeyModifiers::Control);
            add_nav_accel(Windows::System::VirtualKey::Number3, L"contacts", L"", Windows::System::VirtualKeyModifiers::Control);
            add_nav_accel(Windows::System::VirtualKey::Number4, L"tasks", L"", Windows::System::VirtualKeyModifiers::Control);

            // Ctrl+Alt+1..5 (navigation globale ou vues calendrier Jour, Semaine ouvrée, Semaine, Mois, Liste)
            add_nav_accel(Windows::System::VirtualKey::Number1, L"mail", L"day", Windows::System::VirtualKeyModifiers::Control | Windows::System::VirtualKeyModifiers::Menu);
            add_nav_accel(Windows::System::VirtualKey::Number2, L"calendar", L"workweek", Windows::System::VirtualKeyModifiers::Control | Windows::System::VirtualKeyModifiers::Menu);
            add_nav_accel(Windows::System::VirtualKey::Number3, L"contacts", L"week", Windows::System::VirtualKeyModifiers::Control | Windows::System::VirtualKeyModifiers::Menu);
            add_nav_accel(Windows::System::VirtualKey::Number4, L"tasks", L"month", Windows::System::VirtualKeyModifiers::Control | Windows::System::VirtualKeyModifiers::Menu);
            add_nav_accel(Windows::System::VirtualKey::Number5, L"", L"list", Windows::System::VirtualKeyModifiers::Control | Windows::System::VirtualKeyModifiers::Menu);
        }
        RestorePaneWidths();
        RestoreTodoBarSettings();
        ApplyFolderPaneState(GetFolderPaneState());
        ApplyReadingPanePosition(::EvolutionWin::Settings::Instance().GetString(L"layout.reading-pane", L"right"));
        ApplyCompactDensity(::EvolutionWin::Settings::Instance().GetBoolean(L"layout.compact-density", false));
        ApplyCalendarPreferences();
        m_calendar_period = ::EvolutionWin::Settings::Instance().GetString(L"calendar.period", L"week");
        m_updating_period = true;
        for (auto const& item : CalendarPeriodBar().Items())
            item.IsSelected(unbox_value_or<hstring>(item.Tag(), L"") == m_calendar_period);
        m_updating_period = false;

        /* menu contextuel des dossiers (clic droit, touche Menu) */
        FolderTree().ContextRequested([this](UIElement const& sender, Input::ContextRequestedEventArgs const& args)
        {
            ShowFolderContextMenu(sender, args);
        });

        InitializeReaderAsync();
        CheckForUpdateOnStartup();

        /* Module affiché au démarrage configuré dans les préférences (Courrier par défaut) */
        hstring startup_view = ::EvolutionWin::Settings::Instance().GetString(L"appearance.startup-view", L"mail");
        if (startup_view != L"mail")
        {
            SelectNavigationModule(startup_view);
        }

        StartCore();
        UpdateConnectionStatus(L"global", m_is_offline ? L"offline" : L"online", L"");

        Closed([this](IInspectable const&, WindowEventArgs const&)
        {
            /* Purge automatique de la corbeille si le paramètre est activé */
            if (::EvolutionWin::Settings::Instance().GetBoolean(L"mail.empty-trash-on-exit", false))
            {
                EmptyAllTrashFolders();
                Sleep(200);
            }

            /* Fermeture de toutes les fenêtres secondaires (messages, rédaction) */
            CloseAllSecondaryWindows();

            /* Arrêt propre du moteur CoreBridge / evo-core avant la destruction de la fenêtre */
            m_core.reset();

            /* Nettoyage de sécurité résiduel : fermeture de tout processus satellite orphelin persistant */
            ::EvolutionWin::Helpers::TerminateResidualSatelliteProcesses(300);
        });
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
            if (m_splash)
            {
                Activate();
                CloseSplash();
                if (!m_initialized)
                {
                    m_initialized = true;
                    Initialize();
                }
            }
            if (m_initialized)
            {
                StatusBar().IsOpen(false);
                LoadAccounts();
            }
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
            if (((account == m_account && folder == m_folder) || UnifiedFolderMatches(account, folder)) &&
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
            if (m_splash)
            {
                Activate();
                CloseSplash();
                if (!m_initialized)
                {
                    m_initialized = true;
                    Initialize();
                }
            }
            UpdateTaskbarProgress(false, 0, true);
            ShowStatus(L"Le moteur de messagerie n'a pas pu démarrer : " + message, InfoBarSeverity::Error);
        }
        else if (event == L"status")
        {
            hstring display_status = message;
            if (message == L"Connecting to evolution-data-server")
                display_status = L"Connexion à evolution-data-server…";
            if (m_splash)
            {
                m_splash.SetStatus(display_status);
            }
            ShowStatus(display_status, InfoBarSeverity::Informational);
        }
        else if (event == L"new-mail")
        {
            ShowNewMailNotification(object);
        }
        else if (event == L"progress")
        {
            bool active = object.GetNamedBoolean(L"active", true);
            int32_t percent = static_cast<int32_t>(object.GetNamedNumber(L"percent", -1));
            UpdateStatusBarProgress(active, message, percent);
        }
        else if (event == L"connection-status")
        {
            hstring account = object.GetNamedString(L"account", L"");
            hstring status = object.GetNamedString(L"status", L"");
            UpdateConnectionStatus(account, status, message);
        }
        else if (event == L"auth-required")
        {
            if (m_splash)
            {
                Activate();
                CloseSplash();
                if (!m_initialized)
                {
                    m_initialized = true;
                    Initialize();
                }
            }
            UpdateConnectionStatus(object.GetNamedString(L"source_uid", L""), L"auth-required", L"Authentification requise");
            PromptPasswordAsync(
                object.GetNamedString(L"source_uid", L""),
                object.GetNamedString(L"name", L""),
                object.GetNamedString(L"user", L""),
                object.GetNamedBoolean(L"rejected", false));
        }
        else if (event == L"tls-trust-required")
        {
            if (m_splash)
            {
                Activate();
                CloseSplash();
                if (!m_initialized)
                {
                    m_initialized = true;
                    Initialize();
                }
            }
            PromptTlsCertificateAsync(object);
        }
        else if (event == L"oauth2-required")
        {
            if (m_splash)
            {
                Activate();
                CloseSplash();
                if (!m_initialized)
                {
                    m_initialized = true;
                    Initialize();
                }
            }
            UpdateConnectionStatus(object.GetNamedString(L"source_uid", L""), L"auth-required", L"Connexion requise");
            SignInOAuth2Async(object.GetNamedString(L"source_uid", L""), object.GetNamedString(L"name", L""));
        }
    }

    void MainWindow::ShowStatus(hstring const& message, InfoBarSeverity severity)
    {
        if (!m_initialized)
            return;

        StatusBar().Message(message);
        StatusBar().Severity(severity);
        StatusBar().IsOpen(!message.empty());
    }

    /* Badge de la barre latérale sur « Courrier » : total des non-lus des
     * boîtes de réception de tous les comptes (masqué à zéro). */
    void MainWindow::UpdateMailBadge()
    {
        if (!m_initialized)
            return;

        int32_t unread = 0;
        hstring inbox_glyph = FolderGlyph(L"inbox");

        for (auto const& [key, node] : m_nodes)
        {
            /* la Boîte de réception unifiée n'est pas comptée : elle reprend les autres */
            if (auto item = node.Content().try_as<EvolutionWin::FolderItem>(); item && !item.IsAccount() && item.Glyph() == inbox_glyph
                && !::EvolutionWin::UnifiedViews::IsUnifiedAccount(std::wstring_view{ item.Account() }))
                unread += item.Unread();
        }
        UpdateUnifiedCounts();

        if (unread <= 0)
        {
            MailItem().InfoBadge(nullptr);
            ToolTipService::SetToolTip(MailItem(), box_value(L"Courrier"));
            UpdateTaskbarBadge(0);
            return;
        }

        InfoBadge badge;
        badge.Value(unread);
        MailItem().InfoBadge(badge);
        ToolTipService::SetToolTip(MailItem(), box_value(L"Courrier (" + to_hstring(unread) + L" non lus)"));
        UpdateTaskbarBadge(unread);
    }

    /**
     * Sélectionne par programmation le module de navigation demandé (mail, calendar, contacts, tasks, settings).
     * Paramètres :
     *   tag : identifiant du module cible
     */
    void MainWindow::SelectNavigationModule(winrt::hstring const& tag)
    {
        if (tag == L"settings")
        {
            Navigation().SelectedItem(Navigation().SettingsItem());
            return;
        }

        for (auto const& item_obj : Navigation().MenuItems())
        {
            if (auto nav_item = item_obj.try_as<NavigationViewItem>())
            {
                if (unbox_value_or<winrt::hstring>(nav_item.Tag(), L"") == tag)
                {
                    Navigation().SelectedItem(nav_item);
                    break;
                }
            }
        }
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
            InitSettingsView();
            SelectSettingsCategory(m_current_settings_category);
            UpdateTodoBarVisibility();
            return;
        }

        auto item = args.SelectedItem().try_as<NavigationViewItem>();
        hstring tag = item ? unbox_value_or<hstring>(item.Tag(), L"") : hstring{};
        m_view_tag = tag;
        RefreshBanner();
        UpdateRibbon();

        if (auto box = SearchBox())
        {
            if (tag == L"contacts")
                box.PlaceholderText(L"Rechercher un contact (Ctrl+E)");
            else if (tag == L"mail")
                UpdateSearchPlaceholder();
            else if (tag == L"tasks")
                box.PlaceholderText(L"Rechercher dans les tâches (Ctrl+E)");
            else if (tag == L"calendar")
            {
                box.PlaceholderText(L"Rechercher un événement (Ctrl+E)");
                box.Text(m_calendar_search_query);
            }
            else
                box.PlaceholderText(L"Rechercher (Ctrl+E)");
        }

        SearchScopeBox().Visibility(tag == L"mail" ? Visibility::Visible : Visibility::Collapsed);
        MailView().Visibility(tag == L"mail" ? Visibility::Visible : Visibility::Collapsed);
        CalendarViewPane().Visibility(tag == L"calendar" ? Visibility::Visible : Visibility::Collapsed);
        ContactsViewPane().Visibility(tag == L"contacts" ? Visibility::Visible : Visibility::Collapsed);
        TasksViewPane().Visibility(tag == L"tasks" ? Visibility::Visible : Visibility::Collapsed);
        SettingsViewPane().Visibility(Visibility::Collapsed);
        UpdateTodoBarVisibility();

        if (tag == L"calendar")
            LoadCalendarSources();
        else if (tag == L"contacts")
            LoadContactBooks();
        else if (tag == L"tasks")
            LoadTasks();
    }

    /* Entrées de navigation invoquées. */
    void MainWindow::OnNavigationItemInvoked(NavigationView const&, NavigationViewItemInvokedEventArgs const& /*args*/)
    {
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

    /* Croix du bandeau : l'avis affiché est retiré, le suivant apparaît. */
    void MainWindow::OnInfoBannerClosed(InfoBar const& sender, InfoBarClosedEventArgs const&)
    {
        hstring id = unbox_value_or<hstring>(sender.Tag(), L"");
        if (!id.empty())
            ClearBanner(std::wstring{ id });
    }
}
