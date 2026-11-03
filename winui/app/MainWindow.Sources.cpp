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

/* Volet de sources du calendrier et des contacts (calendriers, carnets
 * d'adresses). */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    /* Volet de sources (calendriers, carnets d'adresses) : groupes par compte ;
     * case à cocher = affiché, clic sur le nom = source active (mise en
     * évidence) ; pastille de couleur si la source en a une.
     * Paramètres :
     *   panel       : StackPanel à remplir
     *   sources     : evo_core_list_sources ()
     *   visible     : uids affichés
     *   active      : uid de la source active
     *   on_toggle   : case cochée / décochée (uid, affiché)
     *   on_activate : clic sur le nom (uid)
     * Retourne : nom de la source active. */
    static hstring BuildSourcesPanel(StackPanel const& panel, JsonArray const& sources,
        std::vector<hstring> const& visible, hstring const& active,
        std::function<void(hstring const&, bool)> on_toggle, std::function<void(hstring const&)> on_activate)
    {
        auto children = panel.Children();
        children.Clear();
        hstring active_name;

        std::vector<hstring> accounts;
        for (auto const& value : sources)
        {
            hstring account = value.GetObject().GetNamedString(L"account", L"");
            if (std::find(accounts.begin(), accounts.end(), account) == accounts.end())
                accounts.push_back(account);
        }

        for (auto const& account : accounts)
        {
            TextBlock header;
            header.Text(account.empty() ? hstring{ L"Autres" } : account);
            header.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            header.Foreground(Application::Current().Resources().Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Brush>());
            header.Margin(ThicknessHelper::FromLengths(4, 8, 0, 2));
            children.Append(header);

            for (auto const& value : sources)
            {
                auto source = value.GetObject();
                if (source.GetNamedString(L"account", L"") != account)
                    continue;

                hstring uid = source.GetNamedString(L"uid", L"");
                hstring name = source.GetNamedString(L"name", L"");
                bool is_active = uid == active;
                if (is_active)
                    active_name = name;

                Grid row;
                row.ColumnSpacing(6);
                row.Padding(ThicknessHelper::FromLengths(4, 0, 4, 0));
                row.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
                ColumnDefinition check_col;
                check_col.Width(GridLengthHelper::Auto());
                row.ColumnDefinitions().Append(check_col);
                row.ColumnDefinitions().Append(ColumnDefinition{});
                if (is_active)
                    row.Background(Application::Current().Resources().Lookup(box_value(L"SubtleFillColorSecondaryBrush")).as<Brush>());

                CheckBox shown;
                shown.MinWidth(0);
                shown.IsChecked(std::find(visible.begin(), visible.end(), uid) != visible.end());
                ToolTipService::SetToolTip(shown, box_value(L"Afficher"));
                shown.Click([uid, on_toggle](IInspectable const& sender, RoutedEventArgs const&)
                {
                    auto box = sender.as<CheckBox>();
                    on_toggle(uid, box.IsChecked() && box.IsChecked().GetBoolean());
                });
                row.Children().Append(shown);

                StackPanel label;
                label.Orientation(Orientation::Horizontal);
                label.Spacing(8);
                label.VerticalAlignment(VerticalAlignment::Center);
                label.Background(SolidColorBrush{ Microsoft::UI::Colors::Transparent() });

                std::wstring color{ source.GetNamedString(L"color", L"") };
                if (color.size() == 7 && color[0] == L'#')
                {
                    Border dot;
                    dot.Width(12);
                    dot.Height(12);
                    dot.CornerRadius(CornerRadiusHelper::FromUniformRadius(3));
                    auto channel = [&](size_t at) { return static_cast<uint8_t>(std::wcstoul(color.substr(at, 2).c_str(), nullptr, 16)); };
                    dot.Background(SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, channel(1), channel(3), channel(5)) });
                    label.Children().Append(dot);
                }

                TextBlock text;
                text.Text(name);
                text.TextTrimming(TextTrimming::CharacterEllipsis);
                if (is_active)
                    text.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
                label.Children().Append(text);
                ToolTipService::SetToolTip(label, box_value(is_active
                    ? hstring{ L"Actif : les nouveaux éléments y sont créés" }
                    : hstring{ L"Choisir pour les nouveaux éléments" }));
                label.Tapped([uid, on_activate](IInspectable const&, Input::TappedRoutedEventArgs const&)
                {
                    on_activate(uid);
                });
                Grid::SetColumn(label, 1);
                row.Children().Append(label);

                children.Append(row);
            }
        }

        return active_name;
    }

    /* uids affichés de la préférence key, parmi les sources connues */
    static std::vector<hstring> VisibleUids(JsonArray const& sources, wchar_t const* key)
    {
        std::vector<hstring> uids;
        auto visible = ::EvolutionWin::Settings::Instance().GetArray(key);
        for (auto const& value : sources ? sources : JsonArray{})
        {
            hstring uid = value.GetObject().GetNamedString(L"uid", L"");
            for (auto const& entry : visible)
                if (entry.ValueType() == JsonValueType::String && entry.GetString() == uid)
                    uids.push_back(uid);
        }
        return uids;
    }

    /* source active de la préférence key, sinon celle par défaut */
    static hstring ActiveUid(JsonArray const& sources, wchar_t const* key)
    {
        hstring active = ::EvolutionWin::Settings::Instance().GetString(key, L"");
        hstring fallback;
        for (auto const& value : sources ? sources : JsonArray{})
        {
            auto source = value.GetObject();
            if (source.GetNamedString(L"uid", L"") == active)
                return active;
            if (source.GetNamedBoolean(L"default", false))
                fallback = source.GetNamedString(L"uid", L"");
        }
        return fallback;
    }

    /* coche / décoche uid dans la préférence key */
    static void SetUidVisible(JsonArray const& sources, wchar_t const* key, hstring const& uid, bool shown)
    {
        JsonArray updated;
        for (auto const& current : VisibleUids(sources, key))
            if (current != uid)
                updated.Append(JsonValue::CreateStringValue(current));
        if (shown)
            updated.Append(JsonValue::CreateStringValue(uid));
        ::EvolutionWin::Settings::Instance().Set(key, updated);
    }

    /* Liste des calendriers (comptes, couleurs, calendrier par défaut), puis
     * événements des calendriers affichés. */
    void MainWindow::LoadCalendarSources()
    {
        m_core->ListSources(L"calendar", [this](hstring const& json, hstring const& error)
        {
            JsonArray sources;
            if (!error.empty() || !JsonArray::TryParse(json, sources))
            {
                ShowStatus(L"Calendriers : " + error, InfoBarSeverity::Error);
                return;
            }

            m_calendar_sources = sources;

            /* premier usage : calendrier par défaut et calendriers principaux
               des comptes, pas les calendriers partagés d'autres personnes */
            auto& settings = ::EvolutionWin::Settings::Instance();
            if (!settings.GetBoolean(L"calendar.visible-init", false))
            {
                JsonArray visible;
                for (auto const& value : sources)
                {
                    auto source = value.GetObject();
                    hstring name = source.GetNamedString(L"name", L"");
                    if (source.GetNamedBoolean(L"default", false) || name == L"Calendrier" || name == L"Calendar" ||
                        name == L"Personnel" || name == L"Personal")
                        visible.Append(JsonValue::CreateStringValue(source.GetNamedString(L"uid", L"")));
                }
                settings.Set(L"calendar.visible", visible);
                settings.SetBoolean(L"calendar.visible-init", true);
            }

            RebuildCalendarSourcesPanel();
            LoadEvents();
        });
    }

    std::vector<hstring> MainWindow::VisibleCalendarUids()
    {
        return VisibleUids(m_calendar_sources, L"calendar.visible");
    }

    /* Bouton « + » du volet des calendriers : ouvre la boîte d'ajout d'un calendrier. */
    void MainWindow::OnAddCalendarClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowAddCalendarDialogAsync();
    }

    hstring MainWindow::ActiveCalendarUid()
    {
        return ActiveUid(m_calendar_sources, L"calendar.active");
    }

    void MainWindow::RebuildCalendarSourcesPanel()
    {
        hstring active_name = BuildSourcesPanel(CalendarSourcesPanel(), m_calendar_sources,
            VisibleCalendarUids(), ActiveCalendarUid(),
            [this](hstring const& uid, bool shown)
            {
                SetUidVisible(m_calendar_sources, L"calendar.visible", uid, shown);
                LoadEvents();
            },
            [this](hstring const& uid)
            {
                ::EvolutionWin::Settings::Instance().SetString(L"calendar.active", uid);
                RebuildCalendarSourcesPanel();
            });

        ActiveCalendarText().Text(active_name.empty() ? hstring{}
            : L"Nouveaux événements dans : " + active_name);
    }

    /* Carnets d'adresses (comptes, carnet par défaut), puis contacts des
     * carnets affichés ; premier usage : carnet par défaut et carnets
     * principaux des comptes, pas les annuaires d'entreprise. */
    void MainWindow::LoadContactBooks()
    {
        m_core->ListSources(L"contacts", [this](hstring const& json, hstring const& error)
        {
            JsonArray books;
            if (!error.empty() || !JsonArray::TryParse(json, books))
            {
                ShowStatus(L"Carnets d'adresses : " + error, InfoBarSeverity::Error);
                return;
            }

            m_contact_books = books;

            auto& settings = ::EvolutionWin::Settings::Instance();
            if (!settings.GetBoolean(L"contacts.visible-init", false))
            {
                JsonArray visible;
                for (auto const& value : books)
                {
                    auto book = value.GetObject();
                    hstring name = book.GetNamedString(L"name", L"");
                    if (book.GetNamedBoolean(L"default", false) || name == L"Contacts" || name == L"Mes contacts" ||
                        name == L"My Contacts" || name == L"Personnel" || name == L"Personal")
                        visible.Append(JsonValue::CreateStringValue(book.GetNamedString(L"uid", L"")));
                }
                settings.Set(L"contacts.visible", visible);
                settings.SetBoolean(L"contacts.visible-init", true);
            }

            RebuildContactBooksPanel();
            LoadContacts(ContactSearchBox().Text());
        });
    }

    std::vector<hstring> MainWindow::VisibleBookUids()
    {
        return VisibleUids(m_contact_books, L"contacts.visible");
    }

    hstring MainWindow::ActiveBookUid()
    {
        return ActiveUid(m_contact_books, L"contacts.active");
    }

    void MainWindow::RebuildContactBooksPanel()
    {
        hstring active_name = BuildSourcesPanel(ContactBooksPanel(), m_contact_books,
            VisibleBookUids(), ActiveBookUid(),
            [this](hstring const& uid, bool shown)
            {
                SetUidVisible(m_contact_books, L"contacts.visible", uid, shown);
                LoadContacts(ContactSearchBox().Text());
            },
            [this](hstring const& uid)
            {
                ::EvolutionWin::Settings::Instance().SetString(L"contacts.active", uid);
                RebuildContactBooksPanel();
            });

        ActiveBookText().Text(active_name.empty() ? hstring{}
            : L"Nouveaux contacts dans : " + active_name);
    }

    /* Ajoute un calendrier : calendrier local vide, ou abonnement à une URL
     * iCalendar (.ics / webcal://) actualisé périodiquement. Le nouveau
     * calendrier est affiché immédiatement dans la vue Calendrier. */
    fire_and_forget MainWindow::ShowAddCalendarDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        auto trimmed = [](hstring const& text)
        {
            std::wstring value{ text };
            auto first = value.find_first_not_of(L" \t\r\n");
            auto last = value.find_last_not_of(L" \t\r\n");
            return first == std::wstring::npos ? std::wstring{} : value.substr(first, last - first + 1);
        };

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Ajouter un calendrier"));

        StackPanel panel;
        panel.Spacing(12);
        TextBox name_box;
        name_box.Header(box_value(L"Nom du calendrier"));
        name_box.MinWidth(360);
        panel.Children().Append(name_box);
        TextBox url_box;
        url_box.Header(box_value(L"Adresse Internet (ICS / webcal) — laisser vide pour un calendrier local vide"));
        url_box.PlaceholderText(L"https://exemple.fr/agenda.ics");
        panel.Children().Append(url_box);
        dialog.Content(panel);

        dialog.PrimaryButtonText(L"Ajouter");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.IsPrimaryButtonEnabled(false);
        name_box.TextChanged([dialog, trimmed](IInspectable const& sender, TextChangedEventArgs const&)
        {
            dialog.IsPrimaryButtonEnabled(!trimmed(sender.as<TextBox>().Text()).empty());
        });

        m_dialog_open = true;
        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;
        if (result != ContentDialogResult::Primary)
            co_return;

        JsonObject request;
        request.SetNamedValue(L"name", JsonValue::CreateStringValue(trimmed(name_box.Text())));
        request.SetNamedValue(L"url", JsonValue::CreateStringValue(trimmed(url_box.Text())));

        ShowStatus(L"Création du calendrier…", InfoBarSeverity::Informational);
        m_core->CreateCalendar(request.Stringify(), [this](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Ajout du calendrier : " + error, InfoBarSeverity::Error);
                return;
            }

            /* affiche le nouveau calendrier puis recharge la liste */
            JsonObject created;
            if (JsonObject::TryParse(json, created))
            {
                auto& settings = ::EvolutionWin::Settings::Instance();
                JsonArray visible = settings.GetArray(L"calendar.visible");
                visible.Append(JsonValue::CreateStringValue(created.GetNamedString(L"uid", L"")));
                settings.Set(L"calendar.visible", visible);
            }
            ShowStatus(L"Calendrier ajouté.", InfoBarSeverity::Success);
            LoadCalendarSources();
        });
    }
}
