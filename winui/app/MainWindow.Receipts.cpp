#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "Receipts.h"

/* Accusés de lecture : bandeau « Envoyer / Ne pas envoyer » à l'ouverture d'un
 * message qui en demande un, réglage par défaut, affichage des accusés reçus. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::EvolutionWin::implementation
{
    /** @brief Clé de réglage : "ask" (par défaut), "never" ou "always". */
    static constexpr wchar_t const* ReadReceiptPolicyKey = L"mail.read-receipt-policy";

    /**
     * @brief Indique si la langue d'affichage de l'application est l'anglais.
     * @return true pour une langue « en* », false sinon (français par défaut).
     */
    static bool UiIsEnglish()
    {
        try
        {
            auto languages = Windows::Globalization::ApplicationLanguages::Languages();
            if (languages.Size() > 0)
            {
                hstring tag_text = languages.GetAt(0);
                std::wstring_view tag{ tag_text };
                return tag.size() >= 2 && (tag[0] == L'e' || tag[0] == L'E') && (tag[1] == L'n' || tag[1] == L'N');
            }
        }
        catch (...) {}
        return false;
    }

    /**
     * @brief Choisit le texte de la langue d'affichage.
     * @param fr Texte français.
     * @param en Texte anglais.
     * @return Le texte dans la langue courante.
     */
    static hstring Tr(wchar_t const* fr, wchar_t const* en)
    {
        return hstring{ UiIsEnglish() ? en : fr };
    }

    /** @brief Réglage enregistré par l'utilisateur pour les accusés de lecture. */
    static ::EvolutionWin::Receipts::Policy CurrentReceiptPolicy()
    {
        hstring value = ::EvolutionWin::Settings::Instance().GetString(ReadReceiptPolicyKey, L"ask");
        return ::EvolutionWin::Receipts::ParsePolicy(std::wstring_view{ value });
    }

    /** @brief Referme les deux bandeaux d'accusé de lecture. */
    void MainWindow::HideReceiptBars()
    {
        ReceiptBar().IsOpen(false);
        MdnBar().IsOpen(false);
    }

    /**
     * @brief Met à jour les bandeaux d'accusé de lecture pour le message affiché.
     *
     * - Message portant Disposition-Notification-To / Return-Receipt-To et non encore
     *   traité : bandeau de demande, ou envoi direct si le réglage est « toujours ».
     * - Message qui est lui-même un accusé (MDN) : bandeau « Accusé de lecture : lu par X le … ».
     * @param message Objet JSON renvoyé par evo_core_get_message.
     */
    void MainWindow::UpdateReceiptBars(JsonObject const& message)
    {
        using namespace ::EvolutionWin::Receipts;

        HideReceiptBars();

        /* accusé reçu */
        if (message.HasKey(L"mdn"))
        {
            hstring report = message.GetNamedObject(L"mdn", JsonObject{}).GetNamedString(L"report", L"");
            Mdn mdn = ParseDispositionNotification(std::wstring_view{ report });
            if (mdn.valid)
            {
                hstring date = ::EvolutionWin::Helpers::FormatDate(message.GetNamedNumber(L"date", 0));
                MdnBar().Message(hstring{ DescribeMdn(mdn, std::wstring_view{ date }, UiIsEnglish()) });
                MdnBar().IsOpen(true);
            }
        }

        /* accusé demandé */
        hstring receipt_to = message.GetNamedString(L"receipt_to", L"");
        hstring from = message.GetNamedString(L"from", L"");
        Action action = Decide(CurrentReceiptPolicy(), std::wstring_view{ receipt_to },
            std::wstring_view{ from }, message.GetNamedBoolean(L"mdn_sent", false));

        if (action == Action::SendAuto)
        {
            ProcessReadReceipt(true, true);
        }
        else if (action == Action::Ask)
        {
            std::wstring who = AddressOf(std::wstring_view{ receipt_to });
            bool suspicious = IsReceiptAddressSuspicious(std::wstring_view{ receipt_to }, std::wstring_view{ from });

            ReceiptBar().Title(Tr(L"Accusé de lecture demandé", L"Read receipt requested"));
            std::wstring text = std::wstring{ Tr(L"L'expéditeur demande à être prévenu de la lecture de ce message (réponse envoyée à ",
                L"The sender asks to be notified when this message is read (reply sent to ") } + who + L").";
            if (suspicious)
                text += std::wstring{ Tr(L" Attention : cette adresse n'est pas celle de l'expéditeur.",
                    L" Warning: this address is not the sender's address.") };
            ReceiptBar().Message(hstring{ text });
            ReceiptBar().Severity(suspicious ? InfoBarSeverity::Warning : InfoBarSeverity::Informational);
            ReceiptSendButton().Content(box_value(Tr(L"Envoyer", L"Send")));
            ReceiptIgnoreButton().Content(box_value(Tr(L"Ne pas envoyer", L"Don't send")));
            ReceiptSendButton().IsEnabled(true);
            ReceiptIgnoreButton().IsEnabled(true);
            ReceiptBar().IsOpen(true);
        }
    }

    /**
     * @brief Envoie l'accusé de lecture du message affiché, ou le marque comme traité.
     * @param send true pour envoyer, false pour « Ne pas envoyer ».
     * @param automatic true si l'envoi est déclenché par le réglage « toujours envoyer ».
     */
    void MainWindow::ProcessReadReceipt(bool send, bool automatic)
    {
        if (!m_current_message || m_current_message_account.empty() || m_current_message_folder.empty())
            return;

        hstring uid = m_current_message_uid;
        ReceiptSendButton().IsEnabled(false);
        ReceiptIgnoreButton().IsEnabled(false);

        m_core->SendReadReceipt(m_current_message_account, m_current_message_folder, uid, send,
            [this, uid, send, automatic](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                /* non traité : le bandeau reste proposé pour une nouvelle tentative */
                ReceiptSendButton().IsEnabled(true);
                ReceiptIgnoreButton().IsEnabled(true);
                ShowStatus(Tr(L"Accusé de lecture non envoyé : ", L"Read receipt not sent: ") + error,
                    InfoBarSeverity::Warning);
                return;
            }

            if (m_current_message && m_current_message_uid == uid)
            {
                m_current_message.SetNamedValue(L"mdn_sent", JsonValue::CreateBooleanValue(true));
                ReceiptBar().IsOpen(false);
            }
            if (send)
                ShowStatus(automatic ? Tr(L"Accusé de lecture envoyé automatiquement.", L"Read receipt sent automatically.")
                                     : Tr(L"Accusé de lecture envoyé.", L"Read receipt sent."),
                    InfoBarSeverity::Success);
        });
    }

    /** @brief Bouton « Envoyer » du bandeau d'accusé de lecture. */
    void MainWindow::OnReceiptSendClick(IInspectable const&, RoutedEventArgs const&)
    {
        ProcessReadReceipt(true, false);
    }

    /** @brief Bouton « Ne pas envoyer » : le message est marqué traité, l'accusé n'est plus proposé. */
    void MainWindow::OnReceiptIgnoreClick(IInspectable const&, RoutedEventArgs const&)
    {
        ProcessReadReceipt(false, false);
    }

    /**
     * @brief Affiche le réglage par défaut des accusés de lecture dans les Paramètres (Confidentialité).
     *
     * Les textes (français ou anglais) sont posés ici ; appelé par RefreshPrivacyView.
     */
    void MainWindow::RefreshReadReceiptSettings()
    {
        ReadReceiptPolicyTitle().Text(Tr(L"Accusés de lecture", L"Read receipts"));
        ReadReceiptPolicyHint().Text(Tr(
            L"Quand un expéditeur demande à être prévenu de la lecture d'un message. « Toujours envoyer » demande tout de même confirmation si l'adresse de l'accusé n'est pas celle de l'expéditeur.",
            L"When a sender asks to be notified that a message was read. \"Always send\" still asks first if the receipt address is not the sender's."));

        auto items = ReadReceiptPolicyChoice().Items();
        items.SetAt(0, box_value(Tr(L"Toujours demander", L"Always ask")));
        items.SetAt(1, box_value(Tr(L"Ne jamais envoyer", L"Never send")));
        items.SetAt(2, box_value(Tr(L"Toujours envoyer", L"Always send")));

        auto policy = CurrentReceiptPolicy();
        m_updating_privacy_view = true;
        ReadReceiptPolicyChoice().SelectedIndex(policy == ::EvolutionWin::Receipts::Policy::Never ? 1
            : policy == ::EvolutionWin::Receipts::Policy::Always ? 2 : 0);
        m_updating_privacy_view = false;
    }

    /** @brief Changement du réglage par défaut des accusés de lecture. */
    void MainWindow::OnReadReceiptPolicyChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        using ::EvolutionWin::Receipts::Policy;
        int32_t index = ReadReceiptPolicyChoice().SelectedIndex();
        if (m_updating_privacy_view || index < 0)
            return;

        Policy policy = index == 1 ? Policy::Never : index == 2 ? Policy::Always : Policy::Ask;
        ::EvolutionWin::Settings::Instance().SetString(ReadReceiptPolicyKey,
            hstring{ ::EvolutionWin::Receipts::PolicyToString(policy) });
    }
}
