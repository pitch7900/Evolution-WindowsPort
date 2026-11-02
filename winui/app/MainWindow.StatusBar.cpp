#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "Settings.h"

#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Shapes;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    /* Met à jour la barre de progression permanente et son texte indicatif.
     * Paramètres :
     *   active  : true si une opération de synchronisation est en cours, false si terminée
     *   message : libellé de l'opération en cours
     *   percent : pourcentage d'avancement (-1 pour indéterminé) */
    void MainWindow::UpdateStatusBarProgress(bool active, hstring const& message, int32_t percent)
    {
        if (active)
        {
            if (m_progress_hide_timer)
                m_progress_hide_timer.Stop();

            StatusBarProgressPanel().Visibility(Visibility::Visible);
            StatusBarProgressBar().IsIndeterminate(percent < 0);
            if (percent >= 0)
                StatusBarProgressBar().Value(percent);
            StatusBarProgressText().Text(message);
        }
        else
        {
            StatusBarProgressBar().IsIndeterminate(false);
            StatusBarProgressBar().Value(100);
            if (!message.empty())
                StatusBarProgressText().Text(message);

            if (!m_progress_hide_timer)
            {
                m_progress_hide_timer = DispatcherTimer();
                m_progress_hide_timer.Interval(std::chrono::milliseconds(2000));
                m_progress_hide_timer.Tick([this](IInspectable const&, IInspectable const&)
                {
                    StatusBarProgressPanel().Visibility(Visibility::Collapsed);
                    m_progress_hide_timer.Stop();
                });
            }
            m_progress_hide_timer.Stop();
            m_progress_hide_timer.Start();
        }
    }

    /* Met à jour l'état de connexion réseau d'un compte ou de la session globale.
     * Paramètres :
     *   account : identifiant du compte concerné (ou vide/global)
     *   status  : état reçu ("connected", "connecting", "offline", "auth-required", "error")
     *   message : explication ou libellé de l'état */
    void MainWindow::UpdateConnectionStatus(hstring const& account, hstring const& status, hstring const& message)
    {
        if (account.empty() || account == L"global")
        {
            if (status == L"offline")
                m_is_offline = true;
            else if (status == L"online" || status == L"connected")
                m_is_offline = false;
        }
        else
        {
            hstring account_name = account;
            for (auto const& val : m_accounts)
            {
                auto obj = val.GetObject();
                if (obj.GetNamedString(L"uid", L"") == account)
                {
                    account_name = obj.GetNamedString(L"name", account);
                    break;
                }
            }

            m_account_states[std::wstring{ account }] = AccountConnectionState{ account_name, status, message };
        }

        bool has_error = false;
        bool has_connecting = false;

        for (auto const& [key, state] : m_account_states)
        {
            if (state.status == L"error" || state.status == L"auth-required")
                has_error = true;
            else if (state.status == L"connecting")
                has_connecting = true;
        }

        if (m_is_offline)
        {
            ConnectionStatusDot().Fill(SolidColorBrush(Windows::UI::Color{ 255, 121, 119, 117 })); // Gris
            ConnectionStatusLabel().Text(L"Hors connexion");
            ToggleOfflineButton().Content(box_value(L"Se connecter"));
            FolderStatusText().Text(L"Mode hors connexion");
        }
        else if (has_error)
        {
            ConnectionStatusDot().Fill(SolidColorBrush(Windows::UI::Color{ 255, 232, 17, 35 })); // Rouge
            ConnectionStatusLabel().Text(L"Erreur de connexion");
            ToggleOfflineButton().Content(box_value(L"Travailler hors connexion"));
        }
        else if (has_connecting)
        {
            ConnectionStatusDot().Fill(SolidColorBrush(Windows::UI::Color{ 255, 216, 59, 1 })); // Orange
            ConnectionStatusLabel().Text(L"Connexion en cours…");
            ToggleOfflineButton().Content(box_value(L"Travailler hors connexion"));
        }
        else
        {
            ConnectionStatusDot().Fill(SolidColorBrush(Windows::UI::Color{ 255, 16, 124, 65 })); // Vert
            ConnectionStatusLabel().Text(L"Connecté");
            ToggleOfflineButton().Content(box_value(L"Travailler hors connexion"));
            UpdateFolderStatusText();
        }

        RefreshConnectionAccountsList();
    }

    /* Rafraîchit la liste des comptes affichée dans le menu déroulant de connexion. */
    void MainWindow::RefreshConnectionAccountsList()
    {
        ConnectionAccountsPanel().Children().Clear();

        if (m_accounts.Size() == 0)
        {
            TextBlock empty_text;
            empty_text.Text(L"Aucun compte configuré.");
            empty_text.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            ConnectionAccountsPanel().Children().Append(empty_text);
            return;
        }

        for (auto const& val : m_accounts)
        {
            auto obj = val.GetObject();
            hstring uid = obj.GetNamedString(L"uid", L"");
            hstring name = obj.GetNamedString(L"name", uid);
            bool is_local = obj.GetNamedBoolean(L"local", false);

            if (is_local)
                continue;

            auto it = m_account_states.find(std::wstring{ uid });
            hstring state_status = (it != m_account_states.end()) ? it->second.status : L"connected";
            hstring state_msg = (it != m_account_states.end() && !it->second.message.empty()) ? it->second.message : L"Connecté";

            Grid row;
            ColumnDefinition c0, c1, c2;
            c0.Width(GridLengthHelper::FromPixels(16));
            c1.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            c2.Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().Append(c0);
            row.ColumnDefinitions().Append(c1);
            row.ColumnDefinitions().Append(c2);

            Microsoft::UI::Xaml::Shapes::Ellipse dot;
            dot.Width(8);
            dot.Height(8);
            dot.VerticalAlignment(VerticalAlignment::Center);

            if (m_is_offline)
                dot.Fill(SolidColorBrush(Windows::UI::Color{ 255, 121, 119, 117 }));
            else if (state_status == L"error" || state_status == L"auth-required")
                dot.Fill(SolidColorBrush(Windows::UI::Color{ 255, 232, 17, 35 }));
            else if (state_status == L"connecting")
                dot.Fill(SolidColorBrush(Windows::UI::Color{ 255, 216, 59, 1 }));
            else
                dot.Fill(SolidColorBrush(Windows::UI::Color{ 255, 16, 124, 65 }));

            Grid::SetColumn(dot, 0);
            row.Children().Append(dot);

            StackPanel text_col;
            text_col.Margin(Thickness{ 6, 0, 8, 0 });
            text_col.VerticalAlignment(VerticalAlignment::Center);

            TextBlock name_block;
            name_block.Text(name);
            name_block.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
            text_col.Children().Append(name_block);

            TextBlock msg_block;
            msg_block.Text(m_is_offline ? L"Hors connexion" : state_msg);
            msg_block.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            msg_block.Opacity(0.8);
            text_col.Children().Append(msg_block);

            Grid::SetColumn(text_col, 1);
            row.Children().Append(text_col);

            if (!m_is_offline && (state_status == L"error" || state_status == L"auth-required"))
            {
                Button reconnect_btn;
                reconnect_btn.Content(box_value(L"Se reconnecter"));
                reconnect_btn.Padding(Thickness{ 8, 2, 8, 2 });
                reconnect_btn.Click([this, uid](IInspectable const&, RoutedEventArgs const&)
                {
                    ConnectionStatusFlyout().Hide();
                    if (m_core)
                        m_core->ReconnectAccount(uid);
                });
                Grid::SetColumn(reconnect_btn, 2);
                row.Children().Append(reconnect_btn);
            }

            ConnectionAccountsPanel().Children().Append(row);
        }
    }

    /* Clic sur le bouton d'état des connexions : rafraîchit la liste des comptes.
     * Paramètres :
     *   sender : bouton émetteur
     *   args   : arguments de routage */
    void MainWindow::OnConnectionStatusButtonClick(IInspectable const&, RoutedEventArgs const&)
    {
        RefreshConnectionAccountsList();
    }

    /* Bouton Réessayer du panneau : relance une vérification complète du courrier.
     * Paramètres :
     *   sender : bouton émetteur
     *   args   : arguments de routage */
    void MainWindow::OnRetryConnectionsClick(IInspectable const&, RoutedEventArgs const&)
    {
        ConnectionStatusFlyout().Hide();
        if (m_core)
            m_core->Refresh();
    }

    /* Bascule manuelle du mode hors connexion / en ligne.
     * Paramètres :
     *   sender : bouton émetteur
     *   args   : arguments de routage */
    void MainWindow::OnToggleOfflineClick(IInspectable const&, RoutedEventArgs const&)
    {
        ConnectionStatusFlyout().Hide();
        m_is_offline = !m_is_offline;
        if (m_core)
            m_core->SetOffline(m_is_offline);
        UpdateConnectionStatus(L"global", m_is_offline ? L"offline" : L"online",
            m_is_offline ? L"Mode hors connexion" : L"Connecté");
    }

    /* Met à jour le libellé d'état du dossier à gauche dans la barre d'état. */
    void MainWindow::UpdateFolderStatusText()
    {
        if (m_is_offline)
        {
            FolderStatusText().Text(L"Mode hors connexion");
            return;
        }

        if (m_account.empty() || m_folder.empty())
        {
            FolderStatusText().Text(L"Tous les dossiers sont à jour");
            return;
        }

        uint32_t count = m_all_messages ? m_all_messages.Size() : 0;
        if (count == 0)
        {
            FolderStatusText().Text(L"Aucun message");
        }
        else if (count == 1)
        {
            FolderStatusText().Text(L"1 message");
        }
        else
        {
            FolderStatusText().Text(to_hstring(count) + L" messages");
        }
    }
}
