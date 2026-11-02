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

/* Tâches : liste, filtres, tri et éditeur. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
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
            UpdateTodoBar();
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
            UpdateTodoBar();
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
        /**
     * @brief Détermine le nom du compte et l'adresse email associée à une tâche.
     * 
     * Résout d'abord via les champs JSON de la tâche (account_name, account_email),
     * puis via la source correspondante dans m_task_lists, et enfin par
     * correspondance dans m_accounts (par UID de compte ou par nom).
     * 
     * @param task Données JSON de la tâche.
     * @param out_name Nom du compte résolu en sortie.
     * @param out_email Adresse email associée en sortie.
     */
    void MainWindow::GetTaskAccountInfo(JsonObject const& task, hstring& out_name, hstring& out_email)
    {
        out_name = task.GetNamedString(L"account_name", L"");
        out_email = task.GetNamedString(L"account_email", L"");
        hstring list_uid = task.GetNamedString(L"list_uid", L"");

        if (out_name.empty() || out_email.empty())
        {
            if (m_task_lists)
            {
                for (auto const& item : m_task_lists)
                {
                    auto obj = item.GetObject();
                    if (obj.GetNamedString(L"uid", L"") == list_uid)
                    {
                        if (out_name.empty())
                            out_name = obj.GetNamedString(L"account", L"");
                        if (out_email.empty())
                            out_email = obj.GetNamedString(L"account_email", L"");
                        break;
                    }
                }
            }
        }

        if (out_email.empty() && m_accounts)
        {
            hstring acc_uid = task.GetNamedString(L"account_uid", L"");
            for (auto const& item : m_accounts)
            {
                auto acc = item.GetObject();
                hstring a_name = acc.GetNamedString(L"name", L"");
                hstring a_uid = acc.GetNamedString(L"uid", L"");
                if ((!acc_uid.empty() && a_uid == acc_uid) ||
                    (!out_name.empty() && a_name == out_name))
                {
                    out_email = acc.GetNamedString(L"address", L"");
                    if (out_name.empty())
                        out_name = a_name;
                    break;
                }
            }
        }

        if (out_name.empty())
        {
            out_name = task.GetNamedString(L"list_name", L"Compte local");
        }
    }

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

        /* Compte source de la tâche (nom du compte et adresse email associée) */
        hstring account_name, account_email;
        GetTaskAccountInfo(task, account_name, account_email);

        Border account_badge;
        account_badge.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
        account_badge.Padding(ThicknessHelper::FromLengths(6, 2, 6, 2));
        account_badge.Background(resources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<Brush>());
        account_badge.BorderBrush(resources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).as<Brush>());
        account_badge.BorderThickness(ThicknessHelper::FromUniformLength(1));
        account_badge.HorizontalAlignment(HorizontalAlignment::Left);
        account_badge.Margin(ThicknessHelper::FromLengths(0, 1, 0, 2));

        StackPanel badge_panel;
        badge_panel.Orientation(Orientation::Horizontal);
        badge_panel.Spacing(5);

        FontIcon badge_icon;
        badge_icon.FontSize(11);
        badge_icon.Glyph(account_email.empty() ? L"\xE977" : L"\xE715");
        badge_icon.Foreground(secondary_brush);
        badge_panel.Children().Append(badge_icon);

        TextBlock badge_text;
        badge_text.Style(caption_style);
        if (!account_email.empty())
            badge_text.Text(account_name + L" • " + account_email);
        else
            badge_text.Text(account_name);
        badge_panel.Children().Append(badge_text);
        account_badge.Child(badge_panel);
        details.Children().Append(account_badge);

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

        bool is_preset_new = task && task.GetNamedBoolean(L"is_preset_new", false);
        bool is_new = !task || is_preset_new;
        bool readonly = !is_new && task.GetNamedBoolean(L"readonly", false);
        auto caption_style = Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>();
        auto secondary_brush = Application::Current().Resources().Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Brush>();

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
                    hstring target_list = task ? task.GetNamedString(L"list_uid", m_task_list_uid) : m_task_list_uid;
                    if (list_uids.back() == target_list)
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

        /* Compte source associé à la tâche */
        hstring ed_acc_name, ed_acc_email;
        GetTaskAccountInfo(task ? task : JsonObject{}, ed_acc_name, ed_acc_email);
        if (!ed_acc_name.empty())
        {
            TextBlock acc_info_block;
            std::wstring acc_txt = L"Compte : " + std::wstring{ ed_acc_name };
            if (!ed_acc_email.empty())
                acc_txt += L" (" + std::wstring{ ed_acc_email } + L")";
            acc_info_block.Text(hstring{ acc_txt });
            acc_info_block.Style(caption_style);
            acc_info_block.Foreground(secondary_brush);
            panel.Children().Append(acc_info_block);
        }

        TextBox summary_box;
        summary_box.Header(box_value(L"Objet"));
        summary_box.PlaceholderText(L"Que faut-il faire ?");
        if (task)
            summary_box.Text(task.GetNamedString(L"summary", L""));
        panel.Children().Append(summary_box);

        TextBox desc_box;
        desc_box.Header(box_value(L"Description"));
        desc_box.AcceptsReturn(true);
        desc_box.TextWrapping(TextWrapping::Wrap);
        desc_box.Height(90);
        if (task)
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
            double value = (!task) ? 0 : task.GetNamedNumber(key, 0);
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
        int priority_index = (!task) ? 0 : TaskPriorityIndex(static_cast<int>(task.GetNamedNumber(L"priority", 0)));
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

    /**
     * @brief Gestionnaire de clic du bouton « Créer une tâche » depuis un message.
     * 
     * Récupère le message actuellement sélectionné et déclenche la création d'une tâche.
     * 
     * @param sender Élément déclencheur.
     * @param args Arguments d'événement de routage.
     */
    void MainWindow::OnCreateTaskFromMessageClick(IInspectable const&, RoutedEventArgs const&)
    {
        hstring uid = SelectedMessageUid();
        if (uid.empty())
        {
            ShowStatus(L"Veuillez d'abord sélectionner un message.", InfoBarSeverity::Warning);
            return;
        }
        CreateTaskFromMessage(uid);
    }

    /**
     * @brief Crée une nouvelle tâche préremplie à partir d'un message électronique.
     * 
     * Extrait l'objet, l'expéditeur, la date et le corps ou l'extrait du message pour
     * initialiser le résumé, la description, l'échéance par défaut (J+1) et la priorité,
     * puis ouvre l'éditeur de tâche pour validation par l'utilisateur.
     * 
     * @param message_uid Identifiant unique du message source.
     */
    void MainWindow::CreateTaskFromMessage(hstring const& message_uid)
    {
        if (message_uid.empty())
            return;

        JsonObject msg{ nullptr };
        if (m_all_messages)
        {
            for (auto const& val : m_all_messages)
            {
                auto obj = val.GetObject();
                if (obj.GetNamedString(L"uid", L"") == message_uid)
                {
                    msg = obj;
                    break;
                }
            }
        }

        hstring subject = msg ? msg.GetNamedString(L"subject", L"(Sans objet)") : L"(Sans objet)";
        hstring from = msg ? msg.GetNamedString(L"from", L"") : L"";
        hstring date_str = msg ? msg.GetNamedString(L"date", L"") : L"";
        hstring preview = msg ? msg.GetNamedString(L"preview", L"") : L"";

        /* Si le message affiché est celui-ci, utiliser son sujet et texte détaillé */
        if (m_current_message && m_current_message.GetNamedString(L"uid", L"") == message_uid)
        {
            if (subject.empty() || subject == L"(Sans objet)")
                subject = m_current_message.GetNamedString(L"subject", subject);
            if (from.empty())
                from = m_current_message.GetNamedString(L"from", from);
            hstring body = m_current_message.GetNamedString(L"body_text", L"");
            if (!body.empty())
                preview = body;
        }

        std::wstring desc;
        if (!from.empty())
            desc += L"De : " + std::wstring{ from } + L"\n";
        if (!date_str.empty())
            desc += L"Date : " + std::wstring{ date_str } + L"\n";
        if (!subject.empty())
            desc += L"Objet : " + std::wstring{ subject } + L"\n";
        desc += L"\n--- Contenu du message ---\n";
        if (!preview.empty())
            desc += std::wstring{ preview };

        bool is_flagged = false;
        auto state_it = m_message_states.find(std::wstring{ message_uid });
        if (state_it != m_message_states.end())
            is_flagged = state_it->second.flagged;

        /* Échéance par défaut : minuit du lendemain (J+1) */
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        int64_t default_due = LocalMidnight(now, 1);

        /* Liste de tâches associée au compte courant */
        hstring target_list_uid = m_task_list_uid;
        if (m_task_lists)
        {
            for (auto const& item : m_task_lists)
            {
                auto list_obj = item.GetObject();
                hstring acc_name = list_obj.GetNamedString(L"account", L"");
                hstring acc_uid = list_obj.GetNamedString(L"account_uid", L"");
                if (acc_uid == m_account || acc_name == m_account)
                {
                    target_list_uid = list_obj.GetNamedString(L"uid", L"");
                    break;
                }
            }
        }

        JsonObject preset_task;
        preset_task.SetNamedValue(L"is_preset_new", JsonValue::CreateBooleanValue(true));
        preset_task.SetNamedValue(L"summary", JsonValue::CreateStringValue(subject));
        preset_task.SetNamedValue(L"description", JsonValue::CreateStringValue(hstring{ desc }));
        preset_task.SetNamedValue(L"due", JsonValue::CreateNumberValue(static_cast<double>(default_due)));
        preset_task.SetNamedValue(L"due_date_only", JsonValue::CreateBooleanValue(true));
        preset_task.SetNamedValue(L"priority", JsonValue::CreateNumberValue(is_flagged ? 1.0 : 5.0));
        if (!target_list_uid.empty())
            preset_task.SetNamedValue(L"list_uid", JsonValue::CreateStringValue(target_list_uid));

        ShowTaskEditorAsync(preset_task);
    }

    /**
     * @brief Crée une nouvelle tâche préremplie à partir d'un événement de calendrier.
     * 
     * Extrait le titre, le lieu, la date et la description de l'événement pour
     * initialiser le résumé, la description et l'échéance de la tâche, puis
     * ouvre l'éditeur de tâche pour validation.
     * 
     * @param event_data Objet JSON décrivant l'événement du calendrier.
     */
    void MainWindow::CreateTaskFromEvent(JsonObject const& event_data)
    {
        if (!event_data)
            return;

        hstring summary = event_data.GetNamedString(L"summary", L"(Sans titre)");
        hstring location = event_data.GetNamedString(L"location", L"");
        hstring event_desc = event_data.GetNamedString(L"description", L"");
        double start = event_data.GetNamedNumber(L"start", 0);

        std::wstring desc;
        if (!location.empty())
            desc += L"Lieu : " + std::wstring{ location } + L"\n";
        if (start > 0)
            desc += L"Date de l'événement : " + std::wstring{ FormatTaskDate(start, false) } + L"\n";
        if (!event_desc.empty())
        {
            if (!desc.empty())
                desc += L"\n";
            desc += std::wstring{ event_desc };
        }

        JsonObject preset_task;
        preset_task.SetNamedValue(L"is_preset_new", JsonValue::CreateBooleanValue(true));
        preset_task.SetNamedValue(L"summary", JsonValue::CreateStringValue(summary));
        preset_task.SetNamedValue(L"description", JsonValue::CreateStringValue(hstring{ desc }));
        if (start > 0)
        {
            preset_task.SetNamedValue(L"due", JsonValue::CreateNumberValue(start));
            preset_task.SetNamedValue(L"due_date_only", JsonValue::CreateBooleanValue(event_data.GetNamedBoolean(L"all_day", false)));
        }
        preset_task.SetNamedValue(L"priority", JsonValue::CreateNumberValue(5.0));

        ShowTaskEditorAsync(preset_task);
    }

    /* -------------------------------------------------------------------- */
    /* Barre latérale des tâches (To-Do bar façon Outlook)                 */
    /* -------------------------------------------------------------------- */

    /**
     * @brief Formate une date d'échéance relative adaptée au volet compact des tâches.
     * 
     * Retourne « Aujourd'hui », « Demain », « En retard (X j) », ou la date formatée.
     * 
     * @param unix_time Date d'échéance en secondes Unix.
     * @param date_only Indique si l'heure est ignorée.
     * @param is_overdue Booléen positionné à true en sortie si l'échéance est dépassée.
     * @return Libellé relatif en français.
     */
    static hstring FormatRelativeDueDate(double unix_time, bool /*date_only*/, bool& is_overdue)
    {
        if (unix_time <= 0)
        {
            is_overdue = false;
            return L"Pas d'échéance";
        }

        int64_t now = static_cast<int64_t>(std::time(nullptr));
        int64_t due = static_cast<int64_t>(unix_time);
        int64_t today_start = LocalMidnight(now);
        int64_t today_end = LocalMidnight(now, 1);
        int64_t tomorrow_end = LocalMidnight(now, 2);

        if (due < today_start)
        {
            is_overdue = true;
            int days_late = static_cast<int>((today_start - due) / 86400) + 1;
            if (days_late <= 1)
                return L"Hier (En retard)";
            return L"En retard (" + to_hstring(days_late) + L" j)";
        }

        is_overdue = false;
        if (due >= today_start && due < today_end)
            return L"Aujourd'hui";
        if (due >= today_end && due < tomorrow_end)
            return L"Demain";

        Windows::Globalization::DateTimeFormatting::DateTimeFormatter formatter{ L"shortdate" };
        return formatter.Format(winrt::clock::from_time_t(static_cast<time_t>(due)));
    }

    /**
     * @brief Rétablit les réglages mémorisés de la barre latérale des tâches au démarrage.
     * 
     * Charge l'état de visibilité (layout.todobar-visible) et la largeur (layout.todobar-width),
     * puis applique la disposition.
     */
    void MainWindow::RestoreTodoBarSettings()
    {
        auto& settings = ::EvolutionWin::Settings::Instance();
        m_todobar_configured_visible = settings.GetBoolean(L"layout.todobar-visible", false);
        m_todobar_width = std::clamp(settings.GetNumber(L"layout.todobar-width", 300.0), 220.0, 550.0);

        SetTodoBarVisible(m_todobar_configured_visible, false);
    }

    /**
     * @brief Indique si le volet latéral des tâches est actuellement visible.
     * 
     * @return true si le volet est affiché, false s'il est masqué.
     */
    bool MainWindow::IsTodoBarVisible()
    {
        return TodoBarPane().Visibility() == Visibility::Visible;
    }

    /**
     * @brief Active ou masque le volet latéral des tâches.
     * 
     * Met à jour la largeur de colonne, la visibilité du panneau et du séparateur,
     * l'état des boutons du ruban et enregistre le choix dans les préférences.
     * 
     * @param visible true pour afficher le volet, false pour le masquer.
     * @param save true pour persister le réglage dans settings.json.
     */
    void MainWindow::SetTodoBarVisible(bool visible, bool save)
    {
        m_todobar_configured_visible = visible;
        if (save)
            ::EvolutionWin::Settings::Instance().SetBoolean(L"layout.todobar-visible", visible);

        UpdateTodoBarVisibility();

        if (visible)
        {
            if (!m_all_tasks)
                LoadTasks();
            else
                UpdateTodoBar();
        }
    }

    /**
     * @brief Ajuste la visibilité du volet selon la vue active et la configuration utilisateur.
     * 
     * Le volet est visible dans Courrier, Calendrier et Contacts si l'utilisateur l'a activé.
     * Il est automatiquement replié dans la vue Tâches complète et dans les Paramètres.
     */
    void MainWindow::UpdateTodoBarVisibility()
    {
        bool allowed_view = (m_view_tag == L"mail" || m_view_tag == L"calendar" || m_view_tag == L"contacts");
        bool should_show = m_todobar_configured_visible && allowed_view;

        TodoBarPane().Visibility(should_show ? Visibility::Visible : Visibility::Collapsed);
        TodoBarSplitter().Visibility(should_show ? Visibility::Visible : Visibility::Collapsed);
        TodoBarColumn().Width(should_show ? GridLengthHelper::FromPixels(m_todobar_width) : GridLengthHelper::FromPixels(0));

        RibbonMailTodoBarToggle().IsChecked(should_show);
        RibbonCalendarTodoBarToggle().IsChecked(should_show);
        RibbonContactsTodoBarToggle().IsChecked(should_show);
    }

    /**
     * @brief Met à jour le contenu de la liste des tâches dans la barre latérale.
     * 
     * Filtre les tâches non terminées, les trie (en retard, aujourd'hui, plus tard, sans date)
     * et rafraîchit le compteur du volet.
     */
    void MainWindow::UpdateTodoBar()
    {
        if (!m_all_tasks || TodoBarPane().Visibility() != Visibility::Visible)
            return;

        auto items = TodoBarListView().Items();
        items.Clear();

        int64_t now = static_cast<int64_t>(std::time(nullptr));
        std::vector<JsonObject> open_tasks;

        for (auto const& val : m_all_tasks)
        {
            auto task = val.GetObject();
            hstring status = task.GetNamedString(L"status", L"");
            if (status == L"completed" || status == L"cancelled")
                continue;
            open_tasks.push_back(task);
        }

        /* Tri spécial To-Do bar : en retard en haut, puis par échéance la plus proche,
         * puis priorité décroissante (1 haute -> 5 normale -> 9 basse) */
        std::stable_sort(open_tasks.begin(), open_tasks.end(), [now](JsonObject const& a, JsonObject const& b)
        {
            bool overdue_a = IsTaskOverdue(a, now);
            bool overdue_b = IsTaskOverdue(b, now);
            if (overdue_a != overdue_b)
                return overdue_a;

            double due_a = a.GetNamedNumber(L"due", 0);
            double due_b = b.GetNamedNumber(L"due", 0);
            bool has_due_a = due_a > 0;
            bool has_due_b = due_b > 0;

            if (has_due_a != has_due_b)
                return has_due_a;
            if (has_due_a && has_due_b && due_a != due_b)
                return due_a < due_b;

            double prio_a = a.GetNamedNumber(L"priority", 0);
            double prio_b = b.GetNamedNumber(L"priority", 0);
            double key_a = prio_a > 0 ? prio_a : 10.0;
            double key_b = prio_b > 0 ? prio_b : 10.0;
            return key_a < key_b;
        });

        // Limite à 15 tâches dans la liste rapide
        size_t display_count = (std::min)(open_tasks.size(), static_cast<size_t>(15));
        for (size_t i = 0; i < display_count; ++i)
        {
            items.Append(BuildTodoBarRow(open_tasks[i]));
        }

        // Compteur en pied de volet
        size_t total_open = open_tasks.size();
        if (total_open == 0)
        {
            TodoBarCountText().Text(L"Aucune tâche en attente");
        }
        else if (total_open == 1)
        {
            TodoBarCountText().Text(L"1 tâche en cours");
        }
        else
        {
            TodoBarCountText().Text(to_hstring(total_open) + L" tâches en cours");
        }
    }

    /**
     * @brief Construit une ligne compacte de tâche pour la barre latérale To-Do bar.
     * 
     * Contient la case à cocher, le titre, l'échéance relative et le badge de compte.
     * 
     * @param task Données JSON de la tâche.
     * @return Élément graphique Grid prêt pour la ListView.
     */
    UIElement MainWindow::BuildTodoBarRow(JsonObject const& task)
    {
        auto resources = Application::Current().Resources();
        auto caption_style = resources.Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>();
        auto secondary_brush = resources.Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Brush>();

        Grid row;
        row.Padding(ThicknessHelper::FromLengths(4, 5, 4, 5));
        row.ColumnSpacing(8);
        row.Tag(box_value(task.Stringify()));

        ColumnDefinition check_col;
        check_col.Width(GridLengthHelper::Auto());
        row.ColumnDefinitions().Append(check_col);
        row.ColumnDefinitions().Append(ColumnDefinition{});

        CheckBox done;
        done.IsChecked(false);
        done.MinWidth(0);
        done.Padding(ThicknessHelper::FromUniformLength(0));
        done.VerticalAlignment(VerticalAlignment::Center);
        ToolTipService::SetToolTip(done, box_value(L"Marquer comme terminée"));
        done.Click([this, task](IInspectable const&, RoutedEventArgs const&)
        {
            SetTaskCompleted(task, true);
        });
        row.Children().Append(done);

        StackPanel details;
        details.Spacing(1);
        details.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(details, 1);

        TextBlock title;
        title.Text(task.GetNamedString(L"summary", L"(Sans titre)"));
        title.Style(resources.Lookup(box_value(L"BodyTextBlockStyle")).as<Style>());
        title.TextTrimming(TextTrimming::CharacterEllipsis);
        title.MaxLines(1);
        details.Children().Append(title);

        StackPanel subline;
        subline.Orientation(Orientation::Horizontal);
        subline.Spacing(6);

        double due = task.GetNamedNumber(L"due", 0);
        bool is_overdue = false;
        hstring relative_due = FormatRelativeDueDate(due, task.GetNamedBoolean(L"due_date_only", true), is_overdue);

        TextBlock due_text;
        due_text.Style(caption_style);
        due_text.Text(relative_due);
        if (is_overdue)
            due_text.Foreground(resources.Lookup(box_value(L"SystemFillColorCriticalBrush")).as<Brush>());
        else
            due_text.Foreground(secondary_brush);
        subline.Children().Append(due_text);

        double prio = task.GetNamedNumber(L"priority", 0);
        if (prio > 0 && prio < 5)
        {
            FontIcon prio_icon;
            prio_icon.Glyph(L"\xE7C1");
            prio_icon.FontSize(10);
            prio_icon.Foreground(resources.Lookup(box_value(L"SystemFillColorCriticalBrush")).as<Brush>());
            subline.Children().Append(prio_icon);
        }

        hstring acc_name, acc_email;
        GetTaskAccountInfo(task, acc_name, acc_email);
        if (!acc_name.empty())
        {
            TextBlock acc_text;
            acc_text.Style(caption_style);
            acc_text.Foreground(secondary_brush);
            acc_text.Text(L"• " + acc_name);
            acc_text.TextTrimming(TextTrimming::CharacterEllipsis);
            acc_text.MaxWidth(110);
            subline.Children().Append(acc_text);
        }

        details.Children().Append(subline);
        row.Children().Append(details);

        return row;
    }

    /**
     * @brief Gestionnaire de clic du bouton bascule de la barre des tâches dans le ruban.
     */
    void MainWindow::OnRibbonTodoBarToggled(IInspectable const& sender, RoutedEventArgs const&)
    {
        bool is_checked = unbox_value_or<bool>(sender.as<AppBarToggleButton>().IsChecked(), false);
        SetTodoBarVisible(is_checked, true);
    }

    /**
     * @brief Gestionnaire de clic sur le bouton de fermeture de la barre des tâches.
     */
    void MainWindow::OnTodoBarCloseClick(IInspectable const&, RoutedEventArgs const&)
    {
        SetTodoBarVisible(false, true);
    }

    /**
     * @brief Gestionnaire de clic sur le bouton d'actualisation de la barre des tâches.
     */
    void MainWindow::OnTodoBarRefreshClick(IInspectable const&, RoutedEventArgs const&)
    {
        LoadTasks();
    }

    /**
     * @brief Gestionnaire du bouton d'ajout rapide d'une tâche.
     */
    void MainWindow::OnTodoBarQuickAddClick(IInspectable const&, RoutedEventArgs const&)
    {
        hstring text{ Trim(TodoBarQuickInput().Text()) };
        if (text.empty())
            return;

        JsonObject payload;
        payload.SetNamedValue(L"summary", JsonValue::CreateStringValue(text));
        payload.SetNamedValue(L"description", JsonValue::CreateStringValue(L""));

        /* Échéance par défaut : aujourd'hui à minuit */
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        payload.SetNamedValue(L"due", JsonValue::CreateNumberValue(static_cast<double>(LocalMidnight(now))));
        payload.SetNamedValue(L"due_date_only", JsonValue::CreateBooleanValue(true));
        payload.SetNamedValue(L"priority", JsonValue::CreateNumberValue(5.0));

        if (!m_task_list_uid.empty())
            payload.SetNamedValue(L"list_uid", JsonValue::CreateStringValue(m_task_list_uid));
        else if (m_task_lists && m_task_lists.Size() > 0)
            payload.SetNamedValue(L"list_uid", JsonValue::CreateStringValue(m_task_lists.GetObjectAt(0).GetNamedString(L"uid", L"")));

        TodoBarQuickInput().Text(L"");

        m_core->CreateTask(payload.Stringify(), [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Ajout de la tâche : " + error, InfoBarSeverity::Error);
                return;
            }
            LoadTasks();
        });
    }

    /**
     * @brief Gestionnaire de touche sur le champ d'ajout rapide (validation par Entrée).
     */
    void MainWindow::OnTodoBarQuickInputKeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() == Windows::System::VirtualKey::Enter)
        {
            args.Handled(true);
            OnTodoBarQuickAddClick(nullptr, nullptr);
        }
    }

    /**
     * @brief Clic sur une tâche de la barre latérale pour ouvrir l'éditeur complet.
     */
    void MainWindow::OnTodoBarItemClick(IInspectable const&, ItemClickEventArgs const& args)
    {
        auto row = args.ClickedItem().try_as<Grid>();
        JsonObject task;
        if (row && JsonObject::TryParse(unbox_value_or<hstring>(row.Tag(), L""), task))
        {
            ShowTaskEditorAsync(task);
        }
    }

    /**
     * @brief Gestionnaire du lien « Voir tout » pour basculer vers la vue Tâches.
     */
    void MainWindow::OnTodoBarViewAllClick(IInspectable const&, RoutedEventArgs const&)
    {
        for (auto const& item : Navigation().MenuItems())
        {
            if (auto nav_item = item.try_as<NavigationViewItem>())
            {
                if (unbox_value_or<hstring>(nav_item.Tag(), L"") == L"tasks")
                {
                    Navigation().SelectedItem(nav_item);
                    break;
                }
            }
        }
    }

    /**
     * @brief Début du glissement de redimensionnement de la barre latérale des tâches.
     */
    void MainWindow::OnTodoBarSplitterPointerPressed(IInspectable const& sender, Input::PointerRoutedEventArgs const& args)
    {
        auto splitter = sender.as<UIElement>();
        if (!args.GetCurrentPoint(splitter).Properties().IsLeftButtonPressed())
            return;
        if (!splitter.CapturePointer(args.Pointer()))
            return;

        m_todobar_dragging = true;
        m_todobar_drag_start_x = args.GetCurrentPoint(ViewsArea()).Position().X;
        m_todobar_drag_start_width = TodoBarColumn().ActualWidth();
    }

    /**
     * @brief Redimensionnement dynamique de la barre latérale des tâches.
     */
    void MainWindow::OnTodoBarSplitterPointerMoved(IInspectable const& sender, Input::PointerRoutedEventArgs const& args)
    {
        if (!m_todobar_dragging)
            return;

        double current_x = args.GetCurrentPoint(ViewsArea()).Position().X;
        double delta = m_todobar_drag_start_x - current_x;
        double new_width = std::clamp(m_todobar_drag_start_width + delta, 220.0, 550.0);

        m_todobar_width = new_width;
        TodoBarColumn().Width(GridLengthHelper::FromPixels(new_width));
        ::EvolutionWin::Settings::Instance().SetNumber(L"layout.todobar-width", new_width);
    }

    /**
     * @brief Fin du glissement de redimensionnement de la barre latérale.
     */
    void MainWindow::OnTodoBarSplitterPointerReleased(IInspectable const& sender, Input::PointerRoutedEventArgs const& args)
    {
        if (m_todobar_dragging)
        {
            m_todobar_dragging = false;
            sender.as<UIElement>().ReleasePointerCapture(args.Pointer());
        }
    }

    /**
     * @brief Perte de capture du pointeur lors du redimensionnement.
     */
    void MainWindow::OnTodoBarSplitterPointerCaptureLost(IInspectable const& sender, Input::PointerRoutedEventArgs const& args)
    {
        m_todobar_dragging = false;
    }

    /**
     * @brief Raccourci clavier Alt+F2 pour basculer l'affichage de la barre latérale des tâches.
     */
    void MainWindow::OnTodoBarAccelerator(Input::KeyboardAccelerator const&, Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        SetTodoBarVisible(!IsTodoBarVisible(), true);
    }
}
