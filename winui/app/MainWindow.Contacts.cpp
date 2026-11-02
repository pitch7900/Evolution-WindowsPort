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

/* Contacts : liste, fiche, création, suppression et export. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    /* Charge la liste des contacts depuis les carnets d'adresses EDS.
     * Paramètres :
     *   query : texte de filtrage optionnel sur le nom ou l'adresse e-mail */
    void MainWindow::LoadContacts(hstring const& query)
    {
        if (!m_contact_books)
        {
            LoadContactBooks();
            return;
        }

        std::wstring visible;
        for (auto const& uid : VisibleBookUids())
            visible += (visible.empty() ? L"" : L",") + std::wstring{ uid };

        if (visible.empty())
        {
            ContactList().Items().Clear();
            TextBlock none;
            none.Text(L"Aucun carnet affiché : cochez un carnet d'adresses à gauche.");
            none.TextWrapping(TextWrapping::Wrap);
            none.Padding(ThicknessHelper::FromLengths(8, 8, 8, 8));
            ContactList().Items().Append(none);
            return;
        }

        m_core->ListBookContacts(query, hstring{ visible }, [this](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Contacts : " + error, InfoBarSeverity::Error);
                return;
            }

            auto items = ContactList().Items();
            items.Clear();
            m_selected_contact = nullptr;
            ContactDetailPane().Visibility(Visibility::Collapsed);
            ContactPlaceholder().Visibility(Visibility::Visible);

            auto contacts = JsonArray::Parse(json);
            for (auto const& value : contacts)
            {
                auto contact = value.GetObject();
                hstring full_name = contact.GetNamedString(L"full_name", L"(Sans nom)");
                hstring email = contact.GetNamedString(L"email", L"");

                StackPanel row;
                row.Padding(ThicknessHelper::FromLengths(8, 6, 8, 6));
                row.Tag(box_value(contact.Stringify()));

                TextBlock name_text;
                name_text.Text(full_name);
                name_text.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
                row.Children().Append(name_text);

                if (!email.empty())
                {
                    TextBlock email_text;
                    email_text.Text(email);
                    email_text.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                    row.Children().Append(email_text);
                }

                items.Append(row);
            }
        });
    }

    /* Gestionnaire de modification du champ de recherche de contacts. */
    void MainWindow::OnContactSearchChanged(AutoSuggestBox const& sender, AutoSuggestBoxTextChangedEventArgs const& args)
    {
        if (args.Reason() == AutoSuggestionBoxTextChangeReason::UserInput)
            LoadContacts(sender.Text());
    }

    /* Gestionnaire de validation de la recherche de contacts. */
    void MainWindow::OnContactSearchSubmitted(AutoSuggestBox const&, AutoSuggestBoxQuerySubmittedEventArgs const& args)
    {
        LoadContacts(args.QueryText());
    }

    /* Gestionnaire de sélection d'un contact dans la liste : affiche sa fiche détaillée. */
    void MainWindow::OnContactSelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        auto panel = ContactList().SelectedItem().try_as<StackPanel>();
        if (!panel)
        {
            m_selected_contact = nullptr;
            ContactDetailPane().Visibility(Visibility::Collapsed);
            ContactPlaceholder().Visibility(Visibility::Visible);
            return;
        }

        hstring raw = unbox_value_or<hstring>(panel.Tag(), L"");
        JsonObject contact;
        if (!JsonObject::TryParse(raw, contact))
            return;

        m_selected_contact = contact;
        ContactNameText().Text(contact.GetNamedString(L"full_name", L"(Sans nom)"));
        ContactEmailText().Text(contact.GetNamedString(L"email", L"—"));
        ContactPhoneText().Text(contact.GetNamedString(L"phone", L"—"));
        ContactOrgText().Text(contact.GetNamedString(L"org", L"—"));
        ContactNotesText().Text(contact.GetNamedString(L"note", L""));

        ContactPlaceholder().Visibility(Visibility::Collapsed);
        ContactDetailPane().Visibility(Visibility::Visible);
    }

    /* Gestionnaire du bouton « Nouveau contact ». */
    void MainWindow::OnNewContactClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowNewContactDialogAsync();
    }

    /* Affiche une boîte de dialogue permettant d'ajouter un nouveau contact
     * dans le carnet d'adresses EDS et l'enregistre via evo-core. */
    fire_and_forget MainWindow::ShowNewContactDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Nouveau contact"));
        dialog.PrimaryButtonText(L"Enregistrer");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(380);

        TextBox name_box;
        name_box.Header(box_value(L"Nom complet"));
        name_box.PlaceholderText(L"Jean Martin");
        panel.Children().Append(name_box);

        TextBox email_box;
        email_box.Header(box_value(L"Adresse électronique"));
        email_box.PlaceholderText(L"jean.martin@exemple.fr");
        panel.Children().Append(email_box);

        TextBox phone_box;
        phone_box.Header(box_value(L"Téléphone"));
        panel.Children().Append(phone_box);

        TextBox org_box;
        org_box.Header(box_value(L"Organisation"));
        panel.Children().Append(org_box);

        TextBox note_box;
        note_box.Header(box_value(L"Notes"));
        note_box.AcceptsReturn(true);
        note_box.Height(72);
        panel.Children().Append(note_box);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result != ContentDialogResult::Primary || name_box.Text().empty())
            co_return;

        JsonObject payload;
        payload.SetNamedValue(L"full_name", JsonValue::CreateStringValue(name_box.Text()));
        payload.SetNamedValue(L"email", JsonValue::CreateStringValue(email_box.Text()));
        payload.SetNamedValue(L"phone", JsonValue::CreateStringValue(phone_box.Text()));
        payload.SetNamedValue(L"org", JsonValue::CreateStringValue(org_box.Text()));
        payload.SetNamedValue(L"note", JsonValue::CreateStringValue(note_box.Text()));
        if (hstring active = ActiveBookUid(); !active.empty())
            payload.SetNamedValue(L"book_uid", JsonValue::CreateStringValue(active));

        m_core->CreateContact(payload.Stringify(), [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Création du contact : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Contact enregistré.", InfoBarSeverity::Success);
            LoadContacts(ContactSearchBox().Text());
        });
    }

    /* Gestionnaire du bouton « Exporter » des contacts : liste les carnets
     * d'adresses, puis propose l'exportation vCard. */
    void MainWindow::OnExportContactsClick(IInspectable const&, RoutedEventArgs const&)
    {
        m_core->ListSources(L"contacts", [this](hstring const& json, hstring const& error)
        {
            JsonArray books;
            if (!error.empty() || !JsonArray::TryParse(json, books))
            {
                ShowStatus(L"Exportation : " + error, InfoBarSeverity::Error);
                return;
            }
            if (books.Size() == 0)
            {
                ShowStatus(L"Aucun carnet d'adresses à exporter.", InfoBarSeverity::Warning);
                return;
            }
            ExportContactsAsync(books);
        });
    }

    /* Demande le carnet d'adresses et le format (un seul fichier .vcf ou un
     * fichier par contact), puis la destination, et lance l'exportation vCard.
     * Paramètres :
     *   books : tableau JSON [{"uid", "name", "kind"}] des carnets d'adresses */
    fire_and_forget MainWindow::ExportContactsAsync(JsonArray books)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Exporter les contacts"));
        dialog.PrimaryButtonText(L"Exporter");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(380);

        ComboBox book_box;
        book_box.Header(box_value(L"Carnet d'adresses"));
        book_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        book_box.Items().Append(box_value(L"Tous les carnets d'adresses"));
        for (auto const& value : books)
            book_box.Items().Append(box_value(value.GetObject().GetNamedString(L"name", L"")));
        book_box.SelectedIndex(0);
        panel.Children().Append(book_box);

        RadioButtons format;
        format.Header(box_value(L"Format vCard"));
        format.Items().Append(box_value(L"Un seul fichier .vcf"));
        format.Items().Append(box_value(L"Un fichier .vcf par contact, dans un dossier"));
        format.SelectedIndex(0);
        panel.Children().Append(format);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result != ContentDialogResult::Primary)
            co_return;

        int index = book_box.SelectedIndex();
        hstring book_uid;
        hstring name = L"Contacts";
        if (index > 0)
        {
            auto book = books.GetObjectAt(static_cast<uint32_t>(index - 1));
            book_uid = book.GetNamedString(L"uid", L"");
            name = book.GetNamedString(L"name", L"Contacts");
        }
        bool per_contact = format.SelectedIndex() == 1;

        hstring dest;
        if (per_contact)
        {
            Windows::Storage::Pickers::FolderPicker picker;
            InitializePicker(picker);
            picker.FileTypeFilter().Append(L"*");

            auto folder = co_await picker.PickSingleFolderAsync();
            if (!folder)
                co_return;
            dest = folder.Path();
        }
        else
        {
            Windows::Storage::Pickers::FileSavePicker picker;
            InitializePicker(picker);
            picker.SuggestedFileName(SanitizeFileName(name, L"Contacts"));
            picker.FileTypeChoices().Insert(L"Carte de visite vCard", single_threaded_vector<hstring>({ L".vcf" }));

            auto file = co_await picker.PickSaveFileAsync();
            if (!file)
                co_return;
            dest = file.Path();
        }

        JsonObject request;
        request.SetNamedValue(L"book_uid", JsonValue::CreateStringValue(book_uid));
        request.SetNamedValue(L"path", JsonValue::CreateStringValue(dest));
        request.SetNamedValue(L"per_contact", JsonValue::CreateBooleanValue(per_contact));

        ShowStatus(L"Exportation des contacts…", InfoBarSeverity::Informational);
        m_core->ExportContacts(request.Stringify(), [this, dest](hstring const& json, hstring const& error)
        {
            JsonObject answer;
            if (!error.empty() || !JsonObject::TryParse(json, answer))
            {
                ShowStatus(L"Exportation des contacts : " + error, InfoBarSeverity::Error);
                return;
            }
            auto count = static_cast<uint32_t>(answer.GetNamedNumber(L"count", 0));
            ShowStatus(hstring{ std::to_wstring(count) + L" contact(s) exporté(s) dans « " + std::wstring{ dest } + L" »." },
                InfoBarSeverity::Success);
        });
    }

    /* Gestionnaire du bouton « Écrire un message » sur la fiche d'un contact :
     * ouvre la fenêtre de rédaction pré-remplie avec l'adresse du contact. */
    void MainWindow::OnComposeToContactClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_selected_contact)
            return;

        hstring email = m_selected_contact.GetNamedString(L"email", L"");
        hstring name = m_selected_contact.GetNamedString(L"full_name", L"");
        if (email.empty())
        {
            ShowStatus(L"Ce contact n'a pas d'adresse électronique.", InfoBarSeverity::Warning);
            return;
        }

        ::EvolutionWin::ComposeData data;
        data.account = m_account;
        data.to = name.empty() ? email : name + L" <" + email + L">";
        OpenCompose(data);
    }

    /* Gestionnaire du bouton « Supprimer le contact » sur la fiche d'un contact. */
    void MainWindow::OnDeleteContactClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_selected_contact)
            return;

        hstring book_uid = m_selected_contact.GetNamedString(L"book_uid", L"");
        hstring uid = m_selected_contact.GetNamedString(L"uid", L"");
        if (book_uid.empty() || uid.empty())
            return;

        m_core->DeleteContact(book_uid, uid, [this](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Suppression du contact : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Contact supprimé.", InfoBarSeverity::Informational);
            LoadContacts(ContactSearchBox().Text());
        });
    }

    /**
     * @brief Ouvre le carnet d'adresses en basculant directement vers le module Contacts.
     * @param sender Bouton de commande émetteur.
     * @param args Arguments de l'événement routé.
     */
    void MainWindow::OnAddressBookClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        SelectNavigationModule(L"contacts");
    }

    /**
     * @brief Filtre dynamiquement les suggestions de contacts dans la zone de recherche rapide du ruban.
     * @param sender AutoSuggestBox du ruban.
     * @param args Arguments de modification de texte.
     */
    void MainWindow::OnRibbonContactSearchTextChanged(
        AutoSuggestBox const& sender,
        AutoSuggestBoxTextChangedEventArgs const& args)
    {
        if (args.Reason() != AutoSuggestionBoxTextChangeReason::UserInput)
            return;

        hstring query = sender.Text();
        if (query.empty())
        {
            sender.ItemsSource(nullptr);
            return;
        }

        if (!m_contact_books)
            LoadContactBooks();

        std::wstring visible;
        for (auto const& uid : VisibleBookUids())
            visible += (visible.empty() ? L"" : L",") + std::wstring{ uid };

        m_core->ListBookContacts(query, hstring{ visible }, [this, sender, query](hstring const& json, hstring const& error)
        {
            if (!error.empty())
                return;

            JsonArray contacts;
            if (!JsonArray::TryParse(json, contacts))
                return;

            std::vector<hstring> suggestions;
            for (auto const& value : contacts)
            {
                auto contact = value.GetObject();
                hstring full_name = contact.GetNamedString(L"full_name", L"");
                hstring email = contact.GetNamedString(L"email", L"");
                std::wstring formatted = ::EvolutionWin::Helpers::FormatContactSuggestion(full_name, email);
                suggestions.push_back(hstring{ formatted });
            }

            DispatcherQueue().TryEnqueue([sender, suggestions = std::move(suggestions)]()
            {
                auto vec = single_threaded_vector<hstring>(std::move(const_cast<std::vector<hstring>&>(suggestions)));
                sender.ItemsSource(vec);
            });
        });
    }

    /**
     * @brief Valide la recherche de contact rapide dans le ruban et navigue vers la vue Contacts avec le filtre.
     * @param sender AutoSuggestBox émetteur.
     * @param args Arguments de validation de requête.
     */
    void MainWindow::OnRibbonContactSearchQuerySubmitted(
        [[maybe_unused]] AutoSuggestBox const& sender,
        AutoSuggestBoxQuerySubmittedEventArgs const& args)
    {
        hstring query = args.QueryText();
        SelectNavigationModule(L"contacts");
        ContactSearchBox().Text(query);
        LoadContacts(query);
    }

    /**
     * @brief Sélectionne une suggestion de contact choisie dans la liste déroulante du ruban.
     * @param sender AutoSuggestBox émetteur.
     * @param args Arguments de l'élément sélectionné.
     */
    void MainWindow::OnRibbonContactSearchSuggestionChosen(
        [[maybe_unused]] AutoSuggestBox const& sender,
        AutoSuggestBoxSuggestionChosenEventArgs const& args)
    {
        if (!args.SelectedItem())
            return;

        hstring chosen = unbox_value_or<hstring>(args.SelectedItem(), L"");
        // Extrait le nom avant les chevrons s'il y a une adresse
        std::wstring query{ chosen };
        auto open_bracket = query.find(L" <");
        if (open_bracket != std::wstring::npos)
        {
            query = query.substr(0, open_bracket);
        }

        SelectNavigationModule(L"contacts");
        ContactSearchBox().Text(hstring{ query });
        LoadContacts(hstring{ query });
    }
}
