#include "pch.h"
#include "MainWindow.xaml.h"
#include "Settings.h"
#include "ErrorLog.h"

#include <shellapi.h>
#pragma comment(lib, "Shell32.lib")

#include <vector>
#include <string>
#include <algorithm>
#include <cwctype>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::EvolutionWin::implementation
{
    namespace
    {
        /**
         * @brief Structure décrivant un paramètre indexé pour la recherche rapide.
         */
        struct SettingSearchItem
        {
            std::wstring label;       ///< Libellé affiché dans la suggestion
            std::wstring description; ///< Description courte ou mot-clé
            std::wstring category;    ///< Identifiant de la catégorie cible
        };

        /**
         * @brief Table des paramètres indexés dans la recherche rapide.
         */
        const std::vector<SettingSearchItem> g_search_index = {
            { L"Comptes de messagerie", L"Gérer les comptes Exchange, Microsoft 365, Gmail et IMAP", L"accounts" },
            { L"Ajouter un compte", L"Configurer une nouvelle boîte aux lettres ou adresse e-mail", L"accounts" },
            { L"Intervalle de relève", L"Fréquence de vérification automatique du courrier", L"mail" },
            { L"Relève au démarrage", L"Synchroniser le courrier dès l'ouverture de l'application", L"mail" },
            { L"Format de rédaction", L"Format HTML ou texte brut par défaut des nouveaux messages", L"mail" },
            { L"Marquer comme lu", L"Délai avant de marquer automatiquement un message comme lu", L"mail" },
            { L"Vider la corbeille", L"Nettoyer les éléments supprimés en quittant l'application", L"mail" },
            { L"Signatures de courrier", L"Créer et modifier des signatures texte ou HTML avec images", L"mail" },
            { L"Signature par compte", L"Attribuer une signature par défaut pour chaque compte", L"mail" },
            { L"Avertir si objet vide", L"Demander confirmation avant d'envoyer un message sans objet", L"mail" },
            { L"Suppression définitive", L"Confirmation avant la suppression irréversible d'un message", L"mail" },
            { L"Premier jour de la semaine", L"Lundi, dimanche ou samedi pour la grille du calendrier", L"calendar" },
            { L"Heures de travail", L"Définir les heures habituelles de bureau pour le calendrier", L"calendar" },
            { L"Numéros de semaine", L"Afficher les numéros de semaine dans le calendrier", L"calendar" },
            { L"Durée par défaut des réunions", L"Durée préremplie lors de la création d'un événement", L"calendar" },
            { L"Rappel par défaut des événements", L"Délai d'alerte pour les nouveaux rendez-vous", L"calendar" },
            { L"Format des contacts", L"Ordre d'affichage des noms et prénoms dans le carnet d'adresses", L"contacts" },
            { L"Tri des contacts", L"Trier le carnet d'adresses par nom de famille ou prénom", L"contacts" },
            { L"Tâches terminées", L"Afficher ou masquer les tâches achevées par défaut", L"tasks" },
            { L"Échéance des tâches", L"Date d'échéance par défaut des nouvelles tâches", L"tasks" },
            { L"Rappels des tâches", L"Alerte de rappel par défaut des tâches", L"tasks" },
            { L"Notifications Windows (Toasts)", L"Afficher des bannières toast pour les nouveaux messages", L"notifications" },
            { L"Signal sonore de notification", L"Émettre un son lors de la réception d'un e-mail", L"notifications" },
            { L"Badge de la barre des tâches", L"Compteur de messages non lus sur l'icône de l'application", L"notifications" },
            { L"Mode confidentiel des notifications", L"Masquer l'expéditeur et l'objet dans les alertes", L"notifications" },
            { L"Thème de l'application", L"Mode sombre, mode clair ou thème du système Windows 11", L"appearance" },
            { L"Vue au démarrage", L"Afficher le Courrier, le Calendrier, les Contacts ou les Tâches au lancement", L"appearance" },
            { L"Taille de police", L"Ajuster la taille de texte pour la lecture des courriels", L"appearance" },
            { L"Zone de notification (Systray)", L"Réduire dans la zone de notification Windows à la fermeture", L"appearance" },
            { L"Contenu externe et images distantes", L"Bloquer ou autoriser les images et pixels de suivi", L"privacy" },
            { L"Expéditeurs de confiance", L"Gérer la liste blanche des expéditeurs autorisés", L"privacy" },
            { L"Masquer l'extrait des messages", L"Ne pas afficher les lignes d'aperçu dans la liste", L"privacy" },
            { L"Mise à jour automatique", L"Vérifier la disponibilité d'une nouvelle version au démarrage", L"update" },
            { L"Canal de mise à jour", L"Choisir entre la version stable et les versions bêta préliminaires", L"update" },
            { L"Vérifier les mises à jour", L"Rechercher immédiatement une nouvelle version sur GitHub", L"update" },
            { L"Dossier des données locales", L"Ouvrir le répertoire des réglages et profils dans l'Explorateur", L"advanced" },
            { L"Cache WebView2", L"Vider les fichiers temporaires et le cache de rendu HTML", L"advanced" },
            { L"Journalisation détaillée", L"Activer les journaux étendus pour le diagnostic et le support", L"advanced" },
            { L"Rétablir les paramètres par défaut", L"Réinitialiser l'ensemble des réglages de l'application", L"advanced" },
            { L"À propos d'EvolutionWin", L"Numéro de version, licences open-source et informations légales", L"about" }
        };

        /**
         * @brief Convertit une chaîne en minuscules pour la comparaison insensible à la casse.
         * @param str Chaîne source.
         * @return Chaîne en minuscules.
         */
        std::wstring ToLowerString(std::wstring const& str)
        {
            std::wstring result = str;
            std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) {
                return static_cast<wchar_t>(std::towlower(c));
            });
            return result;
        }
    }

    /**
     * @brief Initialise la vue Paramètres en chargeant les valeurs persistées dans settings.json.
     *        Positionne les interrupteurs (ToggleSwitch) et listes déroulantes (ComboBox)
     *        sans déclencher de réenregistrement intempestif.
     */
    void MainWindow::InitSettingsView()
    {
        if (m_settings_initialized)
            return;

        m_updating_settings_view = true;
        auto& settings = ::EvolutionWin::Settings::Instance();

        /* 1. Courrier */
        // Intervalle de relève (défaut : 5 minutes)
        hstring sync_interval = settings.GetString(L"mail.sync-interval", L"5");
        if (auto combo = MailSyncIntervalCombo())
        {
            int32_t sel = 1; // 5 min par défaut
            for (uint32_t i = 0; i < combo.Items().Size(); ++i)
            {
                if (auto item = combo.Items().GetAt(i).try_as<ComboBoxItem>())
                {
                    if (unbox_value_or<hstring>(item.Tag(), L"") == sync_interval)
                    {
                        sel = static_cast<int32_t>(i);
                        break;
                    }
                }
            }
            combo.SelectedIndex(sel);
        }

        if (auto sw = MailSyncOnStartupSwitch())
            sw.IsOn(settings.GetBoolean(L"mail.sync-on-startup", true));

        // Marquer comme lu
        hstring mark_read = settings.GetString(L"mail.mark-as-read", L"immediate");
        if (auto combo = MailMarkAsReadCombo())
        {
            int32_t sel = 0;
            for (uint32_t i = 0; i < combo.Items().Size(); ++i)
            {
                if (auto item = combo.Items().GetAt(i).try_as<ComboBoxItem>())
                {
                    if (unbox_value_or<hstring>(item.Tag(), L"") == mark_read)
                    {
                        sel = static_cast<int32_t>(i);
                        break;
                    }
                }
            }
            combo.SelectedIndex(sel);
        }

        if (auto sw = MailEmptyTrashOnExitSwitch())
            sw.IsOn(settings.GetBoolean(L"mail.empty-trash-on-exit", false));

        // Format par défaut (HTML / texte brut)
        hstring def_format = settings.GetString(L"mail.default-format", L"html");
        if (auto combo = MailDefaultFormatCombo())
            combo.SelectedIndex(def_format == L"text" ? 1 : 0);

        if (auto sw = MailWarnEmptySubjectSwitch())
            sw.IsOn(settings.GetBoolean(L"mail.warn-empty-subject", true));

        if (auto sw = MailConfirmPermanentDeleteSwitch())
            sw.IsOn(settings.GetBoolean(L"mail.confirm-permanent-delete", true));

        /* 2. Calendrier */
        // Premier jour de la semaine
        hstring first_day = settings.GetString(L"calendar.first-day", L"0");
        if (auto combo = CalendarFirstDayCombo())
        {
            int32_t sel = 0;
            for (uint32_t i = 0; i < combo.Items().Size(); ++i)
            {
                if (auto item = combo.Items().GetAt(i).try_as<ComboBoxItem>())
                {
                    if (unbox_value_or<hstring>(item.Tag(), L"") == first_day)
                    {
                        sel = static_cast<int32_t>(i);
                        break;
                    }
                }
            }
            combo.SelectedIndex(sel);
        }

        // Jours de travail (jours ouvrés)
        hstring work_days = settings.GetString(L"calendar.work-days", L"1,2,3,4,5");
        std::wstring work_days_str{ work_days };
        auto is_workday = [&](std::wstring const& d) {
            return work_days_str.find(d) != std::wstring::npos;
        };
        if (auto btn = WorkDayMon()) btn.IsChecked(is_workday(L"1"));
        if (auto btn = WorkDayTue()) btn.IsChecked(is_workday(L"2"));
        if (auto btn = WorkDayWed()) btn.IsChecked(is_workday(L"3"));
        if (auto btn = WorkDayThu()) btn.IsChecked(is_workday(L"4"));
        if (auto btn = WorkDayFri()) btn.IsChecked(is_workday(L"5"));
        if (auto btn = WorkDaySat()) btn.IsChecked(is_workday(L"6"));
        if (auto btn = WorkDaySun()) btn.IsChecked(is_workday(L"0"));

        // Heures de travail (début et fin)
        hstring work_start = settings.GetString(L"calendar.work-start", L"8");
        if (auto combo = CalendarWorkStartCombo())
        {
            int32_t sel = 2; // 08:00
            for (uint32_t i = 0; i < combo.Items().Size(); ++i)
            {
                if (auto item = combo.Items().GetAt(i).try_as<ComboBoxItem>())
                {
                    if (unbox_value_or<hstring>(item.Tag(), L"") == work_start)
                    {
                        sel = static_cast<int32_t>(i);
                        break;
                    }
                }
            }
            combo.SelectedIndex(sel);
        }

        hstring work_end = settings.GetString(L"calendar.work-end", L"18");
        if (auto combo = CalendarWorkEndCombo())
        {
            int32_t sel = 3; // 18:00
            for (uint32_t i = 0; i < combo.Items().Size(); ++i)
            {
                if (auto item = combo.Items().GetAt(i).try_as<ComboBoxItem>())
                {
                    if (unbox_value_or<hstring>(item.Tag(), L"") == work_end)
                    {
                        sel = static_cast<int32_t>(i);
                        break;
                    }
                }
            }
            combo.SelectedIndex(sel);
        }

        // Numéros de semaine
        if (auto sw = CalendarShowWeekNumbersSwitch())
            sw.IsOn(settings.GetBoolean(L"calendar.show-week-numbers", false));

        // Fuseau horaire principal
        hstring timezone = settings.GetString(L"calendar.timezone", L"system");
        if (auto combo = CalendarTimezoneCombo())
        {
            int32_t sel = 0;
            for (uint32_t i = 0; i < combo.Items().Size(); ++i)
            {
                if (auto item = combo.Items().GetAt(i).try_as<ComboBoxItem>())
                {
                    if (unbox_value_or<hstring>(item.Tag(), L"") == timezone)
                    {
                        sel = static_cast<int32_t>(i);
                        break;
                    }
                }
            }
            combo.SelectedIndex(sel);
        }

        // Second fuseau horaire
        if (auto sw = CalendarSecondTimezoneSwitch())
            sw.IsOn(settings.GetBoolean(L"calendar.second-timezone-enabled", false));

        hstring second_tz = settings.GetString(L"calendar.second-timezone", L"UTC");
        if (auto combo = CalendarSecondTimezoneCombo())
        {
            int32_t sel = 0;
            for (uint32_t i = 0; i < combo.Items().Size(); ++i)
            {
                if (auto item = combo.Items().GetAt(i).try_as<ComboBoxItem>())
                {
                    if (unbox_value_or<hstring>(item.Tag(), L"") == second_tz)
                    {
                        sel = static_cast<int32_t>(i);
                        break;
                    }
                }
            }
            combo.SelectedIndex(sel);
        }

        hstring def_duration = settings.GetString(L"calendar.default-duration", L"30");
        if (auto combo = CalendarDefaultDurationCombo())
        {
            int32_t sel = 1; // 30 min
            for (uint32_t i = 0; i < combo.Items().Size(); ++i)
            {
                if (auto item = combo.Items().GetAt(i).try_as<ComboBoxItem>())
                {
                    if (unbox_value_or<hstring>(item.Tag(), L"") == def_duration)
                    {
                        sel = static_cast<int32_t>(i);
                        break;
                    }
                }
            }
            combo.SelectedIndex(sel);
        }

        hstring def_reminder = settings.GetString(L"calendar.default-reminder", L"15");
        if (auto combo = CalendarDefaultReminderCombo())
        {
            int32_t sel = 2; // 15 min
            for (uint32_t i = 0; i < combo.Items().Size(); ++i)
            {
                if (auto item = combo.Items().GetAt(i).try_as<ComboBoxItem>())
                {
                    if (unbox_value_or<hstring>(item.Tag(), L"") == def_reminder)
                    {
                        sel = static_cast<int32_t>(i);
                        break;
                    }
                }
            }
            combo.SelectedIndex(sel);
        }

        /* 3. Contacts */
        hstring name_format = settings.GetString(L"contacts.name-format", L"first-last");
        if (auto combo = ContactsNameFormatCombo())
            combo.SelectedIndex(name_format == L"last-first" ? 1 : 0);

        hstring sort_order = settings.GetString(L"contacts.sort-order", L"last");
        if (auto combo = ContactsSortOrderCombo())
            combo.SelectedIndex(sort_order == L"first" ? 1 : 0);

        /* 4. Tâches */
        if (auto sw = TasksShowCompletedSwitch())
            sw.IsOn(settings.GetBoolean(L"tasks.show-completed", true));

        hstring def_due = settings.GetString(L"tasks.default-due", L"none");
        if (auto combo = TasksDefaultDueCombo())
        {
            int32_t sel = 0;
            for (uint32_t i = 0; i < combo.Items().Size(); ++i)
            {
                if (auto item = combo.Items().GetAt(i).try_as<ComboBoxItem>())
                {
                    if (unbox_value_or<hstring>(item.Tag(), L"") == def_due)
                    {
                        sel = static_cast<int32_t>(i);
                        break;
                    }
                }
            }
            combo.SelectedIndex(sel);
        }

        /* 5. Notifications */
        if (auto sw = NotificationsToastSwitch())
            sw.IsOn(settings.GetBoolean(L"notifications.toast", true));

        if (auto sw = NotificationsPlaySoundSwitch())
            sw.IsOn(settings.GetBoolean(L"notifications.sound", true));

        if (auto sw = NotificationsTaskbarBadgeSwitch())
            sw.IsOn(settings.GetBoolean(L"notifications.taskbar-badge", true));

        /* 6. Apparence */
        hstring theme = settings.GetString(L"appearance.theme", L"system");
        if (auto radio = AppearanceThemeChoice())
            radio.SelectedIndex(theme == L"light" ? 1 : (theme == L"dark" ? 2 : 0));

        hstring startup_view = settings.GetString(L"appearance.startup-view", L"mail");
        if (auto combo = AppearanceStartupViewCombo())
        {
            int32_t sel = 0;
            for (uint32_t i = 0; i < combo.Items().Size(); ++i)
            {
                if (auto item = combo.Items().GetAt(i).try_as<ComboBoxItem>())
                {
                    if (unbox_value_or<hstring>(item.Tag(), L"") == startup_view)
                    {
                        sel = static_cast<int32_t>(i);
                        break;
                    }
                }
            }
            combo.SelectedIndex(sel);
        }

        hstring font_size = settings.GetString(L"appearance.font-size", L"normal");
        if (auto combo = AppearanceFontSizeCombo())
        {
            int32_t sel = 1; // normal
            for (uint32_t i = 0; i < combo.Items().Size(); ++i)
            {
                if (auto item = combo.Items().GetAt(i).try_as<ComboBoxItem>())
                {
                    if (unbox_value_or<hstring>(item.Tag(), L"") == font_size)
                    {
                        sel = static_cast<int32_t>(i);
                        break;
                    }
                }
            }
            combo.SelectedIndex(sel);
        }

        if (auto sw = AppearanceCompactDensitySwitch())
            sw.IsOn(settings.GetBoolean(L"layout.compact-density", false));

        if (auto sw = AppearanceMinimizeToTraySwitch())
            sw.IsOn(settings.GetBoolean(L"appearance.minimize-to-tray", false));

        /* 7. Confidentialité */
        if (auto sw = PrivacyHideSnippetsSwitch())
            sw.IsOn(settings.GetBoolean(L"privacy.hide-snippets", false));

        /* 8. Mise à jour */
        if (auto sw = UpdateAutoCheckSwitch())
            sw.IsOn(settings.GetBoolean(L"update.auto-check", true));

        hstring channel = settings.GetString(L"update.channel", L"stable");
        if (auto combo = UpdateChannelCombo())
            combo.SelectedIndex(channel == L"beta" ? 1 : 0);

        /* 9. Avancé */
        if (auto sw = AdvancedVerboseLogSwitch())
            sw.IsOn(settings.GetBoolean(L"advanced.verbose-log", false));

        // Sélectionner par défaut la catégorie Comptes dans la liste de gauche
        if (auto nav = SettingsNavList())
        {
            if (nav.SelectedIndex() < 0)
                nav.SelectedIndex(0);
        }

        m_updating_settings_view = false;
        m_settings_initialized = true;
    }

    /**
     * @brief Affiche la catégorie de paramètres spécifiée et adapte l'interface.
     * @param category_tag Identifiant de la catégorie (ex. "accounts", "mail", "calendar", etc.).
     */
    void MainWindow::SelectSettingsCategory(winrt::hstring const& category_tag)
    {
        m_current_settings_category = category_tag;

        // Masquer toutes les sections
        auto hide_section = [](UIElement const& el) {
            if (el) el.Visibility(Visibility::Collapsed);
        };
        hide_section(SettingsCategoryAccounts());
        hide_section(SettingsCategoryMail());
        hide_section(SettingsCategoryCalendar());
        hide_section(SettingsCategoryContacts());
        hide_section(SettingsCategoryTasks());
        hide_section(SettingsCategoryNotifications());
        hide_section(SettingsCategoryAppearance());
        hide_section(SettingsCategoryPrivacy());
        hide_section(SettingsCategoryUpdate());
        hide_section(SettingsCategoryAdvanced());
        hide_section(SettingsCategoryAbout());

        // Met à jour les libellés et affiche la section demandée
        hstring title = L"Paramètres";
        hstring desc = L"";

        if (category_tag == L"accounts")
        {
            title = L"Comptes";
            desc = L"Gérez vos comptes de messagerie Microsoft 365, Exchange, Gmail et IMAP / SMTP.";
            if (auto s = SettingsCategoryAccounts()) s.Visibility(Visibility::Visible);
            RefreshAccountsView();
        }
        else if (category_tag == L"mail")
        {
            title = L"Courrier";
            desc = L"Paramètres de réception, lecture, rédaction de messages et gestion des signatures.";
            if (auto s = SettingsCategoryMail()) s.Visibility(Visibility::Visible);
            RefreshSignaturesView();
        }
        else if (category_tag == L"calendar")
        {
            title = L"Calendrier";
            desc = L"Options d'affichage de la semaine, heures de travail, rappels et durées par défaut.";
            if (auto s = SettingsCategoryCalendar()) s.Visibility(Visibility::Visible);
        }
        else if (category_tag == L"contacts")
        {
            title = L"Contacts";
            desc = L"Format d'affichage des noms et ordre de tri des fiches de contacts.";
            if (auto s = SettingsCategoryContacts()) s.Visibility(Visibility::Visible);
        }
        else if (category_tag == L"tasks")
        {
            title = L"Tâches";
            desc = L"Affichage des tâches achevées, échéances et rappels automatiques.";
            if (auto s = SettingsCategoryTasks()) s.Visibility(Visibility::Visible);
        }
        else if (category_tag == L"notifications")
        {
            title = L"Notifications";
            desc = L"Alertes Windows (toasts), signaux sonores, pastille de la barre des tâches et mode confidentiel.";
            if (auto s = SettingsCategoryNotifications()) s.Visibility(Visibility::Visible);
        }
        else if (category_tag == L"appearance")
        {
            title = L"Apparence";
            desc = L"Personnalisation du thème visuel, taille des polices de lecture et comportement au démarrage.";
            if (auto s = SettingsCategoryAppearance()) s.Visibility(Visibility::Visible);
        }
        else if (category_tag == L"privacy")
        {
            title = L"Confidentialité et sécurité";
            desc = L"Protection contre les pixels espions, blocage du contenu distant et liste de confiance.";
            if (auto s = SettingsCategoryPrivacy()) s.Visibility(Visibility::Visible);
            RefreshPrivacyView();
        }
        else if (category_tag == L"update")
        {
            title = L"Mise à jour";
            desc = L"Recherche automatique de nouvelles versions et canal de distribution du logiciel.";
            if (auto s = SettingsCategoryUpdate()) s.Visibility(Visibility::Visible);
        }
        else if (category_tag == L"advanced")
        {
            title = L"Avancé";
            desc = L"Gestion des données locales, vidage du cache de rendu et réinitialisation des paramètres.";
            if (auto s = SettingsCategoryAdvanced()) s.Visibility(Visibility::Visible);
        }
        else if (category_tag == L"about")
        {
            title = L"À propos";
            desc = L"Informations sur le portage Windows natif d'Evolution, technologies et mentions légales.";
            if (auto s = SettingsCategoryAbout()) s.Visibility(Visibility::Visible);
        }

        // Mettre à jour le fil d'Ariane et les en-têtes
        if (auto b = SettingsBreadcrumbCategory()) b.Text(title);
        if (auto t = SettingsCategoryTitle()) t.Text(title);
        if (auto d = SettingsCategoryDescription()) d.Text(desc);

        // Synchroniser la sélection dans la ListView de navigation
        if (auto nav = SettingsNavList())
        {
            for (uint32_t i = 0; i < nav.Items().Size(); ++i)
            {
                if (auto item = nav.Items().GetAt(i).try_as<ListViewItem>())
                {
                    if (unbox_value_or<hstring>(item.Tag(), L"") == category_tag)
                    {
                        if (nav.SelectedIndex() != static_cast<int32_t>(i))
                            nav.SelectedIndex(static_cast<int32_t>(i));
                        break;
                    }
                }
            }
        }
    }

    /**
     * @brief Gestionnaire déclenché lors du changement de catégorie dans la barre latérale.
     * @param sender Contrôle ListView émetteur.
     * @param args Arguments de l'événement de sélection.
     */
    void MainWindow::OnSettingsNavSelectionChanged(
        [[maybe_unused]] IInspectable const& sender,
        SelectionChangedEventArgs const& args)
    {
        if (args.AddedItems().Size() == 0)
            return;

        if (auto item = args.AddedItems().GetAt(0).try_as<ListViewItem>())
        {
            hstring tag = unbox_value_or<hstring>(item.Tag(), L"accounts");
            SelectSettingsCategory(tag);
        }
    }

    /**
     * @brief Gestionnaire de clic sur le lien racine « Paramètres » dans le fil d'Ariane.
     * @param sender Contrôle émetteur.
     * @param args Arguments d'événement.
     */
    void MainWindow::OnSettingsBreadcrumbRootClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        SelectSettingsCategory(L"accounts");
    }

    /**
     * @brief Raccourci clavier global Ctrl+, ouvrant directement les Paramètres.
     * @param sender Accélérateur clavier émetteur.
     * @param args Arguments d'invocation de l'accélérateur.
     */
    void MainWindow::OnSettingsAccelerator(
        [[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        if (auto nav = Navigation())
            nav.SelectedItem(nav.SettingsItem());
    }

    /**
     * @brief Gestionnaire de recherche rapide filtrant les réglages selon le mot-clé saisi.
     * @param sender Boîte de recherche AutoSuggestBox.
     * @param args Arguments de modification de texte.
     */
    void MainWindow::OnSettingsSearchTextChanged(
        AutoSuggestBox const& sender,
        AutoSuggestBoxTextChangedEventArgs const& args)
    {
        if (args.Reason() != AutoSuggestionBoxTextChangeReason::UserInput)
            return;

        std::wstring query = ToLowerString(sender.Text().c_str());
        if (query.empty())
        {
            sender.ItemsSource(nullptr);
            return;
        }

        std::vector<hstring> suggestions;
        for (auto const& item : g_search_index)
        {
            std::wstring label_lower = ToLowerString(item.label);
            std::wstring desc_lower = ToLowerString(item.description);

            if (label_lower.find(query) != std::wstring::npos || desc_lower.find(query) != std::wstring::npos)
            {
                suggestions.push_back(hstring{ item.label });
                if (suggestions.size() >= 8)
                    break;
            }
        }

        sender.ItemsSource(winrt::single_threaded_observable_vector<hstring>(std::move(suggestions)));
    }

    /**
     * @brief Gestionnaire déclenché lors du clic sur une suggestion de recherche.
     * @param sender Boîte de recherche AutoSuggestBox.
     * @param args Arguments contenant la suggestion choisie.
     */
    void MainWindow::OnSettingsSearchSuggestionChosen(
        AutoSuggestBox const& sender,
        AutoSuggestBoxSuggestionChosenEventArgs const& args)
    {
        hstring chosen = unbox_value_or<hstring>(args.SelectedItem(), L"");
        if (chosen.empty())
            return;

        for (auto const& item : g_search_index)
        {
            if (item.label == chosen.c_str())
            {
                SelectSettingsCategory(hstring{ item.category });
                sender.Text(L"");
                break;
            }
        }
    }

    /**
     * @brief Gestionnaire de changement de thème de l'application (Système, Clair, Sombre).
     * @param sender Contrôle de choix du thème.
     * @param args Arguments de sélection.
     */
    void MainWindow::OnThemeChoiceChanged(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] SelectionChangedEventArgs const& args)
    {
        if (m_updating_settings_view)
            return;

        int32_t index = AppearanceThemeChoice().SelectedIndex();
        hstring theme = index == 1 ? L"light" : (index == 2 ? L"dark" : L"system");
        ::EvolutionWin::Settings::Instance().SetString(L"appearance.theme", theme);
        ApplyTheme(theme);
    }

    /**
     * @brief Applique dynamiquement le thème sélectionné sur la fenêtre principale.
     * @param theme_name Nom du thème (L"system", L"light", L"dark").
     */
    void MainWindow::ApplyTheme(winrt::hstring const& theme_name)
    {
        if (auto root = this->Content().try_as<FrameworkElement>())
        {
            if (theme_name == L"light")
                root.RequestedTheme(ElementTheme::Light);
            else if (theme_name == L"dark")
                root.RequestedTheme(ElementTheme::Dark);
            else
                root.RequestedTheme(ElementTheme::Default);
        }
    }

    /**
     * @brief Gestionnaire universel de modification d'un interrupteur (ToggleSwitch).
     * @param sender Contrôle ToggleSwitch émetteur.
     * @param args Arguments de l'événement.
     */
    void MainWindow::OnSettingToggleChanged(
        IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        if (m_updating_settings_view)
            return;

        if (auto toggle = sender.try_as<ToggleSwitch>())
        {
            hstring key = unbox_value_or<hstring>(toggle.Tag(), L"");
            if (!key.empty())
            {
                ::EvolutionWin::Settings::Instance().SetBoolean(key, toggle.IsOn());
                if (key == L"layout.compact-density")
                {
                    ApplyCompactDensity(toggle.IsOn());
                }
                else if (std::wstring_view{ key }.rfind(L"calendar.", 0) == 0)
                {
                    ApplyCalendarPreferences();
                }
            }
        }
    }

    /**
     * @brief Gestionnaire universel de modification d'une liste déroulante (ComboBox).
     * @param sender Contrôle ComboBox émetteur.
     * @param args Arguments de sélection.
     */
    void MainWindow::OnSettingComboChanged(
        IInspectable const& sender,
        [[maybe_unused]] SelectionChangedEventArgs const& args)
    {
        if (m_updating_settings_view)
            return;

        if (auto combo = sender.try_as<ComboBox>())
        {
            hstring key = unbox_value_or<hstring>(combo.Tag(), L"");
            if (!key.empty())
            {
                if (auto item = combo.SelectedItem().try_as<ComboBoxItem>())
                {
                    hstring val = unbox_value_or<hstring>(item.Tag(), L"");
                    if (!val.empty())
                    {
                        ::EvolutionWin::Settings::Instance().SetString(key, val);
                        if (std::wstring_view{ key }.rfind(L"calendar.", 0) == 0)
                        {
                            ApplyCalendarPreferences();
                        }
                    }
                }
            }
        }
    }

    /**
     * @brief Gestionnaire déclenchant la recherche manuelle immédiate de mise à jour.
     * @param sender Contrôle bouton émetteur.
     * @param args Arguments de clic.
     */
    void MainWindow::OnCheckUpdateNowClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        ShowUpdateDialogAsync();
    }

    /**
     * @brief Ouvre le répertoire des données locales %LOCALAPPDATA%\\EvolutionWin dans l'Explorateur Windows.
     * @param sender Contrôle bouton émetteur.
     * @param args Arguments de clic.
     */
    void MainWindow::OnOpenDataFolderClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        wchar_t local[MAX_PATH]{};
        GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
        std::wstring dir = std::wstring{ local } + L"\\EvolutionWin";
        CreateDirectoryW(dir.c_str(), nullptr);
        ShellExecuteW(nullptr, L"explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    /**
     * @brief Vide le cache WebView2 et affiche une confirmation discrète.
     * @param sender Contrôle bouton émetteur.
     * @param args Arguments de clic.
     */
    void MainWindow::OnClearWebViewCacheClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        ShowStatus(L"Le cache temporaire de rendu et les données locales ont été vidés.", InfoBarSeverity::Success);
    }

    /**
     * @brief Affiche une boîte de dialogue de confirmation pour réinitialiser tous les paramètres.
     * @param sender Contrôle bouton émetteur.
     * @param args Arguments de clic.
     */
    void MainWindow::OnResetAllSettingsClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        ContentDialog dialog;
        dialog.XamlRoot(this->Content().XamlRoot());
        dialog.Title(box_value(L"Réinitialiser les paramètres ?"));
        dialog.Content(box_value(L"Tous les réglages personnalisés de l'application (thème, intervalles, options de lecture) seront rétablis à leurs valeurs d'origine.\n\nVos comptes de messagerie et vos courriels ne seront pas supprimés."));
        dialog.PrimaryButtonText(L"Réinitialiser");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Close);

        dialog.PrimaryButtonClick([this](ContentDialog const&, ContentDialogButtonClickEventArgs const&)
        {
            // Réinitialise les clés principales
            auto& s = ::EvolutionWin::Settings::Instance();
            s.SetString(L"mail.sync-interval", L"5");
            s.SetBoolean(L"mail.sync-on-startup", true);
            s.SetString(L"mail.mark-as-read", L"immediate");
            s.SetBoolean(L"mail.empty-trash-on-exit", false);
            s.SetString(L"mail.default-format", L"html");
            s.SetBoolean(L"mail.warn-empty-subject", true);
            s.SetBoolean(L"mail.confirm-permanent-delete", true);
            s.SetString(L"calendar.first-day", L"0");
            s.SetBoolean(L"calendar.show-week-numbers", false);
            s.SetString(L"calendar.default-duration", L"30");
            s.SetString(L"calendar.default-reminder", L"15");
            s.SetString(L"contacts.name-format", L"first-last");
            s.SetString(L"contacts.sort-order", L"last");
            s.SetBoolean(L"tasks.show-completed", true);
            s.SetString(L"tasks.default-due", L"none");
            s.SetBoolean(L"notifications.toast", true);
            s.SetBoolean(L"notifications.sound", true);
            s.SetBoolean(L"notifications.taskbar-badge", true);
            s.SetString(L"appearance.theme", L"system");
            s.SetString(L"appearance.startup-view", L"mail");
            s.SetString(L"appearance.font-size", L"normal");
            s.SetBoolean(L"appearance.minimize-to-tray", false);
            s.SetBoolean(L"privacy.hide-snippets", false);
            s.SetBoolean(L"update.auto-check", true);
            s.SetString(L"update.channel", L"stable");
            s.SetBoolean(L"advanced.verbose-log", false);

            ApplyTheme(L"system");
            m_settings_initialized = false;
            InitSettingsView();
            ShowStatus(L"Les paramètres par défaut de l'application ont été rétablis.", InfoBarSeverity::Success);
        });

        dialog.ShowAsync();
    }

    /**
     * @brief Gestionnaire de bascule des jours de travail (Lun, Mar, Mer, Jeu, Ven, Sam, Dim).
     * @param sender Bouton ToggleButton cliqué.
     * @param args Arguments d'événement.
     */
    void MainWindow::OnCalendarWorkDayToggled(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        if (m_updating_settings_view)
            return;

        std::wstring days;
        if (WorkDayMon() && WorkDayMon().IsChecked().Value()) days += L"1,";
        if (WorkDayTue() && WorkDayTue().IsChecked().Value()) days += L"2,";
        if (WorkDayWed() && WorkDayWed().IsChecked().Value()) days += L"3,";
        if (WorkDayThu() && WorkDayThu().IsChecked().Value()) days += L"4,";
        if (WorkDayFri() && WorkDayFri().IsChecked().Value()) days += L"5,";
        if (WorkDaySat() && WorkDaySat().IsChecked().Value()) days += L"6,";
        if (WorkDaySun() && WorkDaySun().IsChecked().Value()) days += L"0,";

        if (!days.empty() && days.back() == L',')
            days.pop_back();

        ::EvolutionWin::Settings::Instance().SetString(L"calendar.work-days", hstring{ days });
        ApplyCalendarPreferences();
    }

    /**
     * @brief Applique dynamiquement les préférences du calendrier (premier jour, numéros de semaine, vue).
     */
    void MainWindow::ApplyCalendarPreferences()
    {
        auto& settings = ::EvolutionWin::Settings::Instance();

        // 1. Premier jour de la semaine sur le mini-calendrier
        if (auto cal = MonthCalendar())
        {
            hstring first_day = settings.GetString(L"calendar.first-day", L"0");
            if (first_day == L"1")
                cal.FirstDayOfWeek(Windows::Globalization::DayOfWeek::Sunday);
            else if (first_day == L"2")
                cal.FirstDayOfWeek(Windows::Globalization::DayOfWeek::Saturday);
            else
                cal.FirstDayOfWeek(Windows::Globalization::DayOfWeek::Monday);
        }

        // 2. Si la vue calendrier est active, recharger pour actualiser la grille
        if (m_view_tag == L"calendar")
        {
            LoadEvents();
        }
    }
}
