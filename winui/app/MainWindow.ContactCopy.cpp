#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"

#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.DragDrop.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

/**
 * @file MainWindow.ContactCopy.cpp
 * @brief Contacts : copier / coller d'un ou plusieurs contacts d'un carnet d'adresses à l'autre (clic droit,
 * Ctrl+C / Ctrl+V) et glisser-déposer d'un contact sur un carnet d'adresses du volet de gauche.
 *
 * Coller dans le carnet d'origine propose d'ajouter « (copier) » au nom des copies. Le glisser copie ;
 * avec la touche Maj, il déplace (l'original est supprimé une fois la copie réussie).
 */

namespace winrt::EvolutionWin::implementation
{
    /**
     * @brief Nom d'affichage d'un carnet d'adresses.
     * @param book_uid Identifiant du carnet.
     * @return Nom du carnet, ou son identifiant s'il est inconnu.
     */
    static hstring BookDisplayName(JsonArray const& books, hstring const& book_uid)
    {
        for (auto const& value : books ? books : JsonArray{})
        {
            auto book = value.GetObject();
            if (book.GetNamedString(L"uid", L"") == book_uid)
                return book.GetNamedString(L"name", book_uid);
        }
        return book_uid;
    }

    /**
     * @brief Contacts actuellement sélectionnés dans la liste (carnet et identifiant de chacun).
     * @return Références des contacts sélectionnés ; vide pour l'annuaire de l'organisation.
     */
    std::vector<MainWindow::ContactRef> MainWindow::SelectedContactRefs()
    {
        std::vector<ContactRef> refs;
        if (m_gal_mode)
            return refs;
        for (auto const& object : ContactList().SelectedItems())
        {
            auto row = object.try_as<StackPanel>();
            JsonObject contact;
            if (!row || !JsonObject::TryParse(unbox_value_or<hstring>(row.Tag(), L""), contact))
                continue;
            hstring book_uid = contact.GetNamedString(L"book_uid", L"");
            hstring uid = contact.GetNamedString(L"uid", L"");
            if (!book_uid.empty() && !uid.empty())
                refs.push_back({ book_uid, uid, contact.GetNamedString(L"full_name", L"") });
        }
        return refs;
    }

    /**
     * @brief Copie les contacts sélectionnés : ils seront collés (dupliqués) dans un carnet d'adresses.
     */
    void MainWindow::CopySelectedContacts()
    {
        auto refs = SelectedContactRefs();
        if (refs.empty())
            return;
        m_contact_clipboard = refs;
        ShowStatus(Fmt(Tr(L"{0} contact(s) copié(s) : clic droit sur un carnet d'adresses, puis « Coller »."),
            { to_hstring(static_cast<uint32_t>(refs.size())) }), InfoBarSeverity::Informational);
    }

    /**
     * @brief Colle les contacts copiés dans un carnet d'adresses. Dans le carnet d'origine, propose d'ajouter
     *        « (copier) » au nom des copies.
     * @param target_book Identifiant du carnet de destination.
     */
    fire_and_forget MainWindow::PasteContactsAsync(hstring target_book)
    {
        auto strong = get_strong();
        if (m_contact_clipboard.empty() || target_book.empty())
            co_return;

        auto contacts = m_contact_clipboard;
        bool same_book = false;
        for (auto const& contact : contacts)
            same_book = same_book || contact.book_uid == target_book;

        hstring suffix = hstring{ L" " } + Tr(L"(copier)");
        bool rename = false;
        if (same_book)
        {
            if (m_dialog_open || !Content() || !Content().XamlRoot())
                co_return;
            m_dialog_open = true;

            ContentDialog dialog;
            dialog.RequestedTheme(::EvolutionWin::Helpers::AppElementTheme());  /* suit le thème choisi (clair / sombre) */
            dialog.XamlRoot(Content().XamlRoot());
            dialog.Title(box_value(Tr(L"Coller dans le même carnet d'adresses")));
            dialog.Content(box_value(Fmt(Tr(L"Les contacts collés viennent de ce carnet. Ajouter « {0} » au nom des copies pour les distinguer ?"),
                { hstring{ std::wstring{ suffix }.substr(1) } })));
            dialog.PrimaryButtonText(Tr(L"Renommer les copies"));
            dialog.SecondaryButtonText(Tr(L"Garder les mêmes noms"));
            dialog.CloseButtonText(Tr(L"Annuler"));
            dialog.DefaultButton(ContentDialogButton::Primary);
            auto result = co_await dialog.ShowAsync();
            m_dialog_open = false;
            if (result == ContentDialogResult::None)
                co_return;
            rename = result == ContentDialogResult::Primary;
        }

        TransferContacts(contacts, target_book, false, rename ? suffix : hstring{});
    }

