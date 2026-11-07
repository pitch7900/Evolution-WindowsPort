#include "pch.h"
#include "MainWindow.xaml.h"
#include "WindowIcon.h"
#include "Settings.h"
#include "MainWindow.Helpers.h"
#include "UiTranslate.h"
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
        LocalizeInterface();
        /* textes statiques du XAML : traduits à l'exécution (arbre logique, puis visuel au chargement) */
        ::EvolutionWin::UiTranslate::ApplyToWindow(*this);
        if (auto root = Content().try_as<FrameworkElement>())
            root.Loaded([this](IInspectable const&, RoutedEventArgs const&) { ::EvolutionWin::UiTranslate::ApplyToWindow(*this, true); });
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
        StartEventReminders();

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
                HideStatus();
                LoadAccounts();

                /* à l'ouverture, en tâche de fond : calendriers et groupes de calendriers d'Outlook
                   (noms par Graph, membres par EWS), sans attendre l'affichage de la vue Calendrier */
                if (!m_calendar_background_started)
                {
                    m_calendar_background_started = true;
                    LoadCalendarSources(false);
                }
            }
        }
        else if (event == L"folder-changed")
        {
            hstring account = object.GetNamedString(L"account", L"");
            hstring folder = object.GetNamedString(L"folder", L"");

            SetFolderUnread(account, folder, static_cast<int32_t>(object.GetNamedNumber(L"unread", 0)));
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
            ShowStatus(Fmt(Tr(L"Envoi de « {0} »…"), { object.GetNamedString(L"subject", L"") }), InfoBarSeverity::Informational);
        }
        else if (event == L"sent")
        {
            ShowStatus(Fmt(Tr(L"Message envoyé : « {0} »"), { object.GetNamedString(L"subject", L"") }), InfoBarSeverity::Success);
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
            ShowStatus(Fmt(Tr(L"Le moteur de messagerie n'a pas pu démarrer : {0}"), { message }), InfoBarSeverity::Error);
        }
        else if (event == L"status")
        {
            hstring display_status = message;
            if (message == L"Connecting to evolution-data-server")
                display_status = Tr(L"Connexion à evolution-data-server…");
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
            hstring progress_text = message;
            if (object.GetNamedString(L"kind", L"") == L"offline")
            {
                /* téléchargement hors connexion : texte composé ici, dans la langue de l'interface */
                hstring folder_name = object.GetNamedString(L"folder", L"");
                int32_t total = static_cast<int32_t>(object.GetNamedNumber(L"total", 0));
                progress_text = !active ? Tr(L"Synchronisation hors connexion terminée")
                    : total > 0
                        ? Fmt(Tr(L"Téléchargement hors connexion : {0} ({1}/{2})"),
                            { folder_name, to_hstring(static_cast<int32_t>(object.GetNamedNumber(L"done", 0))), to_hstring(total) })
                        : Fmt(Tr(L"Préparation du téléchargement hors connexion : {0}"), { folder_name });
            }
            UpdateStatusBarProgress(active, progress_text, percent);
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
            UpdateConnectionStatus(object.GetNamedString(L"source_uid", L""), L"auth-required", Tr(L"Authentification requise"));
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
            UpdateConnectionStatus(object.GetNamedString(L"source_uid", L""), L"auth-required", Tr(L"Connexion requise"));
            SignInOAuth2Async(object.GetNamedString(L"source_uid", L""), object.GetNamedString(L"name", L""));
        }
    }

    void MainWindow::ShowStatus(hstring const& message, InfoBarSeverity severity)
    {
        if (!m_initialized)
            return;

        if (message.empty())
        {
            HideStatus();
            return;
        }

        /* message compact dans la barre d'état du bas : icône de gravité + texte sur une ligne
           (texte complet en infobulle), disparition automatique */
        auto resources = Application::Current().Resources();
        hstring glyph = L"\uE946", brush = L"SystemFillColorAttentionBrush";
        int seconds = 7;
        switch (severity)
        {
        case InfoBarSeverity::Success:
            glyph = L"\uE73E";
            brush = L"SystemFillColorSuccessBrush";
            break;
        case InfoBarSeverity::Warning:
            glyph = L"\uE7BA";
            brush = L"SystemFillColorCautionBrush";
            seconds = 12;
            break;
        case InfoBarSeverity::Error:
            glyph = L"\uEA39";
            brush = L"SystemFillColorCriticalBrush";
            seconds = 20;
            break;
        default:
            break;
        }

        StatusMessageIcon().Glyph(glyph);
        StatusMessageIcon().Foreground(resources.Lookup(box_value(brush)).as<Microsoft::UI::Xaml::Media::Brush>());
        StatusMessageText().Text(message);
        ToolTipService::SetToolTip(StatusMessageText(), box_value(message));
        StatusMessagePanel().Visibility(Visibility::Visible);

        if (!m_status_timer)
        {
            m_status_timer = DispatcherTimer();
            m_status_timer.Tick([weak = get_weak()](IInspectable const&, IInspectable const&)
            {
                if (auto self = weak.get())
                    self->HideStatus();
            });
        }
        m_status_timer.Stop();
        m_status_timer.Interval(std::chrono::seconds(seconds));
        m_status_timer.Start();
    }

    /* Masque le message compact de la barre d'état. */
    void MainWindow::HideStatus()
    {
        if (m_status_timer)
            m_status_timer.Stop();
        StatusMessagePanel().Visibility(Visibility::Collapsed);
        StatusMessageText().Text(L"");
    }

    /* Bouton de fermeture du message compact. */
    void MainWindow::OnStatusMessageCloseClick(IInspectable const&, RoutedEventArgs const&)
    {
        HideStatus();
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
            ToolTipService::SetToolTip(MailItem(), box_value(Tr(L"Courrier", L"Mail")));
            UpdateTaskbarBadge(0);
            return;
        }

        InfoBadge badge;
        badge.Value(unread);
        MailItem().InfoBadge(badge);
        ToolTipService::SetToolTip(MailItem(), box_value(unread > 1
            ? Fmt(Tr(L"Courrier ({0} non lus)", L"Mail ({0} unread)"), { to_hstring(unread) })
            : Fmt(Tr(L"Courrier ({0} non lu)", L"Mail ({0} unread)"), { to_hstring(unread) })));
        UpdateTaskbarBadge(unread);
    }

    /**
     * @brief Traduit dynamiquement les contrôles statiques principaux (navigation, onglets du ruban).
     */
    void MainWindow::LocalizeInterface()
    {
        // 1. Éléments de la barre de navigation
        if (auto nav = Navigation())
        {
            for (auto const& item_obj : nav.MenuItems())
            {
                if (auto nav_item = item_obj.try_as<NavigationViewItem>())
                {
                    hstring tag = unbox_value_or<hstring>(nav_item.Tag(), L"");
                    if (tag == L"mail")
                    {
                        nav_item.Content(box_value(Tr(L"Courrier", L"Mail", L"Nav_Mail")));
                        ToolTipService::SetToolTip(nav_item, box_value(Tr(L"Courrier (Ctrl+1)", L"Mail (Ctrl+1)")));
                    }
                    else if (tag == L"calendar")
                    {
                        nav_item.Content(box_value(Tr(L"Calendrier", L"Calendar", L"Nav_Calendar")));
                        ToolTipService::SetToolTip(nav_item, box_value(Tr(L"Calendrier (Ctrl+2)", L"Calendar (Ctrl+2)")));
                    }
                    else if (tag == L"contacts")
                    {
                        nav_item.Content(box_value(Tr(L"Contacts", L"Contacts", L"Nav_Contacts")));
                        ToolTipService::SetToolTip(nav_item, box_value(Tr(L"Contacts (Ctrl+3)", L"Contacts (Ctrl+3)")));
                    }
                    else if (tag == L"tasks")
                    {
                        nav_item.Content(box_value(Tr(L"Tâches", L"Tasks", L"Nav_Tasks")));
                        ToolTipService::SetToolTip(nav_item, box_value(Tr(L"Tâches (Ctrl+4)", L"Tasks (Ctrl+4)")));
                    }
                }
            }

            if (auto settings_item = nav.SettingsItem().try_as<NavigationViewItem>())
            {
                settings_item.Content(box_value(Tr(L"Paramètres", L"Settings", L"Nav_Settings")));
            }
        }

        // 2. Onglets du ruban (SelectorBar)
        if (auto ribbon_tabs = RibbonTabs())
        {
            for (auto const& item_obj : ribbon_tabs.Items())
            {
                if (auto item = item_obj.try_as<SelectorBarItem>())
                {
                    hstring tag = unbox_value_or<hstring>(item.Tag(), L"");
                    if (tag == L"file")
                    {
                        item.Text(Tr(L"Fichier", L"File"));
                    }
                    else if (tag == L"home")
                    {
                        item.Text(Tr(L"Accueil", L"Home"));
                    }
                    else if (tag == L"sendreceive")
                    {
                        item.Text(Tr(L"Envoi/réception", L"Send/Receive"));
                    }
                    else if (tag == L"folder")
                    {
                        item.Text(Tr(L"Dossier", L"Folder"));
                    }
                    else if (tag == L"view")
                    {
                        item.Text(Tr(L"Affichage", L"View"));
                    }
                }
            }
        }

        // 3. Portée de la recherche dans la barre de titre
        if (auto t = SearchScopeFolderText()) t.Text(Tr(L"Dossier courant", L"Current folder"));
        if (auto t = SearchScopeSubfoldersText()) t.Text(Tr(L"Sous-dossiers", L"Subfolders"));
        if (auto t = SearchScopeAccountText()) t.Text(Tr(L"Compte courant", L"Current account"));
        if (auto t = SearchScopeAllText()) t.Text(Tr(L"Tous les comptes", L"All accounts"));

        // 4. Filtres de la liste de messages (Prioritaire / Autres, Tous / Non lus / Lus)
        if (auto t = FocusedBarItemFocused()) t.Text(Tr(L"Prioritaire", L"Focused"));
        if (auto t = FocusedBarItemOther()) t.Text(Tr(L"Autres", L"Other"));
        if (auto t = ReadFilterAll()) t.Text(Tr(L"Tous", L"All"));
        if (auto t = ReadFilterUnread()) t.Text(Tr(L"Non lus", L"Unread"));
        if (auto t = ReadFilterRead()) t.Text(Tr(L"Lus", L"Read"));

        // 5. Boutons principaux du ruban Accueil (Mail)
        if (auto b = RibbonNewMessageBtn()) b.Label(Tr(L"Nouveau message", L"New message"));
        if (auto b = RibbonNewItemBtn()) b.Label(Tr(L"Nouvel élément", L"New item"));
        if (auto b = RibbonDeleteBtn()) b.Label(Tr(L"Supprimer", L"Delete"));
        if (auto b = RibbonJunkBtn()) b.Label(Tr(L"Indésirable", L"Junk"));
        if (auto b = RibbonArchiveBtn()) b.Label(Tr(L"Archiver", L"Archive"));
        if (auto b = RibbonMoveButton()) b.Label(Tr(L"Déplacer", L"Move"));
        if (auto b = RibbonReplyBtn()) b.Label(Tr(L"Répondre", L"Reply"));
        if (auto b = RibbonReplyAllBtn()) b.Label(Tr(L"Répondre à tous", L"Reply all"));
        if (auto b = RibbonForwardBtn()) b.Label(Tr(L"Transférer", L"Forward"));
        if (auto b = RibbonForwardAttachmentBtn()) b.Label(Tr(L"Transférer en PJ", L"Forward as attachment"));

        // Sous-éléments de 'Nouvel élément'
        if (auto m = RibbonNewItemMessage()) m.Text(Tr(L"Message", L"Message"));
        if (auto m = RibbonNewItemEvent()) m.Text(Tr(L"Rendez-vous", L"Appointment"));
        if (auto m = RibbonNewItemMeeting()) m.Text(Tr(L"Réunion (bientôt)", L"Meeting (soon)"));
        if (auto m = RibbonNewItemTask()) m.Text(Tr(L"Tâche", L"Task"));
        if (auto m = RibbonNewItemContact()) m.Text(Tr(L"Contact", L"Contact"));

        // 6. Navigation des Paramètres (titres, recherche, éléments de liste, fil d'Ariane)
        if (auto t = SettingsNavTitle()) t.Text(Tr(L"Paramètres", L"Settings"));
        if (auto s = SettingsSearchBox()) s.PlaceholderText(Tr(L"Rechercher un paramètre", L"Search setting"));
        if (auto h = SettingsBreadcrumbRoot()) h.Content(box_value(Tr(L"Paramètres", L"Settings")));

        if (auto nav_list = SettingsNavList())
        {
            for (auto const& item_obj : nav_list.Items())
            {
                if (auto item = item_obj.try_as<ListViewItem>())
                {
                    hstring tag = unbox_value_or<hstring>(item.Tag(), L"");
                    if (auto sp = item.Content().try_as<StackPanel>())
                    {
                        for (auto const& child : sp.Children())
                        {
                            if (auto tb = child.try_as<TextBlock>())
                            {
                                if (tag == L"accounts") tb.Text(Tr(L"Comptes", L"Accounts"));
                                else if (tag == L"mail") tb.Text(Tr(L"Courrier", L"Mail"));
                                else if (tag == L"calendar") tb.Text(Tr(L"Calendrier", L"Calendar"));
                                else if (tag == L"contacts") tb.Text(Tr(L"Contacts", L"Contacts"));
                                else if (tag == L"tasks") tb.Text(Tr(L"Tâches", L"Tasks"));
                                else if (tag == L"categories") tb.Text(Tr(L"Catégories", L"Categories"));
                                else if (tag == L"notifications") tb.Text(Tr(L"Notifications", L"Notifications"));
                                else if (tag == L"appearance") tb.Text(Tr(L"Apparence", L"Appearance"));
                                else if (tag == L"privacy") tb.Text(Tr(L"Confidentialité et sécurité", L"Privacy & Security"));
                                else if (tag == L"update") tb.Text(Tr(L"Mise à jour", L"Update"));
                                else if (tag == L"advanced") tb.Text(Tr(L"Avancé", L"Advanced"));
                                else if (tag == L"about") tb.Text(Tr(L"À propos", L"About"));
                            }
                        }
                    }
                }
            }
        }

        // 7. En-têtes fixes de la vue Paramètres > Comptes
        if (auto t = AddAccountCardTitle()) t.Text(Tr(L"Ajouter un compte de messagerie", L"Add a mail account"));
        if (auto t = AddAccountCardDesc()) t.Text(Tr(L"Configurez une nouvelle boîte aux lettres Exchange Online, Gmail, Outlook ou IMAP / SMTP.",
                                                     L"Set up a new Exchange Online, Gmail, Outlook or IMAP / SMTP mailbox."));
        if (auto t = AddAccountButtonText()) t.Text(Tr(L"Ajouter un compte", L"Add account"));
        if (auto t = ConfiguredAccountsHeader()) t.Text(Tr(L"Comptes configurés", L"Configured accounts"));
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
                box.PlaceholderText(Tr(L"Rechercher un contact (Ctrl+E)", L"Search contact (Ctrl+E)"));
            else if (tag == L"mail")
                UpdateSearchPlaceholder();
            else if (tag == L"tasks")
                box.PlaceholderText(Tr(L"Rechercher dans les tâches (Ctrl+E)", L"Search in tasks (Ctrl+E)"));
            else if (tag == L"calendar")
            {
                box.PlaceholderText(Tr(L"Rechercher un événement (Ctrl+E)", L"Search event (Ctrl+E)"));
                box.Text(m_calendar_search_query);
            }
            else
                box.PlaceholderText(Tr(L"Rechercher (Ctrl+E)", L"Search (Ctrl+E)"));
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
