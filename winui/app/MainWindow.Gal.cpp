#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "Gal.h"

#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <chrono>
#include <string>
#include <vector>

/* Annuaire de l'organisation (carnet d'adresses global, GAL) : recherche et
 * consultation en lecture seule depuis la vue Contacts. Les fiches reprennent
 * la liste et le volet de détail des contacts (MainWindow.Contacts.cpp). */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    namespace
    {
        /**
         * @brief Ajoute une ligne de message (consigne, attente ou erreur) dans la liste des contacts.
         * @param list Liste des contacts.
         * @param text Texte affiché.
         */
        void AppendGalMessage(ListView const& list, hstring const& text)
        {
            TextBlock message;
            message.Text(text);
            message.TextWrapping(TextWrapping::Wrap);
            message.Padding(ThicknessHelper::FromLengths(8, 8, 8, 8));
            list.Items().Append(message);
        }

        /**
         * @brief Lit une fiche d'annuaire depuis l'objet JSON d'evo_core_search_gal.
         * @param json Objet JSON d'une personne.
         * @return Fiche d'annuaire.
         */
        ::EvolutionWin::Gal::Person ReadGalPerson(JsonObject const& json)
        {
            ::EvolutionWin::Gal::Person person;
            person.name = json.GetNamedString(L"name", L"");
            person.email = json.GetNamedString(L"email", L"");
            person.title = json.GetNamedString(L"title", L"");
            person.department = json.GetNamedString(L"department", L"");
            person.company = json.GetNamedString(L"company", L"");
            person.office = json.GetNamedString(L"office", L"");
            person.phone = json.GetNamedString(L"phone", L"");
            person.mobile = json.GetNamedString(L"mobile", L"");
            person.phone2 = json.GetNamedString(L"phone2", L"");
            person.address = json.GetNamedString(L"address", L"");
            person.photo_url = json.GetNamedString(L"photo_url", L"");
            person.account = json.GetNamedString(L"account", L"");
            person.account_uid = json.GetNamedString(L"account_uid", L"");
            person.provider = json.GetNamedString(L"provider", L"");
            return person;
        }
    }

    /**
     * @brief Active ou désactive le mode « annuaire de l'organisation » de la vue Contacts :
     *        les boutons Modifier et Supprimer sont masqués (fiches en lecture seule), le
     *        champ de recherche et le libellé du carnet actif s'adaptent. Appelé avec false
     *        par LoadContacts () pour revenir aux carnets d'adresses.
     * @param on true pour afficher l'annuaire, false pour revenir aux carnets.
     */
    void MainWindow::SetGalMode(bool on)
    {
        bool was_on = m_gal_mode;
        m_gal_mode = on;
        ++m_gal_request;   /* invalide toute recherche d'annuaire en cours */

        GalDirectoryButton().Content(box_value(Tr(L"Annuaire de l'organisation", L"Organization directory")));
        ContactSearchBox().PlaceholderText(on
            ? Tr(L"Rechercher dans l'annuaire (nom ou adresse)", L"Search the directory (name or address)")
            : Tr(L"Rechercher un contact", L"Search contacts"));
        ContactEditButton().Visibility(on ? Visibility::Collapsed : Visibility::Visible);
        ContactDeleteButton().Visibility(on ? Visibility::Collapsed : Visibility::Visible);

        if (on)
        {
            ActiveBookText().Text(Tr(L"Annuaire de l'organisation (lecture seule)", L"Organization directory (read-only)"));
        }
        else if (was_on)
        {
            if (m_gal_timer)
                m_gal_timer.Stop();
            if (m_contact_books)
                RebuildContactBooksPanel();
        }
    }

    /**
     * @brief Gestionnaire du bouton « Annuaire de l'organisation » : bascule la liste des
     *        contacts sur l'annuaire et lance la recherche de la saisie courante.
     * @param sender Bouton émetteur.
     * @param args Arguments de l'événement routé.
     */
    void MainWindow::OnGalDirectoryClick(IInspectable const&, RoutedEventArgs const&)
    {
        OnGalDirectoryForAccount(hstring{});
    }

    /**
     * @brief Ouvre l'annuaire de l'organisation d'un compte (lien « Annuaire » sous le compte).
     * @param account_uid Identifiant du compte à interroger ("" : tous les comptes).
     */
    void MainWindow::OnGalDirectoryForAccount(hstring const& account_uid)
    {
        SetGalMode(true);
        m_gal_account = account_uid;
        m_selected_contact = nullptr;
        ContactList().Items().Clear();
        ContactDetailPane().Visibility(Visibility::Collapsed);
        ContactPlaceholder().Visibility(Visibility::Visible);
        SearchGalDirectory(ContactSearchBox().Text());
        ContactSearchBox().Focus(FocusState::Programmatic);
    }

    /**
     * @brief Planifie la recherche d'annuaire après une courte pause de frappe (350 ms),
     *        pour ne pas interroger le serveur à chaque touche.
     * @param query Saisie courante (relue au déclenchement du minuteur).
     */
    void MainWindow::ScheduleGalSearch(hstring const&)
    {
        if (!m_gal_timer)
        {
            m_gal_timer = DispatcherTimer();
            m_gal_timer.Interval(std::chrono::milliseconds(350));
            m_gal_timer.Tick([weak = get_weak()](IInspectable const&, IInspectable const&)
            {
                if (auto self = weak.get())
                {
                    self->m_gal_timer.Stop();
                    if (self->m_gal_mode)
                        self->SearchGalDirectory(self->ContactSearchBox().Text());
                }
            });
        }
        m_gal_timer.Stop();
        m_gal_timer.Start();
    }

    /**
     * @brief Recherche dans l'annuaire de l'organisation (2 caractères au moins) et remplit la
     *        liste des contacts avec les fiches trouvées (lecture seule). Les fiches sont
     *        converties au format des contacts (full_name, email, phone, org, note) pour
     *        réutiliser le volet de détail.
     * @param query Texte recherché (début du nom, du prénom ou de l'adresse).
     */
    void MainWindow::SearchGalDirectory(hstring const& query)
    {
        namespace Gal = ::EvolutionWin::Gal;

        auto items = ContactList().Items();
        uint64_t request = ++m_gal_request;

        items.Clear();
        m_selected_contact = nullptr;
        ContactDetailPane().Visibility(Visibility::Collapsed);
        ContactPlaceholder().Visibility(Visibility::Visible);

        if (!Gal::QueryEligible(std::wstring_view{ query }, Gal::kMinQueryContacts))
        {
            AppendGalMessage(ContactList(), Tr(L"Saisissez au moins 2 caractères pour rechercher dans l'annuaire de l'organisation.",
                                               L"Type at least 2 characters to search the organization directory."));
            return;
        }

        AppendGalMessage(ContactList(), Tr(L"Recherche dans l'annuaire…", L"Searching the directory…"));

        m_core->SearchGalAccount(m_gal_account, query, 30, [this, request](hstring const& json, hstring const& error)
        {
            if (request != m_gal_request || !m_gal_mode)
                return;   /* résultat périmé : une autre recherche ou un autre carnet est affiché */

            auto items = ContactList().Items();
            items.Clear();

            if (!error.empty())
            {
                AppendGalMessage(ContactList(), error);
                return;
            }

            JsonArray people;
            if (!JsonArray::TryParse(json, people) || people.Size() == 0)
            {
                AppendGalMessage(ContactList(), Tr(L"Aucun résultat dans l'annuaire.", L"No results in the directory."));
                return;
            }

            hstring mobile_label = Tr(L"mobile", L"mobile");
            hstring office_label = Tr(L"Bureau", L"Office");
            hstring directory_label = Tr(L"Annuaire", L"Directory");

            for (auto const& value : people)
            {
                Gal::Person person = ReadGalPerson(value.GetObject());

                /* même format que les contacts des carnets : le volet de détail est partagé */
                JsonObject contact;
                contact.SetNamedValue(L"full_name", JsonValue::CreateStringValue(Gal::DisplayName(person)));
                contact.SetNamedValue(L"email", JsonValue::CreateStringValue(person.email));
                contact.SetNamedValue(L"phone", JsonValue::CreateStringValue(Gal::PhoneLine(person, std::wstring_view{ mobile_label })));
                contact.SetNamedValue(L"org", JsonValue::CreateStringValue(Gal::OrganizationLine(person)));
                contact.SetNamedValue(L"note", JsonValue::CreateStringValue(
                    Gal::NoteLine(person, std::wstring_view{ office_label }, std::wstring_view{ directory_label })));
                contact.SetNamedValue(L"gal", JsonValue::CreateBooleanValue(true));
                contact.SetNamedValue(L"address", JsonValue::CreateStringValue(person.address));
                contact.SetNamedValue(L"photo_url", JsonValue::CreateStringValue(person.photo_url));
                contact.SetNamedValue(L"account_uid", JsonValue::CreateStringValue(person.account_uid));
                contact.SetNamedValue(L"provider", JsonValue::CreateStringValue(person.provider));

                StackPanel row;
                row.Padding(ThicknessHelper::FromLengths(8, 6, 8, 6));
                row.Tag(box_value(contact.Stringify()));

                TextBlock name_text;
                name_text.Text(Gal::DisplayName(person));
                name_text.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
                row.Children().Append(name_text);

                std::wstring detail = Gal::JoinNonEmpty({ person.email, Gal::OrganizationLine(person) }, L" · ");
                TextBlock detail_text;
                detail_text.Text(detail);
                detail_text.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                row.Children().Append(detail_text);

                items.Append(row);
            }
        });
    }

    /**
     * @brief Affiche la photo de profil d'une fiche d'annuaire (volet de détail des contacts).
     *        Google : URL publique de la photo ; Microsoft 365 : lecture Graph /photo/$value ;
     *        sinon (ou sans photo) initiales du nom. La photo est masquée pour un contact de carnet.
     * @param contact Objet JSON de la fiche sélectionnée (clés gal, photo_url, provider, account_uid, email).
     */
    void MainWindow::ShowDirectoryPhoto(JsonObject const& contact)
    {
        if (!contact.GetNamedBoolean(L"gal", false))
        {
            ContactPhoto().Visibility(Visibility::Collapsed);
            return;
        }

        hstring name = contact.GetNamedString(L"full_name", L"");
        hstring email = contact.GetNamedString(L"email", L"");
        hstring photo_url = contact.GetNamedString(L"photo_url", L"");

        ContactPhoto().ProfilePicture(nullptr);
        ContactPhoto().DisplayName(name);
        ContactPhoto().Visibility(Visibility::Visible);

        if (!photo_url.empty())
        {
            try
            {
                Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
                bitmap.UriSource(Windows::Foundation::Uri{ photo_url });
                ContactPhoto().ProfilePicture(bitmap);
            }
            catch (...)
            {
            }
            return;
        }

        if (contact.GetNamedString(L"provider", L"") != L"microsoft365" || email.empty())
            return;

        m_core->GetGalPhoto(contact.GetNamedString(L"account_uid", L""), email,
            [this, email](hstring const& json, hstring const& error)
        {
            JsonObject result;
            if (!error.empty() || !JsonObject::TryParse(json, result) || !m_selected_contact ||
                m_selected_contact.GetNamedString(L"email", L"") != email)
                return;   /* échec, ou une autre fiche est affichée entre-temps */

            hstring data = result.GetNamedString(L"photo", L"");
            constexpr std::wstring_view prefix = L"base64,";
            std::wstring_view text{ data };
            auto at = text.find(prefix);
            if (at == std::wstring_view::npos)
                return;

            auto buffer = Windows::Security::Cryptography::CryptographicBuffer::DecodeFromBase64String(
                hstring{ text.substr(at + prefix.size()) });
            Windows::Storage::Streams::InMemoryRandomAccessStream stream;
            Windows::Storage::Streams::DataWriter writer{ stream };
            writer.WriteBuffer(buffer);
            writer.StoreAsync().get();
            writer.DetachStream();
            stream.Seek(0);

            Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
            bitmap.SetSource(stream);
            ContactPhoto().ProfilePicture(bitmap);
        });
    }
}
