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

/* Liste des messages : chargement, affichage, sélection et rafraîchissement. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
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
            CancelMarkAsReadTimer();
            m_previous_selected_uid = L"";
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

            UpdateFolderStatusText();
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
        m_message_locations.clear();

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

            hstring msg_account = message.GetNamedString(L"account", m_account);
            hstring msg_folder = message.GetNamedString(L"folder", m_folder);
            hstring msg_folder_name = message.GetNamedString(L"folder_name", L"");
            hstring msg_account_name = message.GetNamedString(L"account_name", L"");
            m_message_locations[std::wstring{ uid }] = { msg_account, msg_folder, msg_folder_name, msg_account_name };

            m_message_states[std::wstring{ uid }] = MessageState{ seen, flagged, attachments };

            Grid item;
            if (m_compact_density)
                item.Padding(ThicknessHelper::FromLengths(8, 2, 8, 2));
            else
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

            /* Affiche l'origine du message (dossier ou compte) pour les recherches transversales */
            if (CurrentSearchScope() != L"folder" || msg_folder != m_folder || msg_account != m_account)
            {
                std::wstring origin_text = L"";
                if (CurrentSearchScope() == L"all" && !msg_account_name.empty())
                    origin_text = std::wstring{ msg_account_name } + L" › " + (!msg_folder_name.empty() ? std::wstring{ msg_folder_name } : std::wstring{ msg_folder });
                else if (!msg_folder_name.empty())
                    origin_text = std::wstring{ msg_folder_name };
                else if (!msg_folder.empty())
                    origin_text = std::wstring{ msg_folder };

                if (!origin_text.empty())
                {
                    TextBlock origin;
                    origin.Text(L"📁 " + origin_text);
                    origin.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                    origin.Opacity(0.65);
                    meta.Children().Append(origin);
                }
            }

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
            delete_button.Width(m_compact_density ? 26 : 32);
            delete_button.Height(m_compact_density ? 26 : 32);
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
                ConfirmAndDeleteMessages(TargetMessageUids(uid), IsTrashFolder());
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

                if (!multi)
                {
                    MenuFlyoutItem open_item;
                    open_item.Text(L"Ouvrir");
                    open_item.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
                    open_item.Icon(FontIcon{});
                    open_item.Icon().as<FontIcon>().Glyph(L"\xE8E7");
                    open_item.Click([this, uid](IInspectable const&, RoutedEventArgs const&)
                    {
                        OpenMessageInWindow(uid);
                    });
                    menu_items.Append(open_item);
                    menu_items.Append(MenuFlyoutSeparator{});
                }

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
                    ? hstring{ L"Transférer (1er mail + " + std::to_wstring(target_uids.size() - 1) + L" en PJ)" }
                    : L"Transférer");
                forward_item.Icon(FontIcon{});
                forward_item.Icon().as<FontIcon>().Glyph(L"\xE72D");
                forward_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ReplyToMessages(ReplyKind::Forward, target_uids);
                });
                menu_items.Append(forward_item);

                MenuFlyoutItem forward_att_item;
                forward_att_item.Text(multi
                    ? hstring{ L"Transférer en pièces jointes" + count_suffix }
                    : L"Transférer en pièce jointe (.eml)");
                forward_att_item.Icon(FontIcon{});
                forward_att_item.Icon().as<FontIcon>().Glyph(L"\xE723");
                forward_att_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ForwardMessagesAsAttachments(target_uids);
                });
                menu_items.Append(forward_att_item);

                MenuFlyoutItem browser_item;
                browser_item.Text(L"Afficher dans le navigateur");
                browser_item.Icon(FontIcon{});
                browser_item.Icon().as<FontIcon>().Glyph(L"\xE774");
                browser_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    hstring target_uid = target_uids.empty() ? SelectedMessageUid() : target_uids.front();
                    if (!target_uid.empty())
                    {
                        auto loc = GetMessageLocation(target_uid);
                        hstring acc = loc.account.empty() ? m_account : loc.account;
                        hstring fld = loc.folder.empty() ? m_folder : loc.folder;
                        m_core->FetchMessage(acc, fld, target_uid, [this](hstring const& json, hstring const& error)
                        {
                            if (!error.empty()) return;
                            JsonObject obj;
                            if (JsonObject::TryParse(json, obj))
                                ::EvolutionWin::Helpers::OpenInDefaultBrowser(obj);
                        });
                    }
                });
                menu_items.Append(browser_item);

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

                MenuFlyoutItem create_task_item;
                create_task_item.Text(hstring{ L"Créer une tâche à partir du message" + count_suffix });
                create_task_item.Icon(FontIcon{});
                create_task_item.Icon().as<FontIcon>().Glyph(L"\xE9D5");
                create_task_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    if (!target_uids.empty())
                        CreateTaskFromMessage(target_uids.front());
                });
                menu_items.Append(create_task_item);

                MenuFlyoutSubItem move_sub;
                move_sub.Text(hstring{ L"Déplacer vers" + count_suffix });
                move_sub.Icon(FontIcon{});
                move_sub.Icon().as<FontIcon>().Glyph(L"\xE8DE");
                PopulateMoveMenuItems(move_sub.Items(), target_uids);
                menu_items.Append(move_sub);

                MenuFlyoutSubItem copy_sub;
                copy_sub.Text(hstring{ L"Copier vers" + count_suffix });
                copy_sub.Icon(FontIcon{});
                copy_sub.Icon().as<FontIcon>().Glyph(L"\xE8C8");
                PopulateMoveMenuItems(copy_sub.Items(), target_uids, true);
                menu_items.Append(copy_sub);

                MenuFlyoutItem archive_item;
                archive_item.Text(hstring{ L"Archiver" + count_suffix });
                archive_item.Icon(FontIcon{});
                archive_item.Icon().as<FontIcon>().Glyph(L"\xE7B8");
                archive_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ArchiveMessagesByUids(target_uids);
                });
                menu_items.Append(archive_item);

                menu_items.Append(MenuFlyoutSeparator{});

                MenuFlyoutItem delete_item;
                delete_item.Text(hstring{ L"Supprimer" + count_suffix });
                delete_item.Icon(FontIcon{});
                delete_item.Icon().as<FontIcon>().Glyph(L"\xE74D");
                delete_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ConfirmAndDeleteMessages(target_uids, IsTrashFolder());
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

    /* Renvoie le compte et le dossier associés à un UID de message,
     * particulièrement utile pour les résultats de recherche transversale.
     * Paramètres :
     *   uid : identifiant unique du message
     * Retourne : structure MessageLocation contenant compte, dossier et libellés. */
    MainWindow::MessageLocation MainWindow::GetMessageLocation(hstring const& uid)
    {
        auto it = m_message_locations.find(std::wstring{ uid });
        if (it != m_message_locations.end() && !it->second.account.empty())
            return it->second;
        return { m_account, m_folder, L"", L"" };
    }

    /**
     * @brief Planifie le marquage comme lu d'un message selon les préférences de l'utilisateur.
     * @param uid Identifiant unique du courriel sélectionné.
     */
    void MainWindow::ScheduleMarkAsRead(hstring const& uid)
    {
        if (uid.empty())
            return;

        // Annule tout délai de lecture en cours
        CancelMarkAsReadTimer();

        hstring mode = ::EvolutionWin::Settings::Instance().GetString(L"mail.mark-as-read", L"immediate");

        if (mode == L"immediate")
        {
            MarkMessageAsRead(uid);
        }
        else if (mode == L"delayed")
        {
            m_pending_read_uid = uid;
            m_mark_read_timer = DispatcherTimer();
            m_mark_read_timer.Interval(std::chrono::seconds(5));
            auto token = std::make_shared<event_token>();
            *token = m_mark_read_timer.Tick([this, token](IInspectable const&, IInspectable const&)
            {
                if (m_mark_read_timer)
                {
                    m_mark_read_timer.Tick(*token);
                    m_mark_read_timer.Stop();
                    m_mark_read_timer = nullptr;
                }
                if (!m_pending_read_uid.empty() && m_pending_read_uid == SelectedMessageUid())
                {
                    MarkMessageAsRead(m_pending_read_uid);
                }
                m_pending_read_uid = L"";
            });
            m_mark_read_timer.Start();
        }
        else if (mode == L"on-selection-change")
        {
            m_pending_read_uid = uid;
        }
    }

    /**
     * @brief Annule la minuterie de marquage automatique comme lu si elle est active.
     */
    void MainWindow::CancelMarkAsReadTimer()
    {
        if (m_mark_read_timer)
        {
            m_mark_read_timer.Stop();
            m_mark_read_timer = nullptr;
        }
        m_pending_read_uid = L"";
    }

    /**
     * @brief Marque effectivement un message comme lu et synchronise l'indicateur avec le serveur.
     * @param uid Identifiant unique du courriel à marquer comme lu.
     */
    void MainWindow::MarkMessageAsRead(hstring const& uid)
    {
        if (uid.empty())
            return;

        auto it = m_message_states.find(std::wstring{ uid });
        if (it != m_message_states.end() && it->second.seen)
            return;

        SetMessageFlag(uid, L"seen", true);
    }

    void MainWindow::OnMessageSelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_reselecting)
            return;

        hstring mode = ::EvolutionWin::Settings::Instance().GetString(L"mail.mark-as-read", L"immediate");

        auto item = MessageList().SelectedItem().try_as<Grid>();
        if (!item)
        {
            CancelMarkAsReadTimer();
            if (mode == L"on-selection-change" && !m_previous_selected_uid.empty())
            {
                auto prev_it = m_message_states.find(std::wstring{ m_previous_selected_uid });
                if (prev_it != m_message_states.end() && !prev_it->second.seen)
                {
                    MarkMessageAsRead(m_previous_selected_uid);
                }
                m_previous_selected_uid = L"";
            }
            ClearReader();
            return;
        }

        hstring uid = unbox_value<hstring>(item.Tag());
        if (uid == m_current_message_uid && m_current_message)
        {
            UpdateActionButtons(uid);
            return;
        }

        // Mode au changement de sélection : marquer l'ancien message lu s'il a changé
        if (mode == L"on-selection-change" && !m_previous_selected_uid.empty() && m_previous_selected_uid != uid)
        {
            auto prev_it = m_message_states.find(std::wstring{ m_previous_selected_uid });
            if (prev_it != m_message_states.end() && !prev_it->second.seen)
            {
                MarkMessageAsRead(m_previous_selected_uid);
            }
        }
        m_previous_selected_uid = uid;

        uint64_t request = ++m_message_request;

        auto state_it = m_message_states.find(std::wstring{ uid });
        bool seen = state_it != m_message_states.end() ? state_it->second.seen : false;
        bool flagged = state_it != m_message_states.end() ? state_it->second.flagged : false;
        UpdateMessageItemVisual(item, seen, flagged);
        UpdateActionButtons(uid);

        CloseAttachmentPreview();
        StopSpeech();
        ScheduleMarkAsRead(uid);

        /* Si le volet de lecture est désactivé, conserver la sélection et les boutons sans charger le WebView2 */
        if (::EvolutionWin::Settings::Instance().GetString(L"layout.reading-pane", L"right") == L"off")
        {
            m_current_message_uid = uid;
            return;
        }

        auto loc = GetMessageLocation(uid);
        hstring target_account = loc.account.empty() ? m_account : loc.account;
        hstring target_folder = loc.folder.empty() ? m_folder : loc.folder;

        m_core->FetchMessage(target_account, target_folder, uid, [this, request, uid](hstring const& json, hstring const& error)
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

    void MainWindow::OnRefreshClick(IInspectable const&, RoutedEventArgs const&)
    {
        UpdateStatusBarProgress(true, L"Relève du courrier…", -1);
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
        UpdateStatusBarProgress(true, L"Relève de tous les dossiers…", -1);
        ShowStatus(L"Relève de tous les dossiers…", InfoBarSeverity::Informational);
        m_core->RefreshAll();
    }

    /* Maj+F9 : le dossier affiché seulement (la liste interroge le serveur) */
    void MainWindow::OnRefreshFolderClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_folder.empty())
            return;
        UpdateStatusBarProgress(true, L"Mise à jour de « " + FolderTitle().Text() + L" »…", -1);
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

        /* Ajout dans l'historique du menu déroulant de la barre d'état (notifications discrètes) */
        AddNotificationItem(title, body);

        /* Émission exclusive de la notification via les Toasts système Windows */
        try
        {
            Microsoft::Windows::AppNotifications::Builder::AppNotificationBuilder builder;
            builder.AddText(title);
            builder.AddText(body);
            Microsoft::Windows::AppNotifications::AppNotificationManager::Default().Show(builder.BuildNotification());
        }
        catch (...)
        {
        }
    }

    /* Détermine si le dossier actuellement affiché est le dossier des brouillons.
     * Retourne : true s'il s'agit du dossier Brouillons. */
    bool MainWindow::IsDraftsFolder()
    {
        if (m_account.empty() || m_folder.empty())
            return false;

        auto it = m_account_folders.find(std::wstring{ m_account });
        if (it != m_account_folders.end() && it->second)
        {
            auto folder = FindFolderJson(it->second, m_folder);
            if (folder && folder.GetNamedString(L"type", L"") == L"drafts")
                return true;
        }

        return false;
    }

    /**
     * @brief Détermine si le dossier actuellement affiché est la Corbeille (Éléments supprimés).
     * @return true s'il s'agit du dossier Corbeille, false sinon.
     */
    bool MainWindow::IsTrashFolder()
    {
        if (m_account.empty() || m_folder.empty())
            return false;

        auto it = m_account_folders.find(std::wstring{ m_account });
        if (it != m_account_folders.end() && it->second)
        {
            auto folder = FindFolderJson(it->second, m_folder);
            if (folder && folder.GetNamedString(L"type", L"") == L"trash")
                return true;
        }

        if (auto current = CurrentFolderItem())
        {
            if (current.Glyph() == FolderGlyph(L"trash"))
                return true;
        }

        return false;
    }

    /**
     * @brief Supprime une sélection de messages avec demande de confirmation si la suppression est définitive.
     * @param uids Liste des identifiants uniques des messages à supprimer.
     * @param permanent Indique si la suppression demandée est définitive (Maj+Suppr ou dossier Corbeille).
     */
    fire_and_forget MainWindow::ConfirmAndDeleteMessages(std::vector<winrt::hstring> uids, bool permanent)
    {
        if (uids.empty())
            co_return;

        bool confirm_required = permanent && ::EvolutionWin::Settings::Instance().GetBoolean(L"mail.confirm-permanent-delete", true);

        if (confirm_required)
        {
            if (m_dialog_open || !Content() || !Content().XamlRoot())
                co_return;

            ContentDialog confirm;
            confirm.XamlRoot(Content().XamlRoot());
            confirm.Title(box_value(L"Supprimer définitivement ?"));
            TextBlock text;
            text.TextWrapping(TextWrapping::Wrap);
            if (uids.size() == 1)
            {
                text.Text(L"Le message sélectionné sera définitivement supprimé. Cette opération est irréversible.");
            }
            else
            {
                text.Text(hstring{ std::to_wstring(uids.size()) + L" messages sélectionnés seront définitivement supprimés. Cette opération est irréversible." });
            }
            confirm.Content(text);
            confirm.PrimaryButtonText(L"Supprimer définitivement");
            confirm.CloseButtonText(L"Annuler");
            confirm.DefaultButton(ContentDialogButton::Close);

            m_dialog_open = true;
            auto result = co_await confirm.ShowAsync();
            m_dialog_open = false;

            if (result != ContentDialogResult::Primary)
                co_return;
        }

        DeleteMessagesByUids(uids);
    }

    /* Double-clic sur la liste des messages : ouvre le message dans une fenêtre
     * indépendante (ou la fenêtre de rédaction si c'est un brouillon).
     * Paramètres :
     *   args : arguments de l'événement DoubleTapped */
    void MainWindow::OnMessageListDoubleTapped(IInspectable const&, Input::DoubleTappedRoutedEventArgs const& args)
    {
        hstring uid = SelectedMessageUid();
        if (uid.empty())
            return;

        args.Handled(true);
        OpenMessageInWindow(uid);
    }

    /* Touche Entrée sur la liste des messages : ouverture dans une fenêtre indépendante.
     * Paramètres :
     *   args : arguments de l'événement KeyDown */
    void MainWindow::OnMessageListKeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() == Windows::System::VirtualKey::Enter)
        {
            hstring uid = SelectedMessageUid();
            if (!uid.empty())
            {
                args.Handled(true);
                OpenMessageInWindow(uid);
            }
        }
        else if (args.Key() == Windows::System::VirtualKey::Delete)
        {
            auto uids = SelectedMessageUids();
            if (uids.empty())
            {
                hstring single = SelectedMessageUid();
                if (!single.empty())
                    uids.push_back(single);
            }
            if (!uids.empty())
            {
                args.Handled(true);
                bool is_shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                ConfirmAndDeleteMessages(uids, is_shift || IsTrashFolder());
            }
        }
    }

    /* Ouvre le message sélectionné dans une fenêtre indépendante (MessageWindow).
     * S'il s'agit d'un brouillon, ouvre la fenêtre de rédaction (ComposeWindow).
     * Si la fenêtre pour cet UID est déjà ouverte, elle est ramenée au premier plan.
     * Paramètres :
     *   uid : identifiant unique du message à ouvrir */
    void MainWindow::OpenMessageInWindow(hstring const& uid)
    {
        if (uid.empty() || m_account.empty() || m_folder.empty())
            return;

        /* Si le dossier courant est le dossier Brouillons, ouvrir la fenêtre de rédaction */
        if (IsDraftsFolder())
        {
            m_core->FetchMessage(m_account, m_folder, uid, [this](hstring const& json, hstring const& error)
            {
                if (!error.empty())
                {
                    ShowStatus(L"Impossible d'ouvrir le brouillon : " + error, InfoBarSeverity::Error);
                    return;
                }
                JsonObject msg;
                if (!JsonObject::TryParse(json, msg))
                    return;

                ::EvolutionWin::ComposeData data;
                data.account = m_account;
                data.to = msg.GetNamedString(L"to", L"");
                data.cc = msg.GetNamedString(L"cc", L"");
                data.subject = msg.GetNamedString(L"subject", L"");
                data.body = msg.GetNamedString(L"text", L"");
                data.html_body = msg.GetNamedString(L"html", L"");
                data.in_reply_to = msg.GetNamedString(L"in_reply_to", L"");
                data.references = msg.GetNamedString(L"references", L"");
                data.reply = false;

                OpenCompose(data);
            });
            return;
        }

        /* Si une fenêtre est déjà ouverte pour ce message, l'activer */
        auto it = m_open_message_windows.find(std::wstring{ uid });
        if (it != m_open_message_windows.end())
        {
            it->second.Activate();
            return;
        }

        /* Liste des UID ordonnés de la vue pour la navigation Précédent / Suivant */
        std::vector<hstring> all_uids;
        for (auto const& item : MessageList().Items())
        {
            if (auto grid = item.try_as<Grid>())
            {
                hstring u = unbox_value_or<hstring>(grid.Tag(), L"");
                if (!u.empty())
                    all_uids.push_back(u);
            }
        }

        auto window = make<MessageWindow>();
        m_open_message_windows[std::wstring{ uid }] = window;

        /* Nettoyage du registre à la fermeture de la fenêtre */
        window.Closed([this, uid](auto const&, auto const&)
        {
            m_open_message_windows.erase(std::wstring{ uid });
        });

        /* Configuration et synchronisation de l'état avec la liste principale */
        auto loc = GetMessageLocation(uid);
        hstring target_account = loc.account.empty() ? m_account : loc.account;
        hstring target_folder = loc.folder.empty() ? m_folder : loc.folder;

        get_self<MessageWindow>(window)->Setup(
            m_core,
            m_accounts,
            target_account,
            target_folder,
            uid,
            all_uids,
            m_account_folders,
            [this](hstring const& updated_uid, bool seen)
            {
                m_message_states[std::wstring{ updated_uid }].seen = seen;
                if (auto grid = FindMessageItem(updated_uid))
                {
                    auto state_it = m_message_states.find(std::wstring{ updated_uid });
                    bool flagged = state_it != m_message_states.end() ? state_it->second.flagged : false;
                    UpdateMessageItemVisual(grid, seen, flagged);
                }
                if (updated_uid == m_current_message_uid)
                    UpdateActionButtons(updated_uid);
            },
            [this](hstring const& /*deleted_uid*/)
            {
                LoadMessages(true);
            },
            [this](hstring const& /*moved_uid*/, hstring const& /*dest_folder*/)
            {
                LoadMessages(true);
            }
        );

        window.Activate();
    }
}
