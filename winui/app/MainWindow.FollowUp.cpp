#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "FollowUp.h"
#include <chrono>
#include <ctime>

/* Suivi des messages : libellé, échéance, marquage « terminé », effacement. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::EvolutionWin::implementation
{
    /**
     * @brief Calcule l'échéance (minuit local, en secondes Unix) d'un choix de suivi.
     * @param choice Choix d'échéance.
     * @return Secondes Unix, 0 pour « sans date ».
     */
    static int64_t FollowUpDueEpochFor(::EvolutionWin::FollowUp::Due choice)
    {
        std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        int days = ::EvolutionWin::FollowUp::DueOffsetDays(choice, local.tm_wday);
        if (days < 0)
            return 0;
        local.tm_hour = 0;
        local.tm_min = 0;
        local.tm_sec = 0;
        local.tm_mday += days;
        local.tm_isdst = -1;
        return static_cast<int64_t>(std::mktime(&local));
    }

    /**
     * @brief Applique l'aspect du suivi (couleur, info-bulle) à l'icône de drapeau d'un message.
     * @param icon Icône de drapeau de la ligne.
     * @param state État du message (libellé, échéance, fin).
     */
    void MainWindow::StyleFollowUpIcon(FontIcon const& icon, MessageState const& state)
    {
        using Windows::UI::Color;
        if (!icon)
            return;
        if (state.follow_up.empty())
        {
            icon.Glyph(L"\xE7C1");
            icon.Foreground(SolidColorBrush{ Color{ 255, 247, 99, 12 } });
            ToolTipService::SetToolTip(icon, box_value(L"Message suivi"));
            return;
        }

        int64_t now = static_cast<int64_t>(std::time(nullptr));
        bool done = state.completed_on != 0;
        bool overdue = ::EvolutionWin::FollowUp::IsOverdue(state.due_by, state.completed_on, now);

        icon.Glyph(done ? L"\xE73E" : L"\xE7C1");
        if (done)
            icon.Foreground(SolidColorBrush{ Color{ 255, 16, 124, 16 } });
        else if (overdue || state.follow_up == L"Urgent")
            icon.Foreground(SolidColorBrush{ Color{ 255, 232, 17, 35 } });
        else
            icon.Foreground(SolidColorBrush{ Color{ 255, 247, 99, 12 } });

        std::wstring tip = state.follow_up;
        if (done)
            tip += L" — terminé";
        else if (state.due_by > 0)
        {
            std::time_t due = static_cast<std::time_t>(state.due_by);
            std::tm tm{};
            localtime_s(&tm, &due);
            wchar_t buf[64]{};
            wcsftime(buf, 63, L"%d/%m/%Y", &tm);
            tip += std::wstring{ L" — échéance " } + buf + (overdue ? L" (dépassée)" : L"");
        }
        ToolTipService::SetToolTip(icon, box_value(hstring{ tip }));
    }

    /**
     * @brief Applique un suivi à des messages : met à jour l'interface puis le moteur.
     * @param uids Messages concernés.
     * @param label Libellé du suivi ; vide pour effacer le suivi.
     * @param due_choice Choix d'échéance (::EvolutionWin::FollowUp::Due converti en entier).
     * @param completed true pour marquer le suivi comme terminé (conserve libellé et échéance existants).
     */
    void MainWindow::ApplyFollowUp(std::vector<hstring> const& uids, std::wstring const& label, int due_choice, bool completed)
    {
        for (auto const& uid : uids)
        {
            if (uid.empty())
                continue;

            auto& state = m_message_states[std::wstring{ uid }];
            std::wstring new_label = label;
            int64_t due = 0;
            int64_t done = 0;

            if (completed)
            {
                /* terminer : conserve libellé et échéance, ajoute la date de fin */
                new_label = state.follow_up.empty() ? L"À traiter" : state.follow_up;
                due = state.due_by;
                done = static_cast<int64_t>(std::time(nullptr));
            }
            else if (!label.empty())
            {
                due = FollowUpDueEpochFor(static_cast<::EvolutionWin::FollowUp::Due>(due_choice));
            }

            state.follow_up = new_label;
            state.due_by = due;
            state.completed_on = done;
            state.flagged = !new_label.empty();

            if (auto item = FindMessageItem(uid))
            {
                UpdateMessageItemVisual(item, state.seen, state.flagged);
                for (auto const& child : item.Children())
                {
                    if (auto meta = child.try_as<StackPanel>())
                        for (auto const& sub : meta.Children())
                            if (auto icon = sub.try_as<FontIcon>(); icon && unbox_value_or<hstring>(icon.Tag(), L"") == L"follow-flag")
                                StyleFollowUpIcon(icon, state);
                }
            }
            if (uid == SelectedMessageUid())
                UpdateActionButtons(uid);

            auto loc = GetMessageLocation(uid);
            hstring account = loc.account.empty() ? m_account : loc.account;
            hstring folder = loc.folder.empty() ? m_folder : loc.folder;
            m_core->SetFollowUp(account, folder, uid, hstring{ new_label },
                due > 0 ? hstring{ std::to_wstring(due) } : hstring{},
                done > 0 ? hstring{ std::to_wstring(done) } : hstring{},
                [this](hstring const&, hstring const& error)
            {
                if (!error.empty())
                    ShowStatus(L"Suivi : " + error, InfoBarSeverity::Error);
            });
        }
    }

    /**
     * @brief Remplit un menu avec les libellés de suivi, les échéances, « Terminé » et « Effacer ».
     * @param items Collection d'éléments du menu à compléter.
     * @param uids Messages ciblés par les commandes.
     */
    void MainWindow::PopulateFollowUpMenuItems(Collections::IVector<MenuFlyoutItemBase> const& items, std::vector<hstring> const& uids)
    {
        using ::EvolutionWin::FollowUp::Due;

        struct DueEntry { Due due; const wchar_t* text; };
        static const DueEntry dues[] = {
            { Due::Today, L"Aujourd'hui" }, { Due::Tomorrow, L"Demain" },
            { Due::ThisWeek, L"Cette semaine" }, { Due::NextWeek, L"La semaine prochaine" },
            { Due::None, L"Sans date" } };

        for (auto const& label : ::EvolutionWin::FollowUp::DefaultLabels())
        {
            MenuFlyoutSubItem sub;
            sub.Text(hstring{ label });
            for (auto const& d : dues)
            {
                MenuFlyoutItem entry;
                entry.Text(d.text);
                int choice = static_cast<int>(d.due);
                entry.Click([this, uids, label, choice](IInspectable const&, RoutedEventArgs const&)
                {
                    ApplyFollowUp(uids, label, choice, false);
                });
                sub.Items().Append(entry);
            }
            items.Append(sub);
        }

        items.Append(MenuFlyoutSeparator{});

        MenuFlyoutItem done_item;
        done_item.Text(L"Marquer comme terminé");
        done_item.Click([this, uids](IInspectable const&, RoutedEventArgs const&)
        {
            ApplyFollowUp(uids, L"", 0, true);
        });
        items.Append(done_item);

        MenuFlyoutItem clear_item;
        clear_item.Text(L"Effacer le suivi");
        clear_item.Click([this, uids](IInspectable const&, RoutedEventArgs const&)
        {
            ApplyFollowUp(uids, L"", static_cast<int>(Due::None), false);
        });
        items.Append(clear_item);
    }

    /**
     * @brief Bouton « Suivi » du volet de lecture : ouvre le menu des libellés et échéances.
     * @param sender Bouton déclencheur.
     */
    void MainWindow::OnFollowUpButtonClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto uids = SelectedMessageUids();
        if (uids.empty())
            return;

        MenuFlyout flyout;
        PopulateFollowUpMenuItems(flyout.Items(), uids);
        flyout.ShowAt(sender.as<FrameworkElement>());
    }
}