    /**
     * @brief Copie (ou déplace) des contacts vers un carnet d'adresses, un lot par carnet d'origine, puis recharge la liste.
     * @param contacts Contacts à copier.
     * @param target_book Carnet de destination.
     * @param move Vrai : les originaux sont supprimés une fois les copies faites (carnets différents seulement).
     * @param rename_suffix Texte ajouté au nom des copies faites dans le carnet d'origine (vide : noms inchangés).
     */
    void MainWindow::TransferContacts(std::vector<ContactRef> contacts, hstring const& target_book, bool move, hstring const& rename_suffix)
    {
        if (!m_core || contacts.empty())
            return;

        std::map<hstring, JsonArray> by_book;   /* carnet d'origine -> identifiants */
        for (auto const& contact : contacts)
            by_book[contact.book_uid].Append(JsonValue::CreateStringValue(contact.uid));

        struct Totals { int pending{ 0 }; int copied{ 0 }; int failed{ 0 }; int removed{ 0 }; hstring error; };
        auto totals = std::make_shared<Totals>();
        totals->pending = static_cast<int>(by_book.size());
        hstring target_name = BookDisplayName(m_contact_books, target_book);

        for (auto const& [source_book, uids] : by_book)
        {
            JsonObject request;
            request.SetNamedValue(L"source_uid", JsonValue::CreateStringValue(source_book));
            request.SetNamedValue(L"target_uid", JsonValue::CreateStringValue(target_book));
            request.SetNamedValue(L"uids", uids);
            request.SetNamedValue(L"move", JsonValue::CreateBooleanValue(move));
            if (source_book == target_book && !rename_suffix.empty())
                request.SetNamedValue(L"rename_suffix", JsonValue::CreateStringValue(rename_suffix));

            m_core->CopyContacts(request.Stringify(), [this, totals, target_name, move](hstring const& json, hstring const& error)
            {
                JsonObject result;
                if (!error.empty() || !JsonObject::TryParse(json, result))
                {
                    totals->failed++;
                    if (totals->error.empty())
                        totals->error = error;
                }
                else
                {
                    totals->copied += static_cast<int>(result.GetNamedNumber(L"copied", 0));
                    totals->failed += static_cast<int>(result.GetNamedNumber(L"failed", 0));
                    totals->removed += static_cast<int>(result.GetNamedNumber(L"removed", 0));
                    if (totals->error.empty())
                        totals->error = result.GetNamedString(L"error", L"");
                }

                if (--totals->pending > 0)
                    return;

                if (totals->failed > 0)
                    ShowStatus(Fmt(Tr(L"Contacts : {0} copié(s), {1} en échec ({2})."),
                        { to_hstring(totals->copied), to_hstring(totals->failed), totals->error }), InfoBarSeverity::Warning);
                else if (move)
                    ShowStatus(Fmt(Tr(L"{0} contact(s) déplacé(s) vers « {1} »."),
                        { to_hstring(totals->copied), target_name }), InfoBarSeverity::Success);
                else
                    ShowStatus(Fmt(Tr(L"{0} contact(s) collé(s) dans « {1} »."),
                        { to_hstring(totals->copied), target_name }), InfoBarSeverity::Success);
                LoadContacts(ContactSearchBox().Text());
            });
        }
    }

