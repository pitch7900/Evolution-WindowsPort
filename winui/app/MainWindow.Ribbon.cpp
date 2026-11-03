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

/* Ruban, barre d'accès rapide et bascule du mode confidentiel. */

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
     * @brief Met à jour l'affichage et l'état du ruban selon la vue active et l'onglet sélectionné.
     * 
     * Affiche ou masque le ruban pour Courrier, Calendrier, Contacts et Tâches.
     * Masque les onglets spécifiques au courrier (Envoi/réception et Dossier) dans les autres vues,
     * et affiche la barre de commandes appropriée selon l'onglet choisi.
     */
    void MainWindow::UpdateRibbon()
    {
        bool mail = m_view_tag == L"mail";
        bool calendar = m_view_tag == L"calendar";
        bool contacts = m_view_tag == L"contacts";
        bool tasks = m_view_tag == L"tasks";

        Ribbon().Visibility((mail || calendar || contacts || tasks) ? Visibility::Visible : Visibility::Collapsed);
        if (!mail && !calendar && !contacts && !tasks)
            return;

        RibbonSendReceiveTab().Visibility(mail ? Visibility::Visible : Visibility::Collapsed);
        RibbonFolderTab().Visibility(mail ? Visibility::Visible : Visibility::Collapsed);

        hstring tab = L"home";
        if (auto selected = RibbonTabs().SelectedItem())
            tab = unbox_value_or<hstring>(selected.Tag(), L"home");
        if (!mail && (tab == L"sendreceive" || tab == L"folder"))
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

        show(RibbonContactsFile(), contacts && tab == L"file");
        show(RibbonContactsHome(), contacts && tab == L"home");
        show(RibbonContactsView(), contacts && tab == L"view");

        show(RibbonTasksFile(), tasks && tab == L"file");
        show(RibbonTasksHome(), tasks && tab == L"home");
        show(RibbonTasksView(), tasks && tab == L"view");

        /* onglet Dossier : selon le dossier affiché (vue Courrier uniquement) */
        if (mail)
        {
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

        /* synchronisation de l'état du bouton bascule de la barre des tâches */
        bool todobar_checked = IsTodoBarVisible();
        RibbonMailTodoBarToggle().IsChecked(todobar_checked);
        RibbonCalendarTodoBarToggle().IsChecked(todobar_checked);
        if (auto mini_cal_toggle = RibbonMiniCalendarToggle())
            mini_cal_toggle.IsChecked(MonthCalendar() && MonthCalendar().Visibility() == Visibility::Visible);
        RibbonContactsTodoBarToggle().IsChecked(todobar_checked);
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
        Navigation().SelectedItem(Navigation().SettingsItem());
        SelectSettingsCategory(L"about");
    }

    void MainWindow::OnRibbonQuitClick(IInspectable const&, RoutedEventArgs const&)
    {
        Close();
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
        { L"archive", L"Archiver", L"\xE7B8", &MainWindow::OnArchiveClick },
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
}
