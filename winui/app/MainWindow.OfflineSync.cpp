#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include <ctime>
#include <vector>

/* Synchronisation hors connexion : boîte de dialogue des plages de synchronisation
 * d'un compte (téléchargement des dossiers, limite d'ancienneté) ou d'un dossier
 * (comme le compte / toujours / jamais), lues et enregistrées par le moteur
 * (evo_core_get_offline_sync / evo_core_set_offline_sync). */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    namespace
    {
        /** Unités de durée proposées, dans l'ordre de la liste déroulante. */
        constexpr wchar_t const* kUnits[] = { L"days", L"weeks", L"months", L"years" };

        /**
         * @brief Indice d'une unité de durée dans la liste déroulante.
         * @param unit Nom de l'unité renvoyé par le moteur.
         * @return Indice (jours par défaut).
         */
        int UnitIndex(hstring const& unit)
        {
            for (int i = 0; i < 4; ++i)
            {
                if (unit == kUnits[i])
                    return i;
            }
            return 0;
        }

        /**
         * @brief Description de la limite d'ancienneté d'un compte.
         * @param data Réglages du compte (réponse du moteur).
         * @return Texte localisé (« messages des 3 derniers mois »…), vide sans limite.
         */
        hstring LimitText(JsonObject const& data)
        {
            if (!data.GetNamedBoolean(L"limit_by_age", false))
                return {};
            hstring value = to_hstring(static_cast<int32_t>(data.GetNamedNumber(L"limit_value", 3)));
            switch (UnitIndex(data.GetNamedString(L"limit_unit", L"months")))
            {
            case 0: return Fmt(Tr(L"limitée aux messages des {0} derniers jours"), { value });
            case 1: return Fmt(Tr(L"limitée aux messages des {0} dernières semaines"), { value });
            case 2: return Fmt(Tr(L"limitée aux messages des {0} derniers mois"), { value });
            default: return Fmt(Tr(L"limitée aux messages des {0} dernières années"), { value });
            }
        }
    }

    /**
     * @brief Ouvre la boîte de synchronisation hors connexion d'un compte ou d'un dossier.
     * @param account Identifiant du compte.
     * @param folder Chemin complet du dossier, vide pour les réglages du compte.
     * @param name Nom affiché (compte ou dossier).
     */
    void MainWindow::OpenOfflineSyncDialog(hstring account, hstring folder, hstring name)
    {
        if (!m_core)
            return;

        m_core->GetOfflineSync(account, folder, [this, account, folder, name](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(Fmt(Tr(L"Synchronisation hors connexion : {0}"), { error }), InfoBarSeverity::Error);
                return;
            }
            JsonObject data;
            if (!JsonObject::TryParse(json, data))
                return;
            ShowOfflineSyncDialogAsync(account, folder, name, data);
        });
    }

    /**
     * @brief Affiche la boîte de dialogue et enregistre les réglages choisis.
     * @param account Identifiant du compte.
     * @param folder Chemin complet du dossier ("" : réglages du compte).
     * @param name Nom affiché.
     * @param data Réglages actuels renvoyés par le moteur.
     */
    fire_and_forget MainWindow::ShowOfflineSyncDialogAsync(hstring account, hstring folder, hstring name, JsonObject data)
    {
        auto strong = get_strong();

        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;
        m_dialog_open = true;

        bool folder_mode = !folder.empty();
        bool supported = data.GetNamedBoolean(L"supported", false)
            && (!folder_mode || data.GetNamedBoolean(L"folder_supported", false));

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(Fmt(Tr(L"Synchronisation hors connexion - {0}"), { name })));
        dialog.PrimaryButtonText(Tr(L"Enregistrer", L"Save", L"Common_Save"));
        dialog.CloseButtonText(Tr(L"Annuler", L"Cancel", L"Common_Cancel"));
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.IsPrimaryButtonEnabled(supported);

        auto secondary_brush = ::EvolutionWin::Helpers::ThemedBrush(L"TextFillColorSecondaryBrush");

        StackPanel panel;
        panel.Spacing(12);
        panel.MinWidth(460);

        TextBlock info;
        info.TextWrapping(TextWrapping::Wrap);
        info.Foreground(secondary_brush);
        panel.Children().Append(info);

        CheckBox stay_box;
        CheckBox limit_box;
        NumberBox limit_value;
        ComboBox limit_unit;
        RadioButtons folder_choice;

        if (!supported)
        {
            info.Text(Tr(L"Ce type de compte ou de dossier ne propose pas la synchronisation hors connexion."));
        }
        else if (!folder_mode)
        {
            info.Text(Tr(L"Les messages des dossiers synchronisés sont téléchargés lors de la relève et restent lisibles sans réseau. Chaque dossier peut avoir son propre réglage (clic droit sur le dossier › Synchronisation hors connexion)."));

            stay_box.Content(box_value(Tr(L"Garder les dossiers du compte synchronisés pour le mode hors connexion")));
            stay_box.IsChecked(data.GetNamedBoolean(L"stay_synchronized", false));
            panel.Children().Append(stay_box);

            limit_box.Content(box_value(Tr(L"Limiter aux messages récents")));
            limit_box.IsChecked(data.GetNamedBoolean(L"limit_by_age", false));
            panel.Children().Append(limit_box);

            StackPanel row;
            row.Orientation(Orientation::Horizontal);
            row.Spacing(8);
            limit_value.Minimum(1);
            limit_value.Maximum(9999);
            limit_value.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);
            limit_value.Value(data.GetNamedNumber(L"limit_value", 3));
            limit_value.Width(120);
            limit_value.IsEnabled(limit_box.IsChecked().Value());
            row.Children().Append(limit_value);

            for (auto unit_text : { Tr(L"jours"), Tr(L"semaines"), Tr(L"mois"), Tr(L"années") })
                limit_unit.Items().Append(box_value(unit_text));
            limit_unit.SelectedIndex(UnitIndex(data.GetNamedString(L"limit_unit", L"months")));
            limit_unit.IsEnabled(limit_box.IsChecked().Value());
            row.Children().Append(limit_unit);
            panel.Children().Append(row);

            limit_box.Click([limit_box, limit_value, limit_unit](IInspectable const&, RoutedEventArgs const&)
            {
                bool on = limit_box.IsChecked().Value();
                limit_value.IsEnabled(on);
                limit_unit.IsEnabled(on);
            });
        }
        else
        {
            hstring account_state = data.GetNamedBoolean(L"stay_synchronized", false)
                ? Tr(L"synchronisation des dossiers activée") : Tr(L"synchronisation des dossiers désactivée");
            hstring limit = LimitText(data);
            info.Text(Fmt(Tr(L"Réglage du compte : {0}{1}. Choisissez si ce dossier suit le compte ou non."),
                { account_state, limit.empty() ? hstring{} : hstring{ L", " } + limit }));

            folder_choice.Items().Append(box_value(Tr(L"Comme le compte")));
            folder_choice.Items().Append(box_value(Tr(L"Toujours télécharger pour le mode hors connexion")));
            folder_choice.Items().Append(box_value(Tr(L"Ne jamais télécharger")));
            hstring state = data.GetNamedString(L"folder_offline_sync", L"inherit");
            folder_choice.SelectedIndex(state == L"on" ? 1 : state == L"off" ? 2 : 0);
            panel.Children().Append(folder_choice);
        }

        /* réglages saisis, au format attendu par evo_core_set_offline_sync */
        auto collect = [account, folder, folder_mode, stay_box, limit_box, limit_value, limit_unit, folder_choice]() -> JsonObject
        {
            JsonObject settings;
            settings.SetNamedValue(L"account", JsonValue::CreateStringValue(account));
            if (!folder_mode)
            {
                settings.SetNamedValue(L"stay_synchronized", JsonValue::CreateBooleanValue(stay_box.IsChecked().Value()));
                settings.SetNamedValue(L"limit_by_age", JsonValue::CreateBooleanValue(limit_box.IsChecked().Value()));
                double value = limit_value.Value();
                settings.SetNamedValue(L"limit_value", JsonValue::CreateNumberValue(value >= 1 ? value : 1));
                int index = limit_unit.SelectedIndex();
                settings.SetNamedValue(L"limit_unit", JsonValue::CreateStringValue(kUnits[index >= 0 && index < 4 ? index : 0]));
            }
            else
            {
                int index = folder_choice.SelectedIndex();
                settings.SetNamedValue(L"folder", JsonValue::CreateStringValue(folder));
                settings.SetNamedValue(L"folder_offline_sync", JsonValue::CreateStringValue(index == 1 ? L"on" : index == 2 ? L"off" : L"inherit"));
            }
            return settings;
        };

        if (supported)
        {
            /* dernier résultat mémorisé : « heure|dossiers|messages|téléchargés|échecs » */
            hstring state_key = L"offline-sync." + account + L"." + folder;
            TextBlock sync_info;
            sync_info.TextWrapping(TextWrapping::Wrap);
            sync_info.Foreground(secondary_brush);

            auto describe = [state_key]() -> hstring
            {
                std::wstring value{ ::EvolutionWin::Settings::Instance().GetString(state_key, L"") };
                std::vector<std::wstring> parts;
                size_t pos = 0;
                while (true)
                {
                    size_t next = value.find(L'|', pos);
                    parts.push_back(value.substr(pos, next == std::wstring::npos ? next : next - pos));
                    if (next == std::wstring::npos)
                        break;
                    pos = next + 1;
                }
                if (value.empty() || parts.size() < 5)
                    return Tr(L"Aucune synchronisation hors connexion effectuée avec ce réglage.");
                int failed = _wtoi(parts[4].c_str());
                return Fmt(Tr(L"Dernière synchronisation : {0} - {1} message(s) téléchargé(s) sur {2}{3}"),
                    { FormatDate(_wtof(parts[0].c_str())), hstring{ parts[3] }, hstring{ parts[2] },
                      failed > 0 ? Fmt(Tr(L", {0} en échec"), { hstring{ parts[4] } }) : hstring{} });
            };
            sync_info.Text(describe());
            panel.Children().Append(sync_info);

            Button sync_button;
            sync_button.Content(box_value(Tr(L"Synchroniser maintenant")));
            panel.Children().Append(sync_button);

            sync_button.Click([this, strong, account, folder, collect, sync_info, sync_button, describe, state_key]
                (IInspectable const&, RoutedEventArgs const&)
            {
                sync_button.IsEnabled(false);
                sync_info.Text(Tr(L"Synchronisation en cours… (progression dans la barre d'état)"));

                /* les réglages affichés sont enregistrés avant de lancer le téléchargement */
                m_core->SetOfflineSync(collect().Stringify(), [this, strong, account, folder, sync_info, sync_button, describe, state_key]
                    (hstring const&, hstring const& save_error)
                {
                    if (!save_error.empty())
                    {
                        sync_button.IsEnabled(true);
                        sync_info.Text(Fmt(Tr(L"Synchronisation hors connexion : {0}"), { save_error }));
                        return;
                    }
                    m_core->OfflineSyncNow(account, folder, [this, strong, sync_info, sync_button, describe, state_key]
                        (hstring const& json, hstring const& sync_error)
                    {
                        sync_button.IsEnabled(true);
                        JsonObject result;
                        if (!sync_error.empty() || !JsonObject::TryParse(json, result))
                        {
                            sync_info.Text(Fmt(Tr(L"Synchronisation hors connexion : {0}"), { sync_error }));
                            return;
                        }
                        hstring record = to_hstring(static_cast<int64_t>(std::time(nullptr))) + L"|"
                            + to_hstring(static_cast<int32_t>(result.GetNamedNumber(L"folders", 0))) + L"|"
                            + to_hstring(static_cast<int32_t>(result.GetNamedNumber(L"messages", 0))) + L"|"
                            + to_hstring(static_cast<int32_t>(result.GetNamedNumber(L"downloaded", 0))) + L"|"
                            + to_hstring(static_cast<int32_t>(result.GetNamedNumber(L"failed", 0)));
                        ::EvolutionWin::Settings::Instance().SetString(state_key, record);
                        sync_info.Text(describe());
                        ShowStatus(Fmt(Tr(L"Synchronisation hors connexion terminée : {0} message(s) téléchargé(s)."),
                            { to_hstring(static_cast<int32_t>(result.GetNamedNumber(L"downloaded", 0))) }),
                            InfoBarSeverity::Success);
                    });
                });
            });
        }

        dialog.Content(panel);

        dialog.PrimaryButtonClick([this, strong, name, collect](ContentDialog const&, ContentDialogButtonClickEventArgs const&)
        {
            m_core->SetOfflineSync(collect().Stringify(), [this, strong, name](hstring const&, hstring const& error)
            {
                if (!error.empty())
                    ShowStatus(Fmt(Tr(L"Synchronisation hors connexion : {0}"), { error }), InfoBarSeverity::Error);
                else
                    ShowStatus(Fmt(Tr(L"Réglages de synchronisation enregistrés pour « {0} »."), { name }), InfoBarSeverity::Success);
            });
        });

        co_await dialog.ShowAsync();
        m_dialog_open = false;
    }
}
