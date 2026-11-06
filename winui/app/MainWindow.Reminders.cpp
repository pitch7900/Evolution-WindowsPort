#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "Settings.h"

#include <chrono>
#include <ctime>
#include <string>

/* Rappels d'événements : toutes les 30 secondes, les événements qui commencent dans les prochaines
 * 25 heures sont relus ; un événement dont le rappel (« x minutes avant ») est échu et qui n'a pas
 * encore commencé déclenche une notification Windows. Les rappels déjà émis sont mémorisés dans les
 * réglages (clé « calendar.reminded ») pour ne pas être répétés, même après un redémarrage. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Windows::Data::Json;

namespace winrt::EvolutionWin::implementation
{
    namespace
    {
        /** @brief Clé de réglage des rappels déjà émis (« début|agenda|uid »). */
        constexpr wchar_t const* kRemindedKey = L"calendar.reminded";

        /** @brief Fenêtre de lecture des événements : rappel maximal proposé (1 jour) + marge. */
        constexpr long long kLookAheadSeconds = 25 * 3600;

        /**
         * @brief Texte « Dans 15 minutes » / « Maintenant » / « Dans 2 h 05 » pour un délai.
         * @param seconds Secondes avant le début.
         * @return Texte affichable.
         */
        std::wstring DelayText(long long seconds)
        {
            long long minutes = (seconds + 59) / 60;
            if (minutes <= 0)
                return L"Maintenant";
            if (minutes == 1)
                return L"Dans 1 minute";
            if (minutes < 60)
                return L"Dans " + std::to_wstring(minutes) + L" minutes";
            wchar_t buffer[32];
            swprintf_s(buffer, L"Dans %lld h %02lld", minutes / 60, minutes % 60);
            return buffer;
        }

        /**
         * @brief Heure locale « 14:30 » d'un instant Unix.
         * @param unix_time Secondes Unix.
         * @return Heure au format HH:MM.
         */
        std::wstring ClockText(long long unix_time)
        {
            time_t t = static_cast<time_t>(unix_time);
            struct tm local{};
            localtime_s(&local, &t);
            wchar_t buffer[16];
            swprintf_s(buffer, L"%02d:%02d", local.tm_hour, local.tm_min);
            return buffer;
        }
    }

    /**
     * @brief Démarre la surveillance des rappels d'événements (une fois, au démarrage).
     */
    void MainWindow::StartEventReminders()
    {
        if (m_reminder_timer)
            return;

        m_reminder_timer = DispatcherTimer();
        m_reminder_timer.Interval(std::chrono::seconds(30));
        m_reminder_timer.Tick([this](auto&&, auto&&) { CheckEventReminders(); });
        m_reminder_timer.Start();
        CheckEventReminders();
    }

    /**
     * @brief Relit les prochains événements et notifie ceux dont le rappel est échu.
     */
    void MainWindow::CheckEventReminders()
    {
        auto& settings = ::EvolutionWin::Settings::Instance();
        if (!settings.GetBoolean(L"calendar.reminders-enabled", true) || !m_core || m_reminder_check_running)
            return;

        m_reminder_check_running = true;
        long long now = static_cast<long long>(std::time(nullptr));
        m_core->ListEvents(now, now + kLookAheadSeconds, [this, now](hstring const& json, hstring const& error)
        {
            m_reminder_check_running = false;
            JsonArray events;
            if (!error.empty() || !JsonArray::TryParse(json, events))
                return;

            auto& settings = ::EvolutionWin::Settings::Instance();
            std::set<std::wstring> done;
            JsonArray kept;
            for (auto const& value : settings.GetArray(kRemindedKey))
            {
                if (value.ValueType() != JsonValueType::String)
                    continue;
                std::wstring entry{ value.GetString() };
                /* les rappels d'événements déjà commencés sont oubliés */
                if (std::wcstoll(entry.c_str(), nullptr, 10) > now - 3600)
                {
                    done.insert(entry);
                    kept.Append(value);
                }
            }

            bool changed = false;
            for (auto const& value : events)
            {
                auto event = value.GetObject();
                long long start = static_cast<long long>(event.GetNamedNumber(L"start", 0));
                int reminder = static_cast<int>(event.GetNamedNumber(L"reminder_minutes", -1));
                if (reminder < 0 || start <= now || start - reminder * 60LL > now)
                    continue;

                std::wstring key = std::to_wstring(start) + L"|" + std::wstring{ event.GetNamedString(L"calendar_uid", L"") } +
                    L"|" + std::wstring{ event.GetNamedString(L"uid", L"") };
                if (!done.insert(key).second)
                    continue;
                kept.Append(JsonValue::CreateStringValue(key));
                changed = true;
                ShowEventReminder(event, start - now, start);
            }
            if (changed)
                settings.Set(kRemindedKey, kept);
        });
    }

    /**
     * @brief Notification Windows (toast) de rappel d'un événement.
     * @param event Événement (objet de evo_core_list_events).
     * @param seconds_left Secondes avant le début.
     * @param start Début de l'événement (secondes Unix).
     */
    void MainWindow::ShowEventReminder(JsonObject const& event, long long seconds_left, long long start)
    {
        auto& settings = ::EvolutionWin::Settings::Instance();
        hstring summary = event.GetNamedString(L"summary", L"(Sans titre)");
        hstring location = event.GetNamedString(L"location", L"");
        bool all_day = event.GetNamedBoolean(L"all_day", false);

        std::wstring title = L"Rappel : " + std::wstring{ summary };
        std::wstring body = all_day ? std::wstring{ L"Aujourd'hui, toute la journée" }
                                    : DelayText(seconds_left) + L" (" + ClockText(start) + L")";
        if (!location.empty())
            body += L" — " + std::wstring{ location };

        AddNotificationItem(hstring{ title }, hstring{ body });

        if (settings.GetBoolean(L"notifications.sound", true))
            ::EvolutionWin::Helpers::PlayNewMailSound(settings.GetString(L"notifications.sound-kind", L"windows"),
                settings.GetString(L"notifications.custom-sound-path", L""));

        try
        {
            using namespace Microsoft::Windows::AppNotifications;
            Builder::AppNotificationBuilder builder;
            builder.AddText(hstring{ title });
            builder.AddText(hstring{ body });
            builder.AddArgument(L"action", L"event");
            builder.AddArgument(L"calendar", event.GetNamedString(L"calendar_uid", L""));
            builder.AddArgument(L"uid", event.GetNamedString(L"uid", L""));

            /* durée d'affichage (réglage « Durée d'affichage des alertes de rappel ») : 0 = jusqu'à fermeture */
            int duration = _wtoi(settings.GetString(L"calendar.reminders-duration", L"10").c_str());
            if (duration == 0)
            {
                builder.SetScenario(Builder::AppNotificationScenario::Reminder);
                builder.AddButton(Builder::AppNotificationButton(L"Ouvrir").AddArgument(L"action", L"event"));
                builder.AddButton(Builder::AppNotificationButton(L"Fermer")
                    .AddArgument(L"action", L"dismiss"));
            }
            else if (duration >= 30)
                builder.SetDuration(Builder::AppNotificationDuration::Long);

            AppNotificationManager::Default().Show(builder.BuildNotification());
        }
        catch (...)
        {
        }
    }
}
