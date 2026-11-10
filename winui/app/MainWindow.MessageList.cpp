#include "pch.h"
#include <winrt/Windows.Graphics.Imaging.h>
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "FollowUp.h"
#include "WindowIcon.h"
#include "Settings.h"
#include "ConversationThreads.h"
#include "UnifiedViews.h"

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
#include <map>
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

        UpdateFocusedInbox();

        /* vue unifiée : agrégation des dossiers de même rôle de tous les comptes */
        if (IsUnifiedView())
        {
            LoadUnifiedMessages(keep_selection);
            return;
        }

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
                ShowStatus(Tr(L"Messages : ") + error, InfoBarSeverity::Error);
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

    /* Charge une vue unifiée (Boîte de réception, Envoyés, Brouillons ou
     * Corbeille de tous les comptes) et l'affiche comme un dossier ordinaire :
     * chaque message garde son compte et son dossier d'origine (voir
     * GetMessageLocation), donc lecture, indicateurs, déplacement,
     * suppression et réponse agissent sur le dossier réel.
     * Paramètres :
     *   keep_selection : conserver le message sélectionné (rechargement) */
    void MainWindow::LoadUnifiedMessages(bool keep_selection)
    {
        hstring account = m_account;
        hstring role = m_folder;
        hstring selected;

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
            m_message_locations.clear();
            ListProgress().IsActive(true);
        }

        RequestUnifiedMessages(role,
            [this, account, role, selected](hstring const& json, hstring const& error)
        {
            /* une autre vue a été choisie entre-temps */
            if (account != m_account || role != m_folder)
                return;

            ListProgress().IsActive(false);

            if (!error.empty())
            {
                ShowStatus(Tr(L"Vue unifiée : ", L"Unified view: ") + error, InfoBarSeverity::Error);
                return;
            }

            m_all_messages = JsonArray::Parse(json);

            if (!SearchBox().Text().empty())
                FilterLocalMessages(SearchBox().Text());
            else
                ShowMessages(m_all_messages);

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
    namespace
    {
        /* Groupe de dates d'un message (façon Outlook) : clé stable et libellé.
         * Aujourd'hui, Hier, jours de la semaine en cours, Semaine dernière, Il y a deux / trois semaines, Mois dernier, Avant le mois dernier. */
        std::pair<std::wstring, std::wstring> DateBucket(double message_time)
        {
            using Windows::Globalization::DateTimeFormatting::DateTimeFormatter;
            std::time_t now = std::time(nullptr);
            std::tm today{};
            localtime_s(&today, &now);
            today.tm_hour = today.tm_min = today.tm_sec = 0;
            today.tm_isdst = -1;
            std::time_t today0 = std::mktime(&today);

            std::time_t raw = static_cast<std::time_t>(message_time);
            std::tm day{};
            localtime_s(&day, &raw);
            day.tm_hour = day.tm_min = day.tm_sec = 0;
            day.tm_isdst = -1;
            std::time_t day0 = std::mktime(&day);

            long long days = std::llround(std::difftime(today0, day0) / 86400.0);
            auto capitalised = [](std::wstring text)
            {
                /* le formateur peut faire précéder le nom d'un caractère de contrôle (marque de direction) : 1re lettre */
                std::wstring cleaned;
                for (wchar_t c : text)
                    if (c != 0x200E && c != 0x200F && c != 0x202A && c != 0x202C)
                        cleaned += c;
                if (!cleaned.empty())
                    cleaned[0] = static_cast<wchar_t>(std::towupper(cleaned[0]));
                return cleaned;
            };
            if (days <= 0)
                return { L"d0", std::wstring{ Tr(L"Aujourd'hui", L"Today") } };
            if (days == 1)
                return { L"d1", std::wstring{ Tr(L"Hier", L"Yesterday") } };

            /* semaines du lundi au dimanche, comme dans Outlook : jours de la semaine en cours, puis
             * « Semaine dernière », « Il y a deux semaines », « Il y a trois semaines », puis mois */
            long long today_offset = (today.tm_wday + 6) % 7;
            long long day_offset = (day.tm_wday + 6) % 7;
            if (days <= today_offset)
                return { L"d" + std::to_wstring(days),
                    capitalised(std::wstring{ DateTimeFormatter{ L"dayofweek.full" }.Format(winrt::clock::from_time_t(raw)) }) };

            long long weeks = std::llround(std::difftime(today0 - today_offset * 86400, day0 - day_offset * 86400) / (7 * 86400.0));
            if (weeks == 1)
                return { L"w1", std::wstring{ Tr(L"Semaine dernière", L"Last week") } };
            if (weeks == 2)
                return { L"w2", std::wstring{ Tr(L"Il y a deux semaines", L"Two weeks ago") } };
            if (weeks == 3)
                return { L"w3", std::wstring{ Tr(L"Il y a trois semaines", L"Three weeks ago") } };

            int months_ago = (today.tm_year - day.tm_year) * 12 + (today.tm_mon - day.tm_mon);
            if (months_ago <= 0)
                return { L"m0", std::wstring{ Tr(L"Plus tôt ce mois-ci", L"Earlier this month") } };
            if (months_ago == 1)
                return { L"m1", std::wstring{ Tr(L"Mois dernier", L"Last month") } };
            return { L"m2", std::wstring{ Tr(L"Avant le mois dernier", L"Before last month") } };
        }
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

        /* Regroupement par discussion : le premier message (selon le tri) de
         * chaque fil sert d'en-tête ; les autres sont masqués tant que le fil
         * n'est pas développé */
        struct ThreadRow { bool header{ true }; bool hidden{ false }; size_t count{ 1 }; size_t unread{ 0 }; std::wstring key; };
        m_thread_latest.clear();
        m_thread_members.clear();
        std::vector<ThreadRow> rows(shown.size());
        m_last_shown_messages = messages;
        if (m_conversations_enabled)
        {
            std::vector<::EvolutionWin::Conversations::ThreadMessage> inputs;
            inputs.reserve(shown.size());
            for (auto const& message : shown)
            {
                ::EvolutionWin::Conversations::ThreadMessage input;
                input.message_id = message.GetNamedString(L"msgid", L"").c_str();
                for (auto const& ref : message.GetNamedArray(L"refs", JsonArray{}))
                    input.references.push_back(ref.GetString().c_str());
                input.subject = message.GetNamedString(L"subject", L"").c_str();
                inputs.push_back(std::move(input));
            }
            auto roots = ::EvolutionWin::Conversations::BuildThreads(inputs);

            /* un fil se range à la place de son premier message affiché */
            std::map<size_t, size_t> head_of;
            for (size_t i = 0; i < shown.size(); ++i)
                head_of.emplace(roots[i], i);
            std::vector<size_t> order;
            order.reserve(shown.size());
            for (size_t i = 0; i < shown.size(); ++i)
            {
                if (head_of[roots[i]] != i)
                    continue;
                for (size_t j = 0; j < shown.size(); ++j)
                    if (roots[j] == roots[i])
                        order.push_back(j);
            }
            std::vector<JsonObject> grouped;
            std::vector<size_t> grouped_roots;
            grouped.reserve(shown.size());
            for (size_t index : order)
            {
                grouped.push_back(shown[index]);
                grouped_roots.push_back(roots[index]);
            }
            shown = std::move(grouped);

            for (size_t i = 0; i < shown.size();)
            {
                size_t end = i;
                size_t unread = 0;
                while (end < shown.size() && grouped_roots[end] == grouped_roots[i])
                {
                    if (!shown[end].GetNamedBoolean(L"seen", true))
                        ++unread;
                    ++end;
                }
                std::wstring key = ::EvolutionWin::Conversations::NormalizeThreadSubject(
                    shown[i].GetNamedString(L"subject", L"").c_str());
                if (key.empty())
                    key = shown[i].GetNamedString(L"uid").c_str();
                bool expanded = m_expanded_threads.count(key) != 0;
                /* le message sélectionné ne doit pas disparaître dans un fil replié, sauf si
                 * l'utilisateur l'a replié lui-même */
                for (size_t k = i; k < end && !expanded && m_collapsed_threads.count(key) == 0; ++k)
                    if (!selected.empty() && shown[k].GetNamedString(L"uid") == selected)
                    {
                        expanded = true;
                        m_expanded_threads.insert(key);
                    }
                /* fil replié : un clic sur l'en-tête affiche le message le plus récent du fil */
                if (!expanded && end - i > 1)
                {
                    size_t latest = i;
                    for (size_t k = i + 1; k < end; ++k)
                        if (shown[k].GetNamedNumber(L"date", 0) > shown[latest].GetNamedNumber(L"date", 0))
                            latest = k;
                    m_thread_latest[std::wstring{ shown[i].GetNamedString(L"uid") }] =
                        { std::wstring{ shown[latest].GetNamedString(L"uid") }, key };
                }
                if (end - i > 1)
                {
                    auto& members = m_thread_members[std::wstring{ shown[i].GetNamedString(L"uid") }];
                    for (size_t k = i; k < end; ++k)
                        members.push_back(std::wstring{ shown[k].GetNamedString(L"uid") });
                }
                for (size_t k = i; k < end; ++k)
                {
                    rows[k].key = key;
                    rows[k].count = end - i;
                    rows[k].unread = unread;
                    rows[k].header = (k == i);
                    rows[k].hidden = (k != i) && !expanded;
                }
                i = end;
            }
        }

        /* groupes de dates (liste triée par date) : un en-tête repliable par groupe */
        bool date_groups = m_sort_key == L"date" && ::EvolutionWin::Settings::Instance().GetBoolean(L"mail.group-by-date", true);
        std::vector<std::pair<std::wstring, std::wstring>> buckets(shown.size());
        std::map<std::wstring, size_t> bucket_counts;
        if (date_groups)
        {
            for (size_t i = 0; i < shown.size(); ++i)
            {
                /* un message d'un fil suit le groupe de l'en-tête du fil */
                buckets[i] = (i > 0 && m_conversations_enabled && !rows[i].header)
                    ? buckets[i - 1] : DateBucket(shown[i].GetNamedNumber(L"date", 0));
                if (!rows[i].hidden)
                    ++bucket_counts[buckets[i].first];
            }
        }
        std::wstring last_bucket;

        size_t row_index = static_cast<size_t>(-1);
        for (auto const& message : shown)
        {
            ++row_index;
            ThreadRow const thread_row = rows[row_index];
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
            {
                auto& fu_state = m_message_states[std::wstring{ uid }];
                fu_state.follow_up = std::wstring{ message.GetNamedString(L"follow_up", L"") };
                fu_state.due_by = ::EvolutionWin::FollowUp::ParseEpoch(message.GetNamedString(L"due_by", L""));
                fu_state.completed_on = ::EvolutionWin::FollowUp::ParseEpoch(message.GetNamedString(L"completed_on", L""));
            }

            if (thread_row.hidden)
                continue;

            if (date_groups)
            {
                auto const& bucket = buckets[row_index];
                bool collapsed = m_collapsed_date_groups.count(bucket.first) != 0;
                if (bucket.first != last_bucket)
                {
                    last_bucket = bucket.first;

                    Grid header;
                    header.Padding(ThicknessHelper::FromLengths(12, 5, 12, 5));
                    header.ColumnSpacing(8);
                    header.HorizontalAlignment(HorizontalAlignment::Stretch);
                    header.Background(Application::Current().Resources().Lookup(box_value(L"SubtleFillColorSecondaryBrush")).as<Brush>());
                    header.Tag(box_value(L""));
                    ColumnDefinition chevron_column, label_column;
                    chevron_column.Width(GridLengthHelper::Auto());
                    label_column.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                    header.ColumnDefinitions().Append(chevron_column);
                    header.ColumnDefinitions().Append(label_column);
                    FontIcon chevron;
                    chevron.Glyph(collapsed ? L"\xE76C" : L"\xE70D");
                    chevron.FontSize(11);
                    chevron.VerticalAlignment(VerticalAlignment::Center);
                    header.Children().Append(chevron);
                    TextBlock label;
                    label.Text(hstring{ bucket.second + L" (" + std::to_wstring(bucket_counts[bucket.first]) + L")" });
                    label.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
                    label.FontSize(12);
                    Grid::SetColumn(label, 1);
                    header.Children().Append(label);
                    std::wstring bucket_key = bucket.first;
                    header.Tapped([this, bucket_key](IInspectable const&, Input::TappedRoutedEventArgs const& args)
                    {
                        args.Handled(true);
                        if (!m_collapsed_date_groups.erase(bucket_key))
                            m_collapsed_date_groups.insert(bucket_key);
                        ReapplyMessageFilters();
                    });
                    items.Append(header);
                }
                /* le message affiché reste visible même dans un groupe replié */
                if (collapsed && !(!selected.empty() && uid == selected))
                    continue;
            }

            Grid item;
            /* trait fin entre deux messages et un peu d'espace autour */
            item.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 1));
            item.BorderBrush(Application::Current().Resources().Lookup(box_value(L"DividerStrokeColorDefaultBrush")).as<Brush>());
            if (m_compact_density)
                item.Padding(ThicknessHelper::FromLengths(8, 5, 8, 5));
            else
                item.Padding(ThicknessHelper::FromLengths(8, 12, 8, 12));
            item.HorizontalAlignment(HorizontalAlignment::Stretch);
            /* Fond transparent explicite pour que le survol (PointerEntered/PointerExited)
             * soit détecté sur toute la surface de la ligne et pas uniquement sur le texte */
            item.Background(SolidColorBrush{ Microsoft::UI::Colors::Transparent() });
            item.Tag(box_value(uid));
            if (m_conversations_enabled && !thread_row.header)
                item.Margin(ThicknessHelper::FromLengths(24, 0, 0, 0));
            item.ColumnDefinitions().Append(ColumnDefinition{});
            ColumnDefinition date_column;
            date_column.Width(GridLengthHelper::Auto());
            item.ColumnDefinitions().Append(date_column);
            ColumnDefinition action_column;
            action_column.Width(GridLengthHelper::Auto());
            item.ColumnDefinitions().Append(action_column);
            item.RowDefinitions().Append(RowDefinition{});
            item.RowDefinitions().Append(RowDefinition{});

            /* ligne expéditeur : bouton replier / développer (si fil) puis nom */
            Grid from_row;
            ColumnDefinition from_toggle_column;
            from_toggle_column.Width(GridLengthHelper::Auto());
            from_row.ColumnDefinitions().Append(from_toggle_column);
            from_row.ColumnDefinitions().Append(ColumnDefinition{});
            TextBlock from;
            from.Text(SenderName(message.GetNamedString(L"from", L"")));
            from.TextTrimming(TextTrimming::CharacterEllipsis);
            from.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(from, 1);
            from_row.Children().Append(from);
            item.Children().Append(from_row);

            StackPanel meta;
            meta.Orientation(Orientation::Horizontal);
            meta.Spacing(6);
            meta.VerticalAlignment(VerticalAlignment::Center);
            meta.Margin(ThicknessHelper::FromLengths(8, 0, 0, 0));
            Grid::SetColumn(meta, 1);

            /* Indicateur d'importance (haute ou basse) */
            hstring importance_str = message.GetNamedString(L"importance", L"normal");
            double priority_num = message.GetNamedNumber(L"priority", 3.0);
            auto importance_level = ::EvolutionWin::Helpers::ParseMessageImportance(importance_str, priority_num);

            if (importance_level == ::EvolutionWin::Helpers::MessageImportanceLevel::High)
            {
                FontIcon high_prio_icon;
                high_prio_icon.Glyph(L"\xE7BA");
                high_prio_icon.FontSize(12);
                high_prio_icon.Foreground(SolidColorBrush{ Windows::UI::Color{ 255, 232, 17, 35 } });
                ToolTipService::SetToolTip(high_prio_icon, box_value(Tr(L"Importance haute")));
                meta.Children().Append(high_prio_icon);
            }
            else if (importance_level == ::EvolutionWin::Helpers::MessageImportanceLevel::Low)
            {
                FontIcon low_prio_icon;
                low_prio_icon.Glyph(L"\xE74B");
                low_prio_icon.FontSize(12);
                low_prio_icon.Foreground(SolidColorBrush{ Windows::UI::Color{ 255, 0, 120, 212 } });
                ToolTipService::SetToolTip(low_prio_icon, box_value(Tr(L"Importance basse")));
                meta.Children().Append(low_prio_icon);
            }

            FontIcon flag_icon;
            flag_icon.Glyph(L"\xE7C1");
            flag_icon.FontSize(12);
            flag_icon.Foreground(SolidColorBrush{ Windows::UI::Color{ 255, 247, 99, 12 } });
            flag_icon.Tag(box_value(L"follow-flag"));
            StyleFollowUpIcon(flag_icon, m_message_states[std::wstring{ uid }]);
            flag_icon.Visibility(flagged ? Visibility::Visible : Visibility::Collapsed);
            meta.Children().Append(flag_icon);

            FontIcon attachment_icon;
            attachment_icon.Glyph(L"\xE723");
            attachment_icon.FontSize(12);
            attachment_icon.Visibility(attachments ? Visibility::Visible : Visibility::Collapsed);
            meta.Children().Append(attachment_icon);

            /* Affiche l'origine du message (dossier ou compte) pour les recherches transversales */
            if (IsUnifiedView() || CurrentSearchScope() != L"folder" || msg_folder != m_folder || msg_account != m_account)
            {
                std::wstring origin_text = L"";
                if (IsUnifiedView())
                    origin_text = !msg_account_name.empty() ? std::wstring{ msg_account_name } : std::wstring{ msg_account };
                else if (CurrentSearchScope() == L"all" && !msg_account_name.empty())
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

            /* Badges colorés des catégories attribuées au message */
            if (message.HasKey(L"labels"))
            {
                auto labels = message.GetNamedArray(L"labels");
                for (auto const& l : labels)
                {
                    std::wstring label_str{ l.GetString() };
                    if (!label_str.empty())
                    {
                        uint8_t cr = 0, cg = 0, cb = 0;
                        ::EvolutionWin::Helpers::GetCategoryColor(label_str, cr, cg, cb);

                        Border badge;
                        badge.Background(SolidColorBrush{ Windows::UI::Color{ 255, cr, cg, cb } });
                        badge.CornerRadius(CornerRadiusHelper::FromUniformRadius(3));
                        badge.Padding(ThicknessHelper::FromLengths(4, 1, 4, 1));
                        badge.VerticalAlignment(VerticalAlignment::Center);

                        TextBlock badge_text;
                        badge_text.Text(hstring{ label_str });
                        badge_text.FontSize(10);
                        badge_text.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
                        badge_text.Foreground(SolidColorBrush{ Microsoft::UI::Colors::White() });
                        badge.Child(badge_text);

                        ToolTipService::SetToolTip(badge, box_value(hstring{ label_str }));
                        meta.Children().Append(badge);
                    }
                }
            }

            /* Fil de discussion : bouton replier / développer, nombre de messages et de non lus */
            if (m_conversations_enabled && thread_row.header && thread_row.count > 1)
            {
                bool expanded = m_expanded_threads.count(thread_row.key) != 0;
                Button toggle;
                FontIcon chevron;
                chevron.Glyph(expanded ? L"\xE70D" : L"\xE76C");
                chevron.FontSize(16);
                chevron.FontWeight(Microsoft::UI::Text::FontWeights::Bold());
                toggle.Content(chevron);
                toggle.Padding(ThicknessHelper::FromLengths(4, 2, 4, 2));
                toggle.Width(32);
                toggle.Height(32);
                toggle.MinWidth(0);
                toggle.MinHeight(0);
                toggle.Margin(ThicknessHelper::FromLengths(0, 0, 4, 0));
                toggle.VerticalAlignment(VerticalAlignment::Center);
                toggle.Background(SolidColorBrush{ Windows::UI::Color{ 40, 128, 128, 128 } });
                toggle.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
                toggle.BorderThickness(ThicknessHelper::FromUniformLength(0));
                ToolTipService::SetToolTip(toggle, box_value(expanded ? Tr(L"Replier la conversation") : Tr(L"Développer la conversation")));
                std::wstring key = thread_row.key;
                toggle.Click([this, key](IInspectable const&, RoutedEventArgs const&)
                {
                    if (m_expanded_threads.erase(key))
                    {
                        m_collapsed_threads.insert(key);
                    }
                    else
                    {
                        m_expanded_threads.insert(key);
                        m_collapsed_threads.erase(key);
                    }
                    if (m_last_shown_messages)
                        ShowMessages(m_last_shown_messages);
                });
                from_row.Children().Append(toggle);

                Border count_badge;
                count_badge.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
                count_badge.Padding(ThicknessHelper::FromLengths(6, 0, 6, 0));
                count_badge.VerticalAlignment(VerticalAlignment::Center);
                count_badge.Background(SolidColorBrush{ thread_row.unread > 0
                    ? Windows::UI::Color{ 255, 0, 120, 212 } : Windows::UI::Color{ 255, 128, 128, 128 } });
                TextBlock count_text;
                std::wstring label = std::to_wstring(thread_row.count);
                if (thread_row.unread > 0)
                    label = std::wstring{ Fmt(thread_row.unread > 1 ? Tr(L"{0} · {1} non lus") : Tr(L"{0} · {1} non lu"),
                        { to_hstring(thread_row.count), to_hstring(thread_row.unread) }) };
                count_text.Text(hstring{ label });
                count_text.FontSize(10);
                count_text.Foreground(SolidColorBrush{ Microsoft::UI::Colors::White() });
                count_badge.Child(count_text);
                meta.Children().Append(count_badge);
            }

            TextBlock date;
            date.Text(FormatSmartDate(message.GetNamedNumber(L"date", 0)));
            date.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            meta.Children().Append(date);

            item.Children().Append(meta);

            TextBlock subject;
            subject.Text(message.GetNamedString(L"subject", L""));
            subject.TextTrimming(TextTrimming::CharacterEllipsis);
            Grid::SetRow(subject, 1);
            Grid::SetColumnSpan(subject, 2);
            item.Children().Append(subject);

            /* Aperçu / extrait du message sous l'objet (configurable en nombre de lignes,
             * masqué si le mode confidentialité privacy.hide-snippets est actif). */
            bool hide_snippets = ::EvolutionWin::Settings::Instance().GetBoolean(L"privacy.hide-snippets", false);
            std::wstring snippet_lines_str{ ::EvolutionWin::Settings::Instance().GetString(L"mail.snippet-lines", L"1") };
            int snippet_lines = _wtoi(snippet_lines_str.c_str());
            hstring preview = message.GetNamedString(L"preview", L"");

            int row_count = 2;
            if (!hide_snippets && snippet_lines > 0 && !preview.empty())
            {
                RowDefinition preview_row;
                item.RowDefinitions().Append(preview_row);

                TextBlock snippet;
                snippet.Text(preview);
                snippet.MaxLines(snippet_lines);
                snippet.TextTrimming(TextTrimming::CharacterEllipsis);
                snippet.TextWrapping(TextWrapping::Wrap);
                snippet.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                snippet.Opacity(0.7);
                snippet.Margin(ThicknessHelper::FromLengths(0, 2, 0, 0));
                Grid::SetRow(snippet, 2);
                Grid::SetColumnSpan(snippet, 2);
                item.Children().Append(snippet);

                row_count = 3;
            }

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
            ToolTipService::SetToolTip(delete_button, box_value(Tr(L"Supprimer")));
            Grid::SetColumn(delete_button, 2);
            Grid::SetRow(delete_button, 0);
            Grid::SetRowSpan(delete_button, row_count);
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
            Grid::SetRowSpan(read_bar, row_count);
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
                /* ajoute « (N messages) » au libellé lorsque plusieurs messages sont ciblés */
                auto with_count = [multi, count = target_uids.size()](hstring const& label)
                {
                    return multi ? Fmt(Tr(L"{0} ({1} messages)"), { label, to_hstring(count) }) : label;
                };

                if (!multi)
                {
                    MenuFlyoutItem open_item;
                    open_item.Text(Tr(L"Ouvrir"));
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
                    ? Fmt(Tr(L"Répondre (1er mail + {0} en PJ)"), { to_hstring(target_uids.size() - 1) })
                    : Tr(L"Répondre"));
                reply_item.Icon(FontIcon{});
                reply_item.Icon().as<FontIcon>().Glyph(L"\xE97A");
                reply_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ReplyToMessages(ReplyKind::Sender, target_uids);
                });
                menu_items.Append(reply_item);

                MenuFlyoutItem reply_all_item;
                reply_all_item.Text(multi
                    ? Fmt(Tr(L"Répondre à tous (1er mail + {0} en PJ)"), { to_hstring(target_uids.size() - 1) })
                    : Tr(L"Répondre à tous"));
                reply_all_item.Icon(FontIcon{});
                reply_all_item.Icon().as<FontIcon>().Glyph(L"\xEE35");
                reply_all_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ReplyToMessages(ReplyKind::All, target_uids);
                });
                menu_items.Append(reply_all_item);

                MenuFlyoutItem forward_item;
                forward_item.Text(multi
                    ? Fmt(Tr(L"Transférer (1er mail + {0} en PJ)"), { to_hstring(target_uids.size() - 1) })
                    : Tr(L"Transférer"));
                forward_item.Icon(FontIcon{});
                forward_item.Icon().as<FontIcon>().Glyph(L"\xE72D");
                forward_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ReplyToMessages(ReplyKind::Forward, target_uids);
                });
                menu_items.Append(forward_item);

                MenuFlyoutItem forward_att_item;
                forward_att_item.Text(multi
                    ? with_count(Tr(L"Transférer en pièces jointes"))
                    : Tr(L"Transférer en pièce jointe (.eml)"));
                forward_att_item.Icon(FontIcon{});
                forward_att_item.Icon().as<FontIcon>().Glyph(L"\xE723");
                forward_att_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ForwardMessagesAsAttachments(target_uids);
                });
                menu_items.Append(forward_att_item);

                MenuFlyoutItem browser_item;
                browser_item.Text(Tr(L"Afficher dans le navigateur"));
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

                MenuFlyoutItem save_eml_item;
                save_eml_item.Text(target_uids.size() > 1 ? Tr(L"Enregistrer sous (dossier .eml)...") : Tr(L"Enregistrer sous (.eml)..."));
                save_eml_item.Icon(FontIcon{});
                save_eml_item.Icon().as<FontIcon>().Glyph(L"\xE74E");
                save_eml_item.Click([this](IInspectable const&, RoutedEventArgs const&)
                {
                    SaveMessageAsAsync();
                });
                menu_items.Append(save_eml_item);

                MenuFlyoutItem raw_source_item;
                raw_source_item.Text(Tr(L"Afficher la source brute (MIME)..."));
                raw_source_item.Icon(FontIcon{});
                raw_source_item.Icon().as<FontIcon>().Glyph(L"\xE943");
                raw_source_item.KeyboardAcceleratorTextOverride(L"Ctrl+Maj+U");
                raw_source_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    hstring target_uid = target_uids.empty() ? SelectedMessageUid() : target_uids.front();
                    if (!target_uid.empty())
                        ShowRawMimeSourceAsync(target_uid);
                });
                menu_items.Append(raw_source_item);

                menu_items.Append(MenuFlyoutSeparator{});

                /* Boîte de réception prioritaire (Microsoft 365) : Prioritaire / Autres, avec règle pour l'expéditeur */
                if (m_focused_inbox_active && !multi)
                {
                    bool is_other = m_inbox_other_ids.count(std::wstring{ uid }) != 0;
                    hstring target = is_other ? L"focused" : L"other";
                    MenuFlyoutItem move_item;
                    move_item.Text(is_other ? Tr(L"Déplacer vers Prioritaire", L"Move to Focused") : Tr(L"Déplacer vers Autres", L"Move to Other"));
                    FontIcon move_icon;
                    move_icon.Glyph(L"\xE8DE");
                    move_item.Icon(move_icon);
                    move_item.Click([this, uid, target](IInspectable const&, RoutedEventArgs const&) { ClassifyMessage(uid, target, false); });
                    menu_items.Append(move_item);

                    MenuFlyoutItem always_item;
                    always_item.Text(is_other ? Tr(L"Toujours déplacer vers Prioritaire", L"Always move to Focused")
                                              : Tr(L"Toujours déplacer vers Autres", L"Always move to Other"));
                    always_item.Click([this, uid, target](IInspectable const&, RoutedEventArgs const&) { ClassifyMessage(uid, target, true); });
                    menu_items.Append(always_item);
                    menu_items.Append(MenuFlyoutSeparator{});
                }

                auto state_it = m_message_states.find(std::wstring{ target_uids.front() });
                bool is_seen = state_it != m_message_states.end() ? state_it->second.seen : true;
                bool is_flagged = state_it != m_message_states.end() ? state_it->second.flagged : false;

                MenuFlyoutItem read_item;
                read_item.Text(with_count(is_seen ? Tr(L"Marquer comme non lu") : Tr(L"Marquer comme lu")));
                read_item.Icon(FontIcon{});
                read_item.Icon().as<FontIcon>().Glyph(is_seen ? L"\xE715" : L"\xE8C3");
                read_item.Click([this, target_uids, is_seen](IInspectable const&, RoutedEventArgs const&)
                {
                    SetMessagesFlag(target_uids, L"seen", !is_seen);
                });
                menu_items.Append(read_item);

                MenuFlyoutItem flag_item;
                flag_item.Text(with_count(is_flagged ? Tr(L"Retirer le drapeau") : Tr(L"Ajouter un drapeau")));
                flag_item.Icon(FontIcon{});
                flag_item.Icon().as<FontIcon>().Glyph(L"\xE7C1");
                flag_item.Click([this, target_uids, is_flagged](IInspectable const&, RoutedEventArgs const&)
                {
                    SetMessagesFlag(target_uids, L"flagged", !is_flagged);
                });
                menu_items.Append(flag_item);

                MenuFlyoutSubItem follow_up_sub;
                follow_up_sub.Text(with_count(Tr(L"Suivi")));
                follow_up_sub.Icon(FontIcon{});
                follow_up_sub.Icon().as<FontIcon>().Glyph(L"\xE787");
                PopulateFollowUpMenuItems(follow_up_sub.Items(), target_uids);
                menu_items.Append(follow_up_sub);

                MenuFlyoutSubItem cat_sub;
                cat_sub.Text(with_count(Tr(L"Catégoriser")));
                cat_sub.Icon(FontIcon{});
                cat_sub.Icon().as<FontIcon>().Glyph(L"\xE8EC");
                PopulateCategorizeMenuItems(cat_sub.Items(), target_uids);
                menu_items.Append(cat_sub);

                MenuFlyoutItem create_task_item;
                create_task_item.Text(with_count(Tr(L"Créer une tâche à partir du message")));
                create_task_item.Icon(FontIcon{});
                create_task_item.Icon().as<FontIcon>().Glyph(L"\xE9D5");
                create_task_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    if (!target_uids.empty())
                        CreateTaskFromMessage(target_uids.front());
                });
                menu_items.Append(create_task_item);

                MenuFlyoutItem move_item_dialog;
                move_item_dialog.Text(with_count(Tr(L"Déplacer vers")));
                move_item_dialog.Icon(FontIcon{});
                move_item_dialog.Icon().as<FontIcon>().Glyph(L"èDE");
                move_item_dialog.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ShowMoveFolderDialogAsync(target_uids, false);
                });
                menu_items.Append(move_item_dialog);

                MenuFlyoutItem copy_item_dialog;
                copy_item_dialog.Text(with_count(Tr(L"Copier vers")));
                copy_item_dialog.Icon(FontIcon{});
                copy_item_dialog.Icon().as<FontIcon>().Glyph(L"èC8");
                copy_item_dialog.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ShowMoveFolderDialogAsync(target_uids, true);
                });
                menu_items.Append(copy_item_dialog);

                MenuFlyoutItem archive_item;
                archive_item.Text(with_count(Tr(L"Archiver")));
                archive_item.Icon(FontIcon{});
                archive_item.Icon().as<FontIcon>().Glyph(L"\xE7B8");
                archive_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    ArchiveMessagesByUids(target_uids);
                });
                menu_items.Append(archive_item);

                MenuFlyoutItem junk_item;
                junk_item.Text(with_count(Tr(L"Indésirable / Non indésirable")));
                junk_item.Icon(FontIcon{});
                junk_item.Icon().as<FontIcon>().Glyph(L"\xE7BA");
                junk_item.Click([this, target_uids](IInspectable const&, RoutedEventArgs const&)
                {
                    MarkAsJunkByUids(target_uids);
                });
                menu_items.Append(junk_item);

                menu_items.Append(MenuFlyoutSeparator{});

                MenuFlyoutItem delete_item;
                delete_item.Text(with_count(Tr(L"Supprimer")));
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
                ShowStatus(Fmt(Tr(L"Aucun message ne correspond à « {0} »."), { SearchBox().Text() }), InfoBarSeverity::Informational);
            else
                ShowStatus(Tr(L"Ce dossier est vide."), InfoBarSeverity::Informational);
        }

        UpdateFocusedBarCounts();
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

    /* Répartit des messages par compte et dossier d'origine (un seul groupe hors
     * vue unifiée ou recherche multi-dossiers).
     * Paramètres :
     *   uids : identifiants affichés des messages
     * Retourne : (compte, dossier) -> identifiants des messages. */
    std::map<std::pair<std::wstring, std::wstring>, std::vector<hstring>> MainWindow::GroupUidsByLocation(std::vector<hstring> const& uids)
    {
        std::map<std::pair<std::wstring, std::wstring>, std::vector<hstring>> groups;
        for (auto const& u : uids)
        {
            auto loc = GetMessageLocation(u);
            groups[{ std::wstring{ loc.account }, std::wstring{ loc.folder } }].push_back(u);
        }
        return groups;
    }

    /* Requête d'export de messages pour le moteur : {"account", "folder", "uids"}.
     * Dans une vue unifiée ou une recherche multi-dossiers, les messages peuvent venir
     * de plusieurs dossiers : la requête porte sur le dossier du premier message, seuls
     * les messages de ce dossier sont retenus ; les identifiants sont ramenés à l'uid réel.
     * Paramètres :
     *   uids : identifiants affichés des messages
     * Retourne : objet JSON de la requête. */
    JsonObject MainWindow::BuildMessagesRequest(std::vector<hstring> const& uids)
    {
        MessageLocation first = uids.empty() ? MessageLocation{ m_account, m_folder, L"", L"" } : GetMessageLocation(uids.front());
        JsonArray real_uids;
        for (auto const& u : uids)
        {
            auto loc = GetMessageLocation(u);
            if (loc.account == first.account && loc.folder == first.folder)
                real_uids.Append(JsonValue::CreateStringValue(hstring{ ::EvolutionWin::UnifiedViews::RealUid(std::wstring_view{ u }) }));
        }

        JsonObject request;
        request.SetNamedValue(L"account", JsonValue::CreateStringValue(first.account));
        request.SetNamedValue(L"folder", JsonValue::CreateStringValue(first.folder));
        request.SetNamedValue(L"uids", real_uids);
        return request;
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
            int delay_seconds = ::EvolutionWin::Settings::Instance().GetInt(L"mail.mark-read-delay-seconds", 5);
            if (delay_seconds < 1)
                delay_seconds = 1;
            if (delay_seconds > 60)
                delay_seconds = 60;

            m_pending_read_uid = uid;
            m_mark_read_timer = DispatcherTimer();
            m_mark_read_timer.Interval(std::chrono::seconds(delay_seconds));
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
        if (item && unbox_value_or<hstring>(item.Tag(), L"").empty())
        {
            /* en-tête de groupe de dates : le clic le plie / déplie, il n'est pas sélectionnable */
            m_reselecting = true;
            MessageList().SelectedItem(nullptr);
            m_reselecting = false;
            return;
        }
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
        /* en-tête d'un fil replié : on lit le message le plus récent du fil, sans développer le fil */
        if (auto latest = m_thread_latest.find(std::wstring{ uid }); latest != m_thread_latest.end())
        {
            m_collapsed_threads.insert(latest->second.second);
            uid = hstring{ latest->second.first };
        }
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
        PrepareDragDropCache();

        /* Si le volet de lecture est désactivé, conserver la sélection et les boutons sans charger le WebView2 */
        if (::EvolutionWin::Settings::Instance().GetString(L"layout.reading-pane", L"right") == L"off")
        {
            m_current_message_uid = uid;
            return;
        }

        auto loc = GetMessageLocation(uid);
        hstring target_account = loc.account.empty() ? m_account : loc.account;
        hstring target_folder = loc.folder.empty() ? m_folder : loc.folder;

        m_core->FetchMessage(target_account, target_folder, uid, [this, request, uid, target_account, target_folder](hstring const& json, hstring const& error)
        {
            if (request != m_message_request)
                return;

            if (!error.empty())
            {
                ShowStatus(Tr(L"Message : ") + error, InfoBarSeverity::Error);
                return;
            }

            auto message = JsonObject::Parse(json);
            m_current_message = message;
            m_current_message_uid = uid;
            m_current_message_account = target_account;
            m_current_message_folder = target_folder;
            hstring to = message.GetNamedString(L"to", L"");
            hstring cc = message.GetNamedString(L"cc", L"");

            MessageSubject().Text(message.GetNamedString(L"subject", L""));
            MessageFrom().Text(message.GetNamedString(L"from", L""));
            MessageTo().Text(Tr(L"À : ") + to + (cc.empty() ? hstring{} : L"   " + Tr(L"Cc : ") + cc));
            MessageDate().Text(FormatDate(message.GetNamedNumber(L"date", 0)));
            UpdateSecurityBanner(message.GetNamedObject(L"security", JsonObject{}));
            UpdateReceiptBars(message);
            UpdateInvitationBar(message);
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
        UpdateStatusBarProgress(true, Tr(L"Relève du courrier…"), -1);
        ShowStatus(Tr(L"Relève du courrier…"), InfoBarSeverity::Informational);
        m_core->Refresh();
    }

    void MainWindow::OnRefreshSplitClick(SplitButton const&, SplitButtonClickEventArgs const&)
    {
        OnRefreshClick(nullptr, nullptr);
    }

    /* F9 : tous les dossiers de tous les comptes (progression dans la barre d'état) */
    void MainWindow::OnRefreshAllClick(IInspectable const&, RoutedEventArgs const&)
    {
        UpdateStatusBarProgress(true, Tr(L"Relève de tous les dossiers…"), -1);
        ShowStatus(Tr(L"Relève de tous les dossiers…"), InfoBarSeverity::Informational);
        m_core->RefreshAll();
    }

    /* Maj+F9 : le dossier affiché seulement (la liste interroge le serveur) */
    void MainWindow::OnRefreshFolderClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_folder.empty())
            return;
        hstring updating = Fmt(Tr(L"Mise à jour de « {0} »…"), { FolderTitle().Text() });
        UpdateStatusBarProgress(true, updating, -1);
        ShowStatus(updating, InfoBarSeverity::Informational);
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
                ToolTipService::SetToolTip(bar, box_value(seen ? Tr(L"Marquer comme non lu") : Tr(L"Marquer comme lu")));
            }
            else if (auto text = child.try_as<TextBlock>(); text && Grid::GetColumn(text) == 0 && Grid::GetRow(text) < 2)
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

        ToggleReadButton().Label(seen ? Tr(L"Non lu") : Tr(L"Lu"));
        ToggleReadIcon().Glyph(seen ? L"\xE715" : L"\xE8C3");

        ToggleFlagButton().Label(flagged ? Tr(L"Retirer suivi") : Tr(L"Suivi"));
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
        hstring msg_account = object.GetNamedString(L"account", L"");
        hstring msg_folder = object.GetNamedString(L"folder", L"");

        /* 1. Filtrage par compte : vérifie si les notifications sont activées pour ce compte */
        if (!msg_account.empty())
        {
            hstring acc_key = L"account." + msg_account + L".notifications-enabled";
            if (!::EvolutionWin::Settings::Instance().GetBoolean(acc_key, true))
                return;
        }

        /* 2. Filtrage par dossier : vérifie si les notifications sont activées pour ce dossier */
        if (!msg_account.empty() && !msg_folder.empty())
        {
            hstring fld_key = L"folder." + msg_account + L"." + msg_folder + L".notifications-enabled";
            if (!::EvolutionWin::Settings::Instance().GetBoolean(fld_key, true))
                return;
        }

        int32_t count = static_cast<int32_t>(object.GetNamedNumber(L"count", 1));
        hstring from = SenderName(object.GetNamedString(L"from", Tr(L"Nouveau message")));
        hstring subject = object.GetNamedString(L"subject", Tr(L"(Sans objet)"));

        hstring title = count > 1
            ? Fmt(Tr(L"{0} nouveaux messages"), { to_hstring(count) })
            : Fmt(Tr(L"Nouveau message de {0}"), { from });
        hstring body = count > 1
            ? from + L" — " + subject
            : subject;

        /* Mode confidentiel : ni expéditeur ni objet (présentation, partage d'écran) */
        if (::EvolutionWin::Settings::Instance().GetBoolean(L"notifications.confidential", false))
        {
            title = count > 1 ? Fmt(Tr(L"{0} nouveaux messages"), { to_hstring(count) }) : Tr(L"Nouveau message");
            body = count > 1 ? Tr(L"Ouvrez Evolution pour les lire.") : Tr(L"Ouvrez Evolution pour le lire.");
        }

        /* Ajout dans l'historique du menu déroulant de la barre d'état (notifications discrètes) */
        AddNotificationItem(title, body);

        /* Émission du signal sonore configuré si activé globalement et pour le compte */
        bool sound_global = ::EvolutionWin::Settings::Instance().GetBoolean(L"notifications.sound", true);
        bool sound_account = msg_account.empty() ? true : ::EvolutionWin::Settings::Instance().GetBoolean(L"account." + msg_account + L".sound-enabled", true);
        if (sound_global && sound_account)
        {
            hstring sound_kind = ::EvolutionWin::Settings::Instance().GetString(L"notifications.sound-kind", L"windows");
            hstring custom_path = ::EvolutionWin::Settings::Instance().GetString(L"notifications.custom-sound-path", L"");
            ::EvolutionWin::Helpers::PlayNewMailSound(sound_kind, custom_path);
        }

        /* Émission exclusive de la notification via les Toasts système Windows si activé globalement */
        if (::EvolutionWin::Settings::Instance().GetBoolean(L"notifications.toast", true))
        {
            try
            {
                Microsoft::Windows::AppNotifications::Builder::AppNotificationBuilder builder;
                builder.AddText(title);
                builder.AddText(body);
                if (!msg_account.empty())
                    builder.AddArgument(L"account", msg_account);
                if (!msg_folder.empty())
                    builder.AddArgument(L"folder", msg_folder);
                hstring uid = object.GetNamedString(L"uid", L"");
                if (!uid.empty())
                    builder.AddArgument(L"uid", uid);
                Microsoft::Windows::AppNotifications::AppNotificationManager::Default().Show(builder.BuildNotification());
            }
            catch (...)
            {
            }
        }
    }

    bool MainWindow::IsDraftsFolder()
    {
        if (IsUnifiedView())
            return m_folder == L"drafts";

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
        if (IsUnifiedView())
            return m_folder == L"trash";

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
            confirm.Title(box_value(Tr(L"Supprimer définitivement ?")));
            TextBlock text;
            text.TextWrapping(TextWrapping::Wrap);
            if (uids.size() == 1)
            {
                text.Text(Tr(L"Le message sélectionné sera définitivement supprimé. Cette opération est irréversible."));
            }
            else
            {
                text.Text(Fmt(Tr(L"{0} messages sélectionnés seront définitivement supprimés. Cette opération est irréversible."), { to_hstring(uids.size()) }));
            }
            confirm.Content(text);
            confirm.PrimaryButtonText(Tr(L"Supprimer définitivement"));
            confirm.CloseButtonText(Tr(L"Annuler"));
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
            auto draft_loc = GetMessageLocation(uid);
            m_core->FetchMessage(draft_loc.account, draft_loc.folder, uid, [this, draft_account = draft_loc.account](hstring const& json, hstring const& error)
            {
                if (!error.empty())
                {
                    ShowStatus(Tr(L"Impossible d'ouvrir le brouillon : ") + error, InfoBarSeverity::Error);
                    return;
                }
                JsonObject msg;
                if (!JsonObject::TryParse(json, msg))
                    return;

                ::EvolutionWin::ComposeData data;
                data.account = draft_account;
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
    /**
     * @brief Pré-génère en tâche de fond les fichiers .eml des messages sélectionnés pour le glisser-déposer.
     */
    void MainWindow::PrepareDragDropCache()
    {
        auto sel_uids = SelectedMessageUids();
        if (sel_uids.empty())
        {
            hstring cur = SelectedMessageUid();
            if (!cur.empty())
                sel_uids.push_back(cur);
        }

        if (sel_uids.empty() || m_account.empty() || m_folder.empty() || !m_core)
            return;

        /* un lot par compte et dossier d'origine (vues unifiées, recherche multi-dossiers) ;
           le moteur ne connaît que l'identifiant réel des messages */
        std::map<std::pair<std::wstring, std::wstring>, JsonArray> batches;
        for (auto const& u : sel_uids)
        {
            auto loc = GetMessageLocation(u);
            batches[{ std::wstring{ loc.account }, std::wstring{ loc.folder } }].Append(
                JsonValue::CreateStringValue(hstring{ ::EvolutionWin::UnifiedViews::RealUid(std::wstring_view{ u }) }));
        }

        m_drag_cached_files.clear();
        for (auto const& [location, uids] : batches)
        {
            JsonObject request;
            request.SetNamedValue(L"account", JsonValue::CreateStringValue(hstring{ location.first }));
            request.SetNamedValue(L"folder", JsonValue::CreateStringValue(hstring{ location.second }));
            request.SetNamedValue(L"uids", uids);

            m_core->ExportMessagesEml(request.Stringify(), [this](hstring const& json, hstring const& error)
            {
                JsonArray paths;
                if (error.empty() && JsonArray::TryParse(json, paths))
                {
                    for (auto const& p : paths)
                        m_drag_cached_files.push_back(std::wstring{ p.GetString() });
                }
            });
        }
    }

    /**
     * @brief Déclenche l'exportation par glisser-déposer (Drag & Drop) d'un ou plusieurs courriels vers l'Explorateur Windows ou le bureau.
     * @param sender Liste de messages émettrice.
     * @param args Arguments décrivant l'opération de glisser-déposer et son DataPackage.
     */
    void MainWindow::OnMessageListDragItemsStarting(
        [[maybe_unused]] IInspectable const& sender,
        DragItemsStartingEventArgs const& args)
    {
        auto sel_uids = SelectedMessageUids();
        if (sel_uids.empty())
        {
            hstring cur_uid = SelectedMessageUid();
            if (!cur_uid.empty())
                sel_uids.push_back(cur_uid);
        }

        /* l'élément réellement glissé peut ne pas faire partie de la sélection : il prime alors */
        {
            std::vector<hstring> dragged;
            for (auto const& entry : args.Items())
                if (auto grid = entry.try_as<Grid>())
                {
                    hstring u = unbox_value_or<hstring>(grid.Tag(), L"");
                    if (!u.empty())
                        dragged.push_back(u);
                }
            bool in_selection = false;
            for (auto const& u : dragged)
                if (std::find(sel_uids.begin(), sel_uids.end(), u) != sel_uids.end())
                    in_selection = true;
            if (!dragged.empty() && !in_selection)
                sel_uids = std::move(dragged);
        }

        if (sel_uids.empty() || m_account.empty() || m_folder.empty())
            return;

        /* l'en-tête d'une conversation (repliée ou non) est glissée en entier : tous ses messages suivent l'en-tête */
        {
            std::vector<hstring> expanded_uids;
            std::set<std::wstring> seen;
            for (auto const& u : sel_uids)
            {
                auto members = m_thread_members.find(std::wstring{ u });
                if (members == m_thread_members.end())
                {
                    if (seen.insert(std::wstring{ u }).second)
                        expanded_uids.push_back(u);
                    continue;
                }
                for (auto const& member : members->second)
                    if (seen.insert(member).second)
                        expanded_uids.push_back(hstring{ member });
            }
            sel_uids = std::move(expanded_uids);
        }

        /* mémorise les messages pour un dépôt sur un dossier de l'arborescence (déplacement) */
        m_drag_move_uids = sel_uids;

        std::wstring temp_dir = ::EvolutionWin::Helpers::GetDragDropTempDir();
        std::vector<Windows::Storage::IStorageItem> storage_items;

        /* 1. Utilisation prioritaire des fichiers réels complets pré-générés en cache */
        if (!m_drag_cached_files.empty())
        {
            for (auto const& path_str : m_drag_cached_files)
            {
                try
                {
                    if (std::filesystem::exists(path_str))
                    {
                        auto file = Windows::Storage::StorageFile::GetFileFromPathAsync(path_str).get();
                        if (file)
                            storage_items.push_back(file);
                    }
                }
                catch (...)
                {
                }
            }
        }

        /* 2. Repli immédiat si le cache n'était pas encore synchronisé */
        if (storage_items.empty())
        {
            for (auto const& u : sel_uids)
            {
                hstring subject = L"message";
                for (auto const& val : m_all_messages ? m_all_messages : JsonArray{})
                {
                    auto obj = val.GetObject();
                    if (obj.GetNamedString(L"uid", L"") == u)
                    {
                        subject = obj.GetNamedString(L"subject", L"message");
                        break;
                    }
                }
                hstring file_name = ::EvolutionWin::Helpers::SanitizeFileName(subject, L"message") + L".eml";
                std::filesystem::path eml_path = std::filesystem::path{ temp_dir } / std::wstring{ file_name };

                std::ofstream ofs(eml_path, std::ios::binary);
                if (ofs.is_open())
                {
                    ofs << "Subject: " << winrt::to_string(subject) << "\r\n";
                    ofs << "X-Evolution-UID: " << winrt::to_string(u) << "\r\n";
                    ofs << "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n";
                    ofs << "(Courriel exporté depuis Evolution)\r\n";
                    ofs.close();
                }

                try
                {
                    auto file = Windows::Storage::StorageFile::GetFileFromPathAsync(eml_path.wstring()).get();
                    if (file)
                        storage_items.push_back(file);
                }
                catch (...)
                {
                }
            }
        }

        if (!storage_items.empty())
        {
            args.Data().SetStorageItems(storage_items);
            args.Data().RequestedOperation(
                Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy |
                Windows::ApplicationModel::DataTransfer::DataPackageOperation::Move);
        }
    }

    /**
     * @brief Neutralise le visuel de glisser d'un élément de la liste : le système affichait un instantané
     *        du texte du message sous le pointeur ; on le remplace par une image transparente d'un pixel.
     * @param args Arguments du conteneur en cours de préparation.
     */
    void MainWindow::OnMessageListContainerChanging(
        [[maybe_unused]] ListViewBase const& sender,
        ContainerContentChangingEventArgs const& args)
    {
        auto container = args.ItemContainer();
        if (!container || !m_drag_hooked_containers.insert(winrt::get_abi(container)).second)
            return;
        container.DragStarting([](UIElement const&, DragStartingEventArgs const& e)
        {
            Windows::Graphics::Imaging::SoftwareBitmap blank(
                Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8, 1, 1,
                Windows::Graphics::Imaging::BitmapAlphaMode::Premultiplied);
            e.DragUI().SetContentFromSoftwareBitmap(blank);
        });
    }

    /**
     * @brief Fin du glisser-déposer : oublie les messages mémorisés et retire le surlignage du dossier cible.
     */
    void MainWindow::OnMessageListDragItemsCompleted(
        [[maybe_unused]] ListViewBase const& sender,
        [[maybe_unused]] DragItemsCompletedEventArgs const& args)
    {
        m_drag_move_uids.clear();
        SetFolderDropHighlight(nullptr);
    }
}
