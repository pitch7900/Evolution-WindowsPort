#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include <ctime>
#include <functional>

/* Mode hors connexion : liste des actions en attente de rejeu (suppressions, déplacements,
 * copies, indicateurs, envois de la Boîte d'envoi) avec annulation, et boîte de dialogue de
 * résolution des conflits de modification des événements et des contacts
 * (Garder la version locale / distante / les deux). Le moteur journalise et rejoue les actions
 * (evo_core_offline_queue_*) ; ce fichier n'en présente que l'état. */

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
        /**
         * @brief Phrase décrivant une action en attente.
         * @param action Action renvoyée par le moteur ("kind", "subject", "dest_folder", "flag", "value").
         * @return Texte localisé (« Supprimer « Objet » »...).
         */
        hstring DescribeAction(JsonObject const& action)
        {
            hstring kind = action.GetNamedString(L"kind", L"");
            hstring subject = action.GetNamedString(L"subject", L"");
            if (subject.empty())
                subject = Tr(L"(Sans objet)");
            hstring dest = action.GetNamedString(L"dest_folder", L"");
            hstring flag = action.GetNamedString(L"flag", L"");
            bool value = action.GetNamedNumber(L"value", 0) != 0;

            if (kind == L"delete")
                return Fmt(Tr(L"Supprimer « {0} »"), { subject });
            if (kind == L"move")
                return Fmt(Tr(L"Déplacer « {0} » vers {1}"), { subject, dest });
            if (kind == L"copy")
                return Fmt(Tr(L"Copier « {0} » vers {1}"), { subject, dest });
            if (kind == L"send")
                return Fmt(Tr(L"Envoyer « {0} »"), { subject });
            if (flag == L"seen")
                return Fmt(value ? Tr(L"Marquer « {0} » comme lu") : Tr(L"Marquer « {0} » comme non lu"), { subject });
            if (flag == L"flagged")
                return Fmt(value ? Tr(L"Ajouter le drapeau sur « {0} »") : Tr(L"Retirer le drapeau de « {0} »"), { subject });
            return Fmt(value ? Tr(L"Ajouter l'étiquette « {1} » sur « {0} »") : Tr(L"Retirer l'étiquette « {1} » de « {0} »"),
                { subject, flag });
        }

        /**
         * @brief Texte d'une valeur de champ en conflit.
         * @param name Nom du champ ("start" et "end" : secondes Unix).
         * @param value Valeur brute.
         * @return Date et heure locales pour start / end, sinon la valeur ; « (vide) » si vide.
         */
        hstring FieldValueText(hstring const& name, hstring const& value)
        {
            if (value.empty())
                return Tr(L"(vide)");
            if (name == L"start" || name == L"end")
            {
                time_t raw = static_cast<time_t>(_wtoi64(value.c_str()));
                struct tm local{};
                if (raw > 0 && localtime_s(&local, &raw) == 0)
                {
                    wchar_t text[48]{};
                    swprintf_s(text, L"%02d/%02d/%04d %02d:%02d", local.tm_mday, local.tm_mon + 1,
                               local.tm_year + 1900, local.tm_hour, local.tm_min);
                    return hstring{ text };
                }
            }
            return value;
        }

        /**
         * @brief Libellé d'un champ en conflit.
         * @param name Nom technique du champ.
         * @return Libellé localisé.
         */
        hstring FieldLabel(hstring const& name)
        {
            if (name == L"summary") return Tr(L"Titre");
            if (name == L"location") return Tr(L"Lieu");
            if (name == L"description") return Tr(L"Description");
            if (name == L"start") return Tr(L"Début");
            if (name == L"end") return Tr(L"Fin");
            if (name == L"full_name") return Tr(L"Nom complet");
            if (name == L"email") return Tr(L"Adresse électronique");
            if (name == L"phone") return Tr(L"Téléphone");
            if (name == L"org") return Tr(L"Organisation");
            if (name == L"notes") return Tr(L"Notes");
            return name;
        }
    }

    /**
     * @brief Met à jour le bouton des actions en attente de la barre d'état.
     * @param pending Nombre d'actions en attente de rejeu.
     * @param failed Nombre d'actions abandonnées (à relancer ou à annuler).
     */
    void MainWindow::UpdatePendingActionsBadge(int pending, int failed)
    {
        int total = pending + failed;
        PendingActionsButton().Visibility(total > 0 ? Visibility::Visible : Visibility::Collapsed);
        if (total > 0)
            PendingActionsLabel().Text(Fmt(total == 1 ? Tr(L"{0} action en attente") : Tr(L"{0} actions en attente"),
                { to_hstring(total) }));
    }

    /** @brief Ouverture du panneau : relit la liste des actions en attente. */
    void MainWindow::OnPendingActionsButtonClick(IInspectable const&, RoutedEventArgs const&)
    {
        RefreshPendingActions();
    }

    /** @brief Relit la file d'attente auprès du moteur et reconstruit la liste du panneau. */
    void MainWindow::RefreshPendingActions()
    {
        if (!m_core)
            return;

        m_core->ListOfflineQueue([this](hstring const& json, hstring const& error)
        {
            JsonObject data;
            if (!error.empty() || !JsonObject::TryParse(json, data))
                return;

            auto actions = data.GetNamedArray(L"actions", JsonArray{});
            UpdatePendingActionsBadge(static_cast<int>(actions.Size()), 0);

            auto panel = PendingActionsPanel();
            panel.Children().Clear();
            auto secondary_brush = ::EvolutionWin::Helpers::ThemedBrush(L"TextFillColorSecondaryBrush");

            for (auto const& value : actions)
            {
                auto action = value.GetObject();
                hstring id = action.GetNamedString(L"id", L"");

                Grid row;
                row.ColumnSpacing(8);
                ColumnDefinition text_column;
                text_column.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                ColumnDefinition button_column;
                button_column.Width(GridLengthHelper::Auto());
                row.ColumnDefinitions().Append(text_column);
                row.ColumnDefinitions().Append(button_column);

                StackPanel texts;
                TextBlock title;
                title.Text(DescribeAction(action));
                title.TextWrapping(TextWrapping::Wrap);
                texts.Children().Append(title);

                hstring failure = action.GetNamedString(L"error", L"");
                if (action.GetNamedNumber(L"status", 0) != 0 || !failure.empty())
                {
                    TextBlock detail;
                    detail.Text(action.GetNamedNumber(L"status", 0) != 0
                        ? Fmt(Tr(L"Abandonnée après plusieurs échecs : {0}"), { failure })
                        : Fmt(Tr(L"Dernière erreur : {0}"), { failure }));
                    detail.TextWrapping(TextWrapping::Wrap);
                    detail.Foreground(secondary_brush);
                    detail.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                    texts.Children().Append(detail);
                }
                Grid::SetColumn(texts, 0);
                row.Children().Append(texts);

                Button cancel;
                cancel.Content(box_value(Tr(L"Annuler")));
                Grid::SetColumn(cancel, 1);
                cancel.Click([this, id](IInspectable const&, RoutedEventArgs const&)
                {
                    m_core->CancelOfflineAction(id, [this](hstring const&, hstring const&)
                    {
                        RefreshPendingActions();
                    });
                });
                row.Children().Append(cancel);
                panel.Children().Append(row);
            }

            if (actions.Size() == 0)
            {
                TextBlock empty;
                empty.Text(Tr(L"Aucune action en attente"));
                empty.Foreground(secondary_brush);
                panel.Children().Append(empty);
            }
        });
    }

    /** @brief « Tout annuler » : abandonne toutes les actions du journal. */
    void MainWindow::OnPendingActionsCancelAllClick(IInspectable const&, RoutedEventArgs const&)
    {
        m_core->CancelAllOfflineActions([this](hstring const&, hstring const&)
        {
            RefreshPendingActions();
        });
    }

    /** @brief « Rejouer maintenant » : relance aussi les actions abandonnées. */
    void MainWindow::OnPendingActionsReplayClick(IInspectable const&, RoutedEventArgs const&)
    {
        m_core->ReplayOfflineQueue(true, [this](hstring const& json, hstring const&)
        {
            JsonObject result;
            if (JsonObject::TryParse(json, result) && result.GetNamedString(L"status", L"") == L"offline")
                ShowStatus(Tr(L"Hors connexion : les actions seront rejouées au retour du réseau."), InfoBarSeverity::Warning);
            RefreshPendingActions();
        });
    }

    /**
     * @brief Bilan d'un rejeu de la file hors ligne, annoncé dans la barre d'état.
     * @param report Événement « offline-replay » {"done", "gone", "failed", "remaining"}.
     */
    void MainWindow::OnOfflineReplayEvent(JsonObject const& report)
    {
        hstring done = to_hstring(static_cast<int>(report.GetNamedNumber(L"done", 0)));
        hstring gone = to_hstring(static_cast<int>(report.GetNamedNumber(L"gone", 0)));
        hstring failed = to_hstring(static_cast<int>(report.GetNamedNumber(L"failed", 0)));
        bool problem = report.GetNamedNumber(L"failed", 0) > 0;

        ShowStatus(Fmt(Tr(L"Actions hors connexion rejouées : {0} effectuée(s), {1} sans objet (message disparu), {2} en échec."),
            { done, gone, failed }), problem ? InfoBarSeverity::Warning : InfoBarSeverity::Success);
        RefreshPendingActions();
    }

    /**
     * @brief Boîte de dialogue d'un conflit de modification (événement ou contact).
     *
     * Affiche les champs en désaccord (votre version / version distante) et laisse choisir :
     * garder la version locale, la version distante ou les deux (la locale devient un nouvel élément).
     * L'enregistrement est ensuite renvoyé au moteur avec la résolution choisie.
     * @param kind "event" ou "contact".
     * @param conflict Réponse du moteur {"conflict", "fields": [{"name", "local", "remote"}]}.
     * @param request Modification initialement demandée (renvoyée avec "conflict_resolution").
     * @param on_done Appelé une fois la résolution appliquée (rechargement de la vue).
     */
    fire_and_forget MainWindow::ShowConflictDialogAsync(hstring kind, JsonObject conflict, JsonObject request, std::function<void()> on_done)
    {
        auto strong = get_strong();

        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;
        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(Tr(L"Conflit de modification")));
        dialog.PrimaryButtonText(Tr(L"Appliquer"));
        dialog.CloseButtonText(Tr(L"Annuler"));
        dialog.DefaultButton(ContentDialogButton::Primary);

        auto secondary_brush = ::EvolutionWin::Helpers::ThemedBrush(L"TextFillColorSecondaryBrush");

        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(460);

        TextBlock intro;
        intro.TextWrapping(TextWrapping::Wrap);
        intro.Text(kind == L"event"
            ? Tr(L"Cet événement a été modifié ailleurs (sur le serveur ou depuis un autre appareil) pendant que vous le modifiiez.")
            : Tr(L"Ce contact a été modifié ailleurs (sur le serveur ou depuis un autre appareil) pendant que vous le modifiiez."));
        panel.Children().Append(intro);

        for (auto const& value : conflict.GetNamedArray(L"fields", JsonArray{}))
        {
            auto field = value.GetObject();
            hstring name = field.GetNamedString(L"name", L"");

            StackPanel block;
            TextBlock label;
            label.Text(FieldLabel(name));
            label.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
            block.Children().Append(label);

            TextBlock local_text;
            local_text.Text(Fmt(Tr(L"Votre version : {0}"), { FieldValueText(name, field.GetNamedString(L"local", L"")) }));
            local_text.TextWrapping(TextWrapping::Wrap);
            block.Children().Append(local_text);

            TextBlock remote_text;
            remote_text.Text(Fmt(Tr(L"Version distante : {0}"), { FieldValueText(name, field.GetNamedString(L"remote", L"")) }));
            remote_text.TextWrapping(TextWrapping::Wrap);
            remote_text.Foreground(secondary_brush);
            block.Children().Append(remote_text);
            panel.Children().Append(block);
        }

        RadioButtons choice;
        choice.Items().Append(box_value(Tr(L"Garder la version locale")));
        choice.Items().Append(box_value(Tr(L"Garder la version distante")));
        choice.Items().Append(box_value(Tr(L"Garder les deux")));
        choice.SelectedIndex(2);
        panel.Children().Append(choice);

        TextBlock hint;
        hint.Text(Tr(L"« Garder les deux » enregistre votre version comme un nouvel élément et conserve l'original distant."));
        hint.TextWrapping(TextWrapping::Wrap);
        hint.Foreground(secondary_brush);
        panel.Children().Append(hint);

        ScrollViewer scroller;
        scroller.MaxHeight(480);
        scroller.Content(panel);
        dialog.Content(scroller);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result != ContentDialogResult::Primary)
            co_return;

        static constexpr wchar_t const* kResolutions[] = { L"local", L"remote", L"both" };
        int selected = choice.SelectedIndex();
        request.SetNamedValue(L"conflict_resolution",
            JsonValue::CreateStringValue(kResolutions[selected >= 0 && selected < 3 ? selected : 2]));

        auto handler = [this, on_done](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(Fmt(Tr(L"Résolution du conflit : {0}"), { error }), InfoBarSeverity::Error);
                return;
            }
            ShowStatus(Tr(L"Conflit résolu."), InfoBarSeverity::Success);
            if (on_done)
                on_done();
        };

        if (kind == L"event")
            m_core->ModifyEvent(request.Stringify(), handler);
        else
            m_core->ModifyContact(request.Stringify(), handler);
    }
}
