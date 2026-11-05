#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "AutoReply.h"
#include <chrono>
#include <ctime>

/* Réponses automatiques (absence du bureau) : boîte de dialogue de configuration
 * d'un compte Microsoft 365 ou Gmail, lecture et enregistrement sur le serveur
 * (evo_core_get_auto_reply / evo_core_set_auto_reply). */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::EvolutionWin::implementation
{
    namespace
    {
        namespace AR = ::EvolutionWin::AutoReply;

        /**
         * @brief Indique si l'interface doit s'afficher en anglais (réglage de langue ou langue du système).
         * @return true pour l'anglais.
         */
        bool AutoReplyEnglish()
        {
            wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
            ::GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
            std::wstring setting{ ::EvolutionWin::Settings::Instance().GetString(L"general.language", L"system") };
            return AR::UseEnglish(setting, locale);
        }

        /**
         * @brief Convertit une chaîne UTF-8 du moteur en chaîne Windows.
         * @param utf8 Texte UTF-8.
         * @return Texte UTF-16.
         */
        hstring Wide(std::string const& utf8)
        {
            if (utf8.empty())
                return {};
            int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
            std::wstring w(static_cast<size_t>(n), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
            return hstring{ w };
        }

        /**
         * @brief Remplace le premier « %s » d'un modèle localisé.
         * @param model Modèle (ex. « Activées pour « %s » »).
         * @param value Valeur à insérer.
         * @return Texte final.
         */
        std::wstring Format1(std::wstring model, std::wstring const& value)
        {
            auto pos = model.find(L"%s");
            if (pos != std::wstring::npos)
                model.replace(pos, 2, value);
            return model;
        }

        /**
         * @brief Lit les réglages d'un objet JSON renvoyé par le moteur.
         * @param json Objet {"provider", "enabled", "external_audience", ...}.
         * @return Réglages décodés.
         */
        AR::Settings ParseSettings(JsonObject const& json)
        {
            AR::Settings s;
            s.provider = winrt::to_string(json.GetNamedString(L"provider", L""));
            s.single_message = json.GetNamedBoolean(L"single_message", false);
            s.enabled = json.GetNamedBoolean(L"enabled", false);
            s.external_audience = winrt::to_string(json.GetNamedString(L"external_audience", L"all"));
            s.start = static_cast<int64_t>(json.GetNamedNumber(L"start", 0));
            s.end = static_cast<int64_t>(json.GetNamedNumber(L"end", 0));
            s.subject = winrt::to_string(json.GetNamedString(L"subject", L""));
            s.internal_message = winrt::to_string(json.GetNamedString(L"internal_message", L""));
            s.external_message = winrt::to_string(json.GetNamedString(L"external_message", L""));
            return s;
        }

        /**
         * @brief Mémorise l'état des réponses automatiques (alimente le bandeau d'alerte).
         * @param account Identifiant du compte.
         * @param s Réglages courants du serveur.
         */
        void CacheAutoReplyState(hstring const& account, AR::Settings const& s)
        {
            auto& settings = ::EvolutionWin::Settings::Instance();
            settings.SetBoolean(L"account." + account + L".out-of-office-enabled", s.enabled);
            settings.SetString(L"account." + account + L".out-of-office-until",
                s.end > 0 ? hstring{ std::to_wstring(s.end) } : hstring{});
        }

        /**
         * @brief Remplit un sélecteur de date et d'heure à partir de secondes Unix locales.
         * @param epoch Secondes Unix (0 : maintenant + offset_hours).
         * @param offset_hours Décalage par défaut si epoch vaut 0.
         * @param date Sélecteur de date.
         * @param time Sélecteur d'heure.
         */
        void FillPickers(int64_t epoch, int offset_hours, CalendarDatePicker const& date, TimePicker const& time)
        {
            if (epoch <= 0)
                epoch = static_cast<int64_t>(std::time(nullptr)) + offset_hours * 3600;
            std::tm local{};
            if (!AR::EpochToLocal(epoch, local))
                return;
            date.Date(winrt::clock::from_time_t(static_cast<std::time_t>(epoch)));
            time.Time(std::chrono::hours(local.tm_hour) + std::chrono::minutes(local.tm_min));
        }

        /**
         * @brief Lit un sélecteur de date et d'heure en secondes Unix.
         * @param date Sélecteur de date.
         * @param time Sélecteur d'heure.
         * @return Secondes Unix (0 si aucune date choisie).
         */
        int64_t ReadPickers(CalendarDatePicker const& date, TimePicker const& time)
        {
            if (!date.Date())
                return 0;
            std::time_t raw = winrt::clock::to_time_t(date.Date().Value());
            std::tm local{};
            localtime_s(&local, &raw);
            auto minutes = std::chrono::duration_cast<std::chrono::minutes>(time.Time()).count();
            return AR::LocalToEpoch(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                static_cast<int>(minutes / 60), static_cast<int>(minutes % 60));
        }

        /**
         * @brief Nom d'affichage d'un compte.
         * @param accounts Liste JSON des comptes (peut être nulle).
         * @param uid Identifiant du compte.
         * @return Nom du compte, ou l'identifiant s'il est introuvable.
         */
        hstring AccountDisplayName(JsonArray const& accounts, hstring const& uid)
        {
            if (accounts)
            {
                for (auto const& val : accounts)
                {
                    auto obj = val.GetObject();
                    if (obj.GetNamedString(L"uid", L"") == uid)
                        return obj.GetNamedString(L"name", uid);
                }
            }
            return uid;
        }
    }

    /**
     * @brief Ouvre la boîte « Réponses automatiques » pour le compte actif (bouton du ruban Fichier).
     */
    void MainWindow::OnRibbonAutoReplyClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_account.empty() || m_account == L"local" || IsUnifiedView())
        {
            ShowStatus(AR::Tr(AR::Str::NoAccount, AutoReplyEnglish()), InfoBarSeverity::Warning);
            return;
        }
        ShowOutOfOfficeDialogAsync(m_account);
    }

    /**
     * @brief Désactive les réponses automatiques d'un compte sur le serveur (bouton du bandeau d'alerte).
     * @param account Identifiant du compte.
     */
    void MainWindow::DisableAutoReply(hstring const& account)
    {
        bool english = AutoReplyEnglish();
        hstring name = AccountDisplayName(m_accounts, account);

        m_core->GetAutoReply(account, [this, account, name, english](hstring const& json, hstring const& error)
        {
            if (!error.empty() || json.empty())
            {
                ShowStatus(hstring{ AR::Tr(AR::Str::SaveError, english) } + error, InfoBarSeverity::Error);
                return;
            }

            JsonObject obj;
            if (!JsonObject::TryParse(json, obj))
                return;
            AR::Settings s = ParseSettings(obj);
            s.enabled = false;

            m_core->SetAutoReply(account, Wide(AR::BuildSettingsJson(s)),
                [this, account, name, english, s](hstring const&, hstring const& save_error)
            {
                if (!save_error.empty())
                {
                    ShowStatus(hstring{ AR::Tr(AR::Str::SaveError, english) } + save_error, InfoBarSeverity::Error);
                    return;
                }
                CacheAutoReplyState(account, s);
                UpdateOutOfOfficeBanner();
                ShowStatus(hstring{ Format1(AR::Tr(AR::Str::SavedDisabled, english), std::wstring{ name }) }, InfoBarSeverity::Success);
            });
        });
    }

    /**
     * @brief Affiche la boîte de dialogue de configuration des réponses automatiques (absence du bureau) d'un compte.
     *
     * Les réglages sont lus sur le serveur (Microsoft 365 via Graph, Gmail via l'API
     * vacation), modifiables (activation, période, audience externe, messages) puis
     * enregistrés avec retour d'état dans la boîte, qui reste ouverte en cas d'erreur.
     * @param account Identifiant du compte visé.
     */
    Windows::Foundation::IAsyncAction MainWindow::ShowOutOfOfficeDialogAsync(winrt::hstring account)
    {
        auto strong = get_strong();

        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        bool english = AutoReplyEnglish();
        hstring acc_name = AccountDisplayName(m_accounts, account);

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(hstring{ AR::Tr(AR::Str::Title, english) } + L" - " + acc_name));
        dialog.PrimaryButtonText(AR::Tr(AR::Str::Save, english));
        dialog.CloseButtonText(AR::Tr(AR::Str::Cancel, english));
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.IsPrimaryButtonEnabled(false);
        /* fenêtre plus large que par défaut (une boîte de dialogue n'est pas redimensionnable) */
        dialog.Resources().Insert(box_value(L"ContentDialogMaxWidth"), box_value(1000.0));

        auto secondary_brush = Application::Current().Resources().Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Media::Brush>();
        auto caption_style = Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>();

        StackPanel panel;
        panel.Spacing(12);
        panel.MinWidth(720);

        /* état : lecture, enregistrement ou erreur */
        StackPanel status_row;
        status_row.Orientation(Orientation::Horizontal);
        status_row.Spacing(8);
        ProgressRing ring;
        ring.Width(18);
        ring.Height(18);
        ring.IsActive(true);
        TextBlock status_text;
        status_text.Text(AR::Tr(AR::Str::Loading, english));
        status_text.TextWrapping(TextWrapping::Wrap);
        status_text.VerticalAlignment(VerticalAlignment::Center);
        status_row.Children().Append(ring);
        status_row.Children().Append(status_text);
        panel.Children().Append(status_row);

        /* contenu du formulaire, activé une fois les réglages lus */
        StackPanel form_panel;
        ContentControl form;
        form_panel.Spacing(12);
        form.Content(form_panel);
        form.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        form.IsEnabled(false);

        ToggleSwitch enabled_switch;
        enabled_switch.Header(box_value(AR::Tr(AR::Str::Enable, english)));
        form_panel.Children().Append(enabled_switch);

        CheckBox period_box;
        period_box.Content(box_value(AR::Tr(AR::Str::LimitPeriod, english)));
        form_panel.Children().Append(period_box);

        auto make_row = [](CalendarDatePicker const& date, TimePicker const& time)
        {
            Grid row;
            row.ColumnSpacing(8);
            row.ColumnDefinitions().Append(ColumnDefinition{});
            row.ColumnDefinitions().Append(ColumnDefinition{});
            Grid::SetColumn(time, 1);
            row.Children().Append(date);
            row.Children().Append(time);
            return row;
        };
        CalendarDatePicker start_date;
        start_date.Header(box_value(AR::Tr(AR::Str::Start, english)));
        TimePicker start_time;
        start_time.Header(box_value(L" "));
        CalendarDatePicker end_date;
        end_date.Header(box_value(AR::Tr(AR::Str::End, english)));
        TimePicker end_time;
        end_time.Header(box_value(L" "));
        Grid start_row = make_row(start_date, start_time);
        Grid end_row = make_row(end_date, end_time);
        start_row.Visibility(Visibility::Collapsed);
        end_row.Visibility(Visibility::Collapsed);
        form_panel.Children().Append(start_row);
        form_panel.Children().Append(end_row);
        period_box.Checked([start_row, end_row](IInspectable const&, RoutedEventArgs const&)
        {
            start_row.Visibility(Visibility::Visible);
            end_row.Visibility(Visibility::Visible);
        });
        period_box.Unchecked([start_row, end_row](IInspectable const&, RoutedEventArgs const&)
        {
            start_row.Visibility(Visibility::Collapsed);
            end_row.Visibility(Visibility::Collapsed);
        });

        ComboBox audience_combo;
        audience_combo.Header(box_value(AR::Tr(AR::Str::Audience, english)));
        audience_combo.HorizontalAlignment(HorizontalAlignment::Stretch);
        audience_combo.Items().Append(box_value(AR::Tr(AR::Str::AudienceNone, english)));
        audience_combo.Items().Append(box_value(AR::Tr(AR::Str::AudienceKnown, english)));
        audience_combo.Items().Append(box_value(AR::Tr(AR::Str::AudienceAll, english)));
        audience_combo.SelectedIndex(2);

        TextBox subject_box;
        subject_box.Header(box_value(AR::Tr(AR::Str::Subject, english)));
        subject_box.Visibility(Visibility::Collapsed);
        form_panel.Children().Append(subject_box);

        /* zones de message : deux lignes de même hauteur dans une grille, couleurs et alignement
         * explicites (sinon texte illisible : noir sur fond noir, aligné à droite) */
        auto make_message_box = [](hstring const& header)
        {
            auto resources = Application::Current().Resources();
            TextBox box;
            box.Header(box_value(header));
            box.AcceptsReturn(true);
            box.TextWrapping(TextWrapping::Wrap);
            box.TextAlignment(TextAlignment::Left);
            box.FlowDirection(FlowDirection::LeftToRight);
            box.HorizontalAlignment(HorizontalAlignment::Stretch);
            box.VerticalAlignment(VerticalAlignment::Stretch);
            box.MinHeight(110);
            box.Foreground(resources.Lookup(box_value(L"TextFillColorPrimaryBrush")).as<Media::Brush>());
            box.Background(resources.Lookup(box_value(L"ControlFillColorDefaultBrush")).as<Media::Brush>());
            return box;
        };
        TextBox internal_box = make_message_box(AR::Tr(AR::Str::InternalMessage, english));
        TextBox external_box = make_message_box(AR::Tr(AR::Str::ExternalMessage, english));

        /* deux onglets comme Outlook : au sein de l'organisation / en dehors de l'organisation */
        internal_box.Height(260);
        external_box.Height(210);

        SelectorBar message_tabs;
        SelectorBarItem tab_internal;
        tab_internal.Text(::EvolutionWin::Helpers::Tr(L"Au sein de mon organisation", L"Inside my organization"));
        SelectorBarItem tab_external;
        tab_external.Text(::EvolutionWin::Helpers::Tr(L"En dehors de mon organisation", L"Outside my organization"));
        message_tabs.Items().Append(tab_internal);
        message_tabs.Items().Append(tab_external);
        message_tabs.SelectedItem(tab_internal);
        form_panel.Children().Append(message_tabs);

        StackPanel internal_panel;
        internal_panel.Spacing(8);
        internal_panel.Children().Append(internal_box);

        StackPanel external_panel;
        external_panel.Spacing(8);
        external_panel.Visibility(Visibility::Collapsed);
        external_panel.Children().Append(audience_combo);
        external_panel.Children().Append(external_box);

        form_panel.Children().Append(internal_panel);
        form_panel.Children().Append(external_panel);

        message_tabs.SelectionChanged([internal_panel, external_panel, tab_external](SelectorBar const& sender, SelectorBarSelectionChangedEventArgs const&)
        {
            bool outside = sender.SelectedItem() == tab_external;
            internal_panel.Visibility(outside ? Visibility::Collapsed : Visibility::Visible);
            external_panel.Visibility(outside ? Visibility::Visible : Visibility::Collapsed);
        });

        TextBlock note;
        note.Style(caption_style);
        note.Foreground(secondary_brush);
        note.TextWrapping(TextWrapping::Wrap);
        form_panel.Children().Append(note);

        panel.Children().Append(form);
        ScrollViewer scroll;
        scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        scroll.MaxHeight(620);
        scroll.Content(panel);
        dialog.Content(scroll);

        /* réglages lus : fournisseur retenu pour la validation et l'affichage */
        auto provider = std::make_shared<std::string>();
        auto single = std::make_shared<bool>(false);
        auto busy = std::make_shared<bool>(false);

        auto show_error = [status_text, ring](hstring const& text)
        {
            ring.IsActive(false);
            ring.Visibility(Visibility::Collapsed);
            status_text.Text(text);
            status_text.Foreground(Application::Current().Resources().Lookup(box_value(L"SystemFillColorCriticalBrush")).as<Media::Brush>());
        };

        m_core->GetAutoReply(account, [=](hstring const& json, hstring const& error)
        {
            JsonObject obj;
            if (!error.empty() || json.empty() || !JsonObject::TryParse(json, obj))
            {
                show_error(error.empty() ? hstring{ AR::Tr(AR::Str::LoadError, english) } : error);
                return;
            }

            AR::Settings s = ParseSettings(obj);
            *provider = s.provider;
            *single = s.single_message;

            enabled_switch.IsOn(s.enabled);
            audience_combo.SelectedIndex(AR::AudienceToIndex(s.external_audience));
            bool has_period = s.start > 0 || s.end > 0;
            FillPickers(s.start, 0, start_date, start_time);
            FillPickers(s.end, 24 * 7, end_date, end_time);
            period_box.IsChecked(has_period);
            subject_box.Text(Wide(s.subject));
            internal_box.Text(Wide(s.internal_message));
            external_box.Text(Wide(s.external_message));

            if (s.single_message)
            {
                /* un seul message : zone unique « Message de réponse » + objet */
                /* Gmail : un seul message : pas d'onglets, seule la zone « message de réponse » */
                message_tabs.Visibility(Visibility::Collapsed);
                internal_panel.Visibility(Visibility::Collapsed);
                external_panel.Visibility(Visibility::Visible);
                external_box.Header(box_value(AR::Tr(AR::Str::SingleMessage, english)));
                subject_box.Visibility(Visibility::Visible);
                note.Text(AR::Tr(AR::Str::GmailNote, english));
            }
            else
            {
                note.Text(AR::Tr(AR::Str::M365Note, english));
            }

            CacheAutoReplyState(account, s);
            UpdateOutOfOfficeBanner();

            ring.IsActive(false);
            ring.Visibility(Visibility::Collapsed);
            status_row.Visibility(Visibility::Collapsed);
            form.IsEnabled(true);
            dialog.IsPrimaryButtonEnabled(true);
        });

        /* enregistrement : la boîte reste ouverte tant que le serveur n'a pas accepté */
        dialog.PrimaryButtonClick([=](ContentDialog const& sender, ContentDialogButtonClickEventArgs const& args)
        {
            args.Cancel(true);
            if (*busy)
                return;

            AR::Settings s;
            s.provider = *provider;
            s.single_message = *single;
            s.enabled = enabled_switch.IsOn();
            s.external_audience = AR::AudienceFromIndex(audience_combo.SelectedIndex());
            if (period_box.IsChecked() && period_box.IsChecked().GetBoolean())
            {
                s.start = ReadPickers(start_date, start_time);
                s.end = ReadPickers(end_date, end_time);
            }
            s.subject = winrt::to_string(subject_box.Text());
            s.internal_message = winrt::to_string(internal_box.Text());
            s.external_message = winrt::to_string(external_box.Text());
            if (s.single_message)
                s.internal_message = s.external_message;

            AR::FormError invalid = AR::Validate(s);
            if (invalid != AR::FormError::None)
            {
                ring.Visibility(Visibility::Collapsed);
                status_row.Visibility(Visibility::Visible);
                show_error(AR::Tr(invalid == AR::FormError::EndBeforeStart ? AR::Str::ErrEndBeforeStart
                    : invalid == AR::FormError::IncompleteRange ? AR::Str::ErrIncompleteRange
                    : AR::Str::ErrEmptyMessage, english));
                return;
            }

            *busy = true;
            form.IsEnabled(false);
            sender.IsPrimaryButtonEnabled(false);
            status_text.Foreground(secondary_brush);
            status_text.Text(AR::Tr(AR::Str::Saving, english));
            ring.Visibility(Visibility::Visible);
            ring.IsActive(true);
            status_row.Visibility(Visibility::Visible);

            m_core->SetAutoReply(account, Wide(AR::BuildSettingsJson(s)),
                [=](hstring const&, hstring const& error)
            {
                *busy = false;
                if (!error.empty())
                {
                    form.IsEnabled(true);
                    dialog.IsPrimaryButtonEnabled(true);
                    show_error(hstring{ AR::Tr(AR::Str::SaveError, english) } + error);
                    return;
                }

                CacheAutoReplyState(account, s);
                UpdateOutOfOfficeBanner();
                dialog.Hide();
                ShowStatus(hstring{ Format1(AR::Tr(s.enabled ? AR::Str::Saved : AR::Str::SavedDisabled, english), std::wstring{ acc_name }) },
                    InfoBarSeverity::Success);
            });
        });

        co_await dialog.ShowAsync();
        m_dialog_open = false;
    }
}
