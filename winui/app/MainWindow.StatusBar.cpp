#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "Settings.h"

#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <algorithm>
#include <objidl.h>
#include <gdiplus.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")

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
    /**
     * @brief Met à jour l'état et l'avancement de la progression sur l'icône de la barre des tâches Windows (ITaskbarList3).
     * @param active true si une tâche de fond est en cours, false si terminée.
     * @param percent Pourcentage d'avancement (0 à 100), ou -1 pour indéterminé.
     * @param error true pour afficher l'indicateur d'erreur rouge.
     */
    void MainWindow::UpdateTaskbarProgress(bool active, int32_t percent, bool error)
    {
        if (!m_initialized)
            return;

        HWND hwnd = reinterpret_cast<HWND>(AppWindow().Id().Value);
        if (!hwnd)
            return;

        if (!m_taskbar_list)
        {
            if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList3, m_taskbar_list.put_void())))
                return;
            m_taskbar_list->HrInit();
        }

        if (error)
        {
            m_taskbar_list->SetProgressState(hwnd, TBPF_ERROR);
            m_taskbar_list->SetProgressValue(hwnd, 100, 100);

            auto reset_timer = std::make_shared<DispatcherTimer>();
            reset_timer->Interval(std::chrono::milliseconds(3000));
            reset_timer->Tick([this, reset_timer, hwnd](IInspectable const&, IInspectable const&)
            {
                reset_timer->Stop();
                if (m_taskbar_list)
                    m_taskbar_list->SetProgressState(hwnd, TBPF_NOPROGRESS);
            });
            reset_timer->Start();
        }
        else if (!active)
        {
            m_taskbar_list->SetProgressState(hwnd, TBPF_NOPROGRESS);
        }
        else if (percent < 0)
        {
            m_taskbar_list->SetProgressState(hwnd, TBPF_INDETERMINATE);
        }
        else
        {
            m_taskbar_list->SetProgressState(hwnd, TBPF_NORMAL);
            m_taskbar_list->SetProgressValue(hwnd, static_cast<ULONGLONG>(percent), 100);
        }
    }

    /**
     * @brief Met à jour le badge de superposition (overlay icon) de la barre des tâches avec le nombre de courriels non lus.
     * @param count Nombre total de courriels non lus dans les boîtes de réception.
     */
    void MainWindow::UpdateTaskbarBadge(int32_t count)
    {
        if (!m_initialized)
            return;

        // Contrôle de l'autorisation du badge dans les paramètres utilisateur
        bool show_badge = ::EvolutionWin::Settings::Instance().GetBoolean(L"notifications.taskbar-badge", true);
        if (!show_badge)
            count = 0;

        HWND hwnd = reinterpret_cast<HWND>(AppWindow().Id().Value);
        if (!hwnd)
            return;

        if (!m_taskbar_list)
        {
            if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList3, m_taskbar_list.put_void())))
                return;
            m_taskbar_list->HrInit();
        }

        if (count <= 0)
        {
            m_taskbar_list->SetOverlayIcon(hwnd, nullptr, nullptr);
            return;
        }

        // Initialisation unique du sous-système GDI+ pour le tracé vectoriel avec canal alpha
        static bool s_gdiplus_inited = false;
        if (!s_gdiplus_inited)
        {
            Gdiplus::GdiplusStartupInput gsi;
            ULONG_PTR token = 0;
            Gdiplus::GdiplusStartup(&token, &gsi, nullptr);
            s_gdiplus_inited = true;
        }

        // Détermine la taille optimale selon l'échelle DPI de la barre des tâches Windows (16, 20, 24, 32...)
        int icon_size = GetSystemMetrics(SM_CXSMICON);
        if (icon_size < 16) icon_size = 16;
        if (icon_size > 48) icon_size = 48;

        HICON overlay_icon = nullptr;
        {
            // Bitmap 32-bit ARGB avec transparence native (aucun contour noir)
            Gdiplus::Bitmap bmp(icon_size, icon_size, PixelFormat32bppARGB);
            Gdiplus::Graphics g(&bmp);

            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

            // Fond totalement transparent
            g.Clear(Gdiplus::Color(0, 0, 0, 0));

            float fsize = static_cast<float>(icon_size);
            float pad = 1.0f;
            float circle_d = fsize - 2.0f * pad;

            // Pastille circulaire bleue Fluent (#0078D4)
            Gdiplus::SolidBrush badge_brush(Gdiplus::Color(255, 0, 120, 215));
            g.FillEllipse(&badge_brush, pad, pad, circle_d, circle_d);

            // Fine bordure blanche anti-aliasée pour détacher la pastille sur tout type de fond
            Gdiplus::Pen border_pen(Gdiplus::Color(255, 255, 255, 255), 1.25f);
            g.DrawEllipse(&border_pen, pad, pad, circle_d, circle_d);

            // Texte centré lisible et net adapté à la taille de l'icône
            std::wstring badge_text = count > 99 ? L"99+" : std::to_wstring(count);

            float font_size = fsize * 0.48f;
            if (badge_text.size() > 2)
                font_size = fsize * 0.38f;

            Gdiplus::Font font(L"Segoe UI Variable Text", font_size, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            if (font.GetLastStatus() != Gdiplus::Ok)
            {
                // Fallback sur Segoe UI standard si Segoe UI Variable n'est pas trouvée
                Gdiplus::Font fallback_font(L"Segoe UI", font_size, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush text_brush(Gdiplus::Color(255, 255, 255, 255));
                Gdiplus::StringFormat format;
                format.SetAlignment(Gdiplus::StringAlignmentCenter);
                format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                Gdiplus::RectF rect(0.0f, 0.0f, fsize, fsize);
                g.DrawString(badge_text.c_str(), -1, &fallback_font, rect, &format, &text_brush);
            }
            else
            {
                Gdiplus::SolidBrush text_brush(Gdiplus::Color(255, 255, 255, 255));
                Gdiplus::StringFormat format;
                format.SetAlignment(Gdiplus::StringAlignmentCenter);
                format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                Gdiplus::RectF rect(0.0f, 0.0f, fsize, fsize);
                g.DrawString(badge_text.c_str(), -1, &font, rect, &format, &text_brush);
            }

            // Génération de l'icône Windows HICON 32-bit ARGB
            bmp.GetHICON(&overlay_icon);
        }

        std::wstring desc = std::to_wstring(count) + L" messages non lus";
        m_taskbar_list->SetOverlayIcon(hwnd, overlay_icon, desc.c_str());

        if (overlay_icon)
            DestroyIcon(overlay_icon);
    }

    /* Met à jour la barre de progression permanente et son texte indicatif.
     * Paramètres :
     *   active  : true si une opération de synchronisation est en cours, false si terminée
     *   message : libellé de l'opération en cours
     *   percent : pourcentage d'avancement (-1 pour indéterminé) */
    void MainWindow::UpdateStatusBarProgress(bool active, hstring const& message, int32_t percent)
    {
        if (!m_initialized)
            return;

        UpdateTaskbarProgress(active, percent, false);

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

    /**
     * @brief Gestionnaire du bouton d'annulation sur la barre de progression de la barre d'état.
     * Interrompt l'opération réseau ou de synchronisation en cours via evo-core.
     * @param sender Bouton émetteur.
     * @param args Arguments d'événement.
     */
    void MainWindow::OnCancelSyncClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_core)
        {
            m_core->CancelOperations();
            ShowStatus(L"Annulation de l'opération en cours demandée…", InfoBarSeverity::Informational);
        }
        if (StatusBarProgressPanel())
            StatusBarProgressPanel().Visibility(Visibility::Collapsed);
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
        if (m_accounts)
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

        if (!m_initialized)
            return;

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
        if (!m_initialized)
            return;

        ConnectionAccountsPanel().Children().Clear();

        if (!m_accounts || m_accounts.Size() == 0)
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
        if (!m_initialized)
            return;

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

    /* Ajoute une notification dans l'historique de la barre d'état.
     * Paramètres :
     *   title : titre de la notification (ex: "Nouveau message de Alice")
     *   body  : extrait ou objet du message */
    void MainWindow::AddNotificationItem(hstring const& title, hstring const& body)
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t time_buf[16];
        swprintf_s(time_buf, L"%02d:%02d", st.wHour, st.wMinute);

        NotificationItemRecord record;
        record.id = L"notif_" + to_hstring(m_next_notification_id++);
        record.title = title;
        record.body = body;
        record.timestamp = hstring{ time_buf };

        /* Insertion en tête de liste (chronologie inverse) */
        m_notifications.insert(m_notifications.begin(), record);

        /* Limite à 50 notifications au maximum */
        if (m_notifications.size() > 50)
            m_notifications.pop_back();

        UpdateNotificationsUI();
    }

    /* Supprime une notification individuelle de la liste.
     * Paramètre :
     *   id : identifiant unique de la notification à retirer */
    void MainWindow::DismissNotificationItem(hstring const& id)
    {
        for (auto it = m_notifications.begin(); it != m_notifications.end(); ++it)
        {
            if (it->id == id)
            {
                m_notifications.erase(it);
                break;
            }
        }
        UpdateNotificationsUI();
    }

    /* Efface toutes les notifications enregistrées. */
    void MainWindow::ClearAllNotifications()
    {
        m_notifications.clear();
        UpdateNotificationsUI();
    }

    /* Gestionnaire du bouton "Tout effacer" du menu déroulant des notifications.
     * Paramètres :
     *   sender : bouton ayant déclenché l'action
     *   args   : arguments de routage de l'événement */
    void MainWindow::OnClearAllNotificationsClick(IInspectable const&, RoutedEventArgs const&)
    {
        ClearAllNotifications();
    }

    /* Rafraîchit l'affichage du menu déroulant et du badge des notifications. */
    void MainWindow::UpdateNotificationsUI()
    {
        uint32_t count = static_cast<uint32_t>(m_notifications.size());

        if (count == 0)
        {
            NotificationsBadge().Visibility(Visibility::Collapsed);
            NotificationsBadge().Value(0);
            NoNotificationsText().Visibility(Visibility::Visible);
            ClearAllNotificationsButton().IsEnabled(false);
        }
        else
        {
            NotificationsBadge().Visibility(Visibility::Visible);
            NotificationsBadge().Value(static_cast<int32_t>(count));
            NoNotificationsText().Visibility(Visibility::Collapsed);
            ClearAllNotificationsButton().IsEnabled(true);
        }

        NotificationsPanel().Children().Clear();

        auto res = Application::Current().Resources();
        auto card_bg = res.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<Brush>();
        auto card_stroke = res.Lookup(box_value(L"CardStrokeColorDefaultBrush")).as<Brush>();
        auto text_secondary = res.Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Brush>();
        auto body_strong_style = res.Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>();
        auto caption_style = res.Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>();
        Style subtle_btn_style{ nullptr };
        if (res.HasKey(box_value(L"SubtleButtonStyle")))
            subtle_btn_style = res.Lookup(box_value(L"SubtleButtonStyle")).as<Style>();

        for (auto const& item : m_notifications)
        {
            Border card;
            card.Background(card_bg);
            card.BorderBrush(card_stroke);
            card.BorderThickness(Thickness{ 1, 1, 1, 1 });
            card.CornerRadius(CornerRadius{ 4, 4, 4, 4 });
            card.Padding(Thickness{ 10, 8, 8, 8 });

            Grid grid;
            ColumnDefinition c0, c1;
            c0.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            c1.Width(GridLengthHelper::Auto());
            grid.ColumnDefinitions().Append(c0);
            grid.ColumnDefinitions().Append(c1);

            StackPanel text_stack;
            text_stack.Spacing(2);
            text_stack.VerticalAlignment(VerticalAlignment::Center);

            /* Ligne supérieure : Titre et horodatage */
            Grid header_grid;
            ColumnDefinition hc0, hc1;
            hc0.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            hc1.Width(GridLengthHelper::Auto());
            header_grid.ColumnDefinitions().Append(hc0);
            header_grid.ColumnDefinitions().Append(hc1);

            TextBlock title_block;
            title_block.Text(item.title);
            title_block.Style(body_strong_style);
            title_block.TextTrimming(TextTrimming::CharacterEllipsis);
            Grid::SetColumn(title_block, 0);
            header_grid.Children().Append(title_block);

            TextBlock time_block;
            time_block.Text(item.timestamp);
            time_block.Style(caption_style);
            time_block.Foreground(text_secondary);
            time_block.Margin(Thickness{ 8, 0, 0, 0 });
            Grid::SetColumn(time_block, 1);
            header_grid.Children().Append(time_block);

            text_stack.Children().Append(header_grid);

            /* Ligne inférieure : Extrait du message */
            TextBlock body_block;
            body_block.Text(item.body);
            body_block.Style(caption_style);
            body_block.TextWrapping(TextWrapping::Wrap);
            body_block.MaxLines(2);
            body_block.TextTrimming(TextTrimming::CharacterEllipsis);
            text_stack.Children().Append(body_block);

            Grid::SetColumn(text_stack, 0);
            grid.Children().Append(text_stack);

            /* Bouton de fermeture individuel (✕) */
            Button close_btn;
            if (subtle_btn_style)
                close_btn.Style(subtle_btn_style);
            close_btn.Padding(Thickness{ 4, 4, 4, 4 });
            close_btn.Margin(Thickness{ 4, 0, 0, 0 });
            close_btn.VerticalAlignment(VerticalAlignment::Top);
            ToolTipService::SetToolTip(close_btn, box_value(L"Supprimer cette notification"));

            FontIcon close_icon;
            close_icon.Glyph(L"\xE711");
            close_icon.FontSize(10);
            close_btn.Content(close_icon);

            hstring id_to_dismiss = item.id;
            close_btn.Click([this, id_to_dismiss](IInspectable const&, RoutedEventArgs const&)
            {
                DismissNotificationItem(id_to_dismiss);
            });

            Grid::SetColumn(close_btn, 1);
            grid.Children().Append(close_btn);

            card.Child(grid);
            NotificationsPanel().Children().Append(card);
        }
    }
}
