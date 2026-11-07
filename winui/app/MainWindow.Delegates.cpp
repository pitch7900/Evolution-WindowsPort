#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include <functional>

/* Délégations et autorisations : boîte de dialogue de gestion des délégués de la boîte
 * d'un compte Gmail / Google Workspace (liste, ajout, révocation avec confirmation ;
 * evo_core_list_delegates / evo_core_add_delegate / evo_core_remove_delegate). Les
 * autres comptes reçoivent l'explication du moteur (Microsoft 365 : droits gérés dans
 * le centre d'administration Exchange). */

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
         * @brief Libellé localisé de l'état d'un délégué Gmail.
         * @param status État renvoyé par l'API (accepted, pending, rejected, expired).
         * @return Texte localisé.
         */
        hstring StatusText(hstring const& status)
        {
            if (status == L"pending")
                return Tr(L"En attente de confirmation");
            if (status == L"rejected")
                return Tr(L"Refusé");
            if (status == L"expired")
                return Tr(L"Invitation expirée");
            return Tr(L"Accepté");
        }
    }

    /**
     * @brief Affiche la boîte « Délégations et autorisations » d'un compte.
     * @param account Identifiant du compte.
     * @param name Nom affiché du compte.
     */
    fire_and_forget MainWindow::ShowDelegatesDialogAsync(hstring account, hstring name)
    {
        auto strong = get_strong();

        if (m_dialog_open || !m_core || !Content() || !Content().XamlRoot())
            co_return;
        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(Fmt(Tr(L"Délégations et autorisations - {0}"), { name })));
        dialog.CloseButtonText(Tr(L"Fermer", L"Close", L"Common_Close"));

        auto secondary_brush = Application::Current().Resources().Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Media::Brush>();

        StackPanel panel;
        panel.Spacing(12);
        panel.MinWidth(520);

        TextBlock info;
        info.TextWrapping(TextWrapping::Wrap);
        info.Foreground(secondary_brush);
        info.Text(Tr(L"Les délégués peuvent lire et envoyer des messages depuis votre boîte Gmail (Google Workspace). Un nouveau délégué doit confirmer l'invitation reçue par courriel."));
        panel.Children().Append(info);

        TextBlock status;
        status.TextWrapping(TextWrapping::Wrap);
        status.Text(Tr(L"Lecture des délégués sur le serveur…"));
        panel.Children().Append(status);

        StackPanel list;
        list.Spacing(6);
        panel.Children().Append(list);

        StackPanel add_row;
        add_row.Orientation(Orientation::Horizontal);
        add_row.Spacing(8);
        TextBox email_box;
        email_box.Width(320);
        email_box.PlaceholderText(Tr(L"Adresse électronique du délégué"));
        add_row.Children().Append(email_box);
        Button add_button;
        add_button.Content(box_value(Tr(L"Ajouter un délégué")));
        add_button.IsEnabled(false);
        add_row.Children().Append(add_button);
        panel.Children().Append(add_row);

        dialog.Content(panel);

        auto refresh = std::make_shared<std::function<void()>>();
        auto show_error = [status](hstring const& error)
        {
            status.Text(error);
            status.Foreground(Application::Current().Resources().Lookup(box_value(L"SystemFillColorCriticalBrush")).as<Media::Brush>());
        };

        *refresh = [this, strong, account, status, list, add_button, show_error, refresh, secondary_brush]()
        {
            m_core->ListDelegates(account, [this, strong, account, status, list, add_button, show_error, refresh, secondary_brush]
                (hstring const& json, hstring const& error)
            {
                if (!error.empty())
                {
                    show_error(error);
                    return;
                }
                JsonObject data;
                if (!JsonObject::TryParse(json, data))
                    return;

                list.Children().Clear();
                auto delegates = data.GetNamedArray(L"delegates", JsonArray{});
                status.Foreground(secondary_brush);
                status.Text(delegates.Size() == 0
                    ? Tr(L"Aucun délégué n'a accès à cette boîte.")
                    : Fmt(Tr(L"{0} délégué(s) ont accès à cette boîte."), { to_hstring(delegates.Size()) }));
                add_button.IsEnabled(true);

                for (auto const& value : delegates)
                {
                    auto delegate_obj = value.GetObject();
                    hstring email = delegate_obj.GetNamedString(L"email", L"");

                    Grid row;
                    row.ColumnSpacing(12);
                    ColumnDefinition c0;
                    c0.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                    ColumnDefinition c1;
                    c1.Width(GridLengthHelper::Auto());
                    ColumnDefinition c2;
                    c2.Width(GridLengthHelper::Auto());
                    row.ColumnDefinitions().Append(c0);
                    row.ColumnDefinitions().Append(c1);
                    row.ColumnDefinitions().Append(c2);

                    TextBlock address;
                    address.Text(email);
                    address.VerticalAlignment(VerticalAlignment::Center);
                    Grid::SetColumn(address, 0);
                    row.Children().Append(address);

                    TextBlock state;
                    state.Text(StatusText(delegate_obj.GetNamedString(L"status", L"accepted")));
                    state.Foreground(secondary_brush);
                    state.VerticalAlignment(VerticalAlignment::Center);
                    Grid::SetColumn(state, 1);
                    row.Children().Append(state);

                    /* révocation : confirmation dans une bulle rattachée au bouton */
                    Button revoke;
                    revoke.Content(box_value(Tr(L"Révoquer")));
                    Grid::SetColumn(revoke, 2);
                    Flyout confirm;
                    StackPanel confirm_panel;
                    confirm_panel.Spacing(8);
                    TextBlock question;
                    question.TextWrapping(TextWrapping::Wrap);
                    question.MaxWidth(280);
                    question.Text(Fmt(Tr(L"Retirer l'accès de {0} à votre boîte ?"), { email }));
                    confirm_panel.Children().Append(question);
                    Button confirm_button;
                    confirm_button.Content(box_value(Tr(L"Confirmer la révocation")));
                    confirm_button.Click([this, strong, account, email, confirm, status, refresh, show_error]
                        (IInspectable const&, RoutedEventArgs const&)
                    {
                        confirm.Hide();
                        status.Text(Tr(L"Révocation en cours…"));
                        m_core->RemoveDelegate(account, email, [strong, refresh, show_error](hstring const&, hstring const& remove_error)
                        {
                            if (!remove_error.empty())
                                show_error(remove_error);
                            else
                                (*refresh)();
                        });
                    });
                    confirm_panel.Children().Append(confirm_button);
                    confirm.Content(confirm_panel);
                    revoke.Flyout(confirm);
                    row.Children().Append(revoke);

                    list.Children().Append(row);
                }
            });
        };

        email_box.TextChanged([add_button, status, email_box](IInspectable const&, TextChangedEventArgs const&)
        {
            std::wstring text{ email_box.Text() };
            add_button.IsEnabled(text.find(L'@') != std::wstring::npos && !status.Text().empty());
        });

        add_button.Click([this, strong, account, email_box, status, refresh, show_error, add_button]
            (IInspectable const&, RoutedEventArgs const&)
        {
            hstring email = email_box.Text();
            add_button.IsEnabled(false);
            status.Text(Tr(L"Ajout du délégué en cours…"));
            m_core->AddDelegate(account, email, [strong, refresh, show_error, email_box, add_button](hstring const&, hstring const& add_error)
            {
                add_button.IsEnabled(true);
                if (!add_error.empty())
                {
                    show_error(add_error);
                    return;
                }
                email_box.Text(L"");
                (*refresh)();
            });
        });

        (*refresh)();
        co_await dialog.ShowAsync();
        m_dialog_open = false;
    }
}