    /**
     * @brief Rend une ligne du volet des carnets d'adresses sensible au clic droit (« Coller ») et au dépôt de contacts
     *        glissés depuis la liste (copie, ou déplacement avec Maj).
     * @param row Ligne du carnet.
     * @param source Carnet au format JSON du moteur (uid, name).
     */
    void MainWindow::DecorateContactBookRow(Grid const& row, JsonObject const& source)
    {
        hstring book_uid = source.GetNamedString(L"uid", L"");
        hstring book_name = source.GetNamedString(L"name", book_uid);
        if (book_uid.empty())
            return;

        /* clic droit : Coller (actif seulement si des contacts ont été copiés) */
        MenuFlyout flyout;
        MenuFlyoutItem paste_item;
        paste_item.Text(Tr(L"Coller"));
        FontIcon paste_icon;
        paste_icon.Glyph(L"\xE77F");
        paste_item.Icon(paste_icon);
        paste_item.Click([this, book_uid](IInspectable const&, RoutedEventArgs const&) { PasteContactsAsync(book_uid); });
        flyout.Items().Append(paste_item);
        flyout.Opening([this, paste_item](IInspectable const&, IInspectable const&) { paste_item.IsEnabled(!m_contact_clipboard.empty()); });
        row.ContextFlyout(flyout);

        /* dépôt de contacts glissés : copie, ou déplacement avec Maj ; sans effet sur les contacts déjà dans ce carnet */
        auto original = row.Background();
        auto other_books = [this, book_uid]()
        {
            std::vector<ContactRef> others;
            for (auto const& contact : m_dragged_contacts)
                if (contact.book_uid != book_uid)
                    others.push_back(contact);
            return others;
        };
        row.AllowDrop(true);
        row.DragOver([this, row, book_name, other_books](IInspectable const&, DragEventArgs const& args)
        {
            if (other_books().empty())
                return;
            namespace DragDrop = Windows::ApplicationModel::DataTransfer::DragDrop;
            bool move = (args.Modifiers() & DragDrop::DragDropModifiers::Shift) == DragDrop::DragDropModifiers::Shift;
            args.AcceptedOperation(move ? Windows::ApplicationModel::DataTransfer::DataPackageOperation::Move
                                        : Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
            args.DragUIOverride().Caption(move ? Fmt(Tr(L"Déplacer vers « {0} »"), { book_name })
                                               : Fmt(Tr(L"Copier vers « {0} »"), { book_name }));
            row.Background(ThemedBrush(L"SubtleFillColorSecondaryBrush"));
        });
        row.DragLeave([row, original](IInspectable const&, DragEventArgs const&) { row.Background(original); });
        row.Drop([this, row, original, book_uid, other_books](IInspectable const&, DragEventArgs const& args)
        {
            row.Background(original);
            auto others = other_books();
            if (others.empty())
                return;
            namespace DragDrop = Windows::ApplicationModel::DataTransfer::DragDrop;
            bool move = (args.Modifiers() & DragDrop::DragDropModifiers::Shift) == DragDrop::DragDropModifiers::Shift;
            TransferContacts(others, book_uid, move, hstring{});
        });
    }

    /**
     * @brief Ctrl+C dans la vue Contacts : copie les contacts sélectionnés (sans effet dans une zone de saisie).
     */
    void MainWindow::OnContactsCopyAccelerator(Input::KeyboardAccelerator const&, Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        if (Content() && Content().XamlRoot())
            if (Input::FocusManager::GetFocusedElement(Content().XamlRoot()).try_as<TextBox>())
                return;
        if (ContactsViewPane().Visibility() != Visibility::Visible || SelectedContactRefs().empty())
            return;
        args.Handled(true);
        CopySelectedContacts();
    }

    /**
     * @brief Ctrl+V dans la vue Contacts : colle les contacts copiés dans le carnet actif.
     */
    void MainWindow::OnContactsPasteAccelerator(Input::KeyboardAccelerator const&, Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        if (Content() && Content().XamlRoot())
            if (Input::FocusManager::GetFocusedElement(Content().XamlRoot()).try_as<TextBox>())
                return;
        if (ContactsViewPane().Visibility() != Visibility::Visible || m_contact_clipboard.empty())
            return;
        args.Handled(true);
        PasteContactsAsync(ActiveBookUid());
    }
}
