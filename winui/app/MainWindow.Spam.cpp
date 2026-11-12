#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/* Filtre anti-spam local (classificateur bayésien du moteur, evo-core-spam.c) :
 * réglages de Paramètres > Courrier > Courrier indésirable, apprentissage à
 * partir des actions « Indésirable » / « Non indésirable » et des dossiers,
 * remise à zéro, et notification des messages classés automatiquement.
 *
 * Réglages : spam.enabled (booléen, activé par défaut), spam.threshold
 * (pourcentage de 50 à 99, 90 par défaut). */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace
{
    /** @brief Seuil de classement par défaut (pourcentage). */
    constexpr int kSpamDefaultThreshold = 90;

    /** @brief Nombre maximal de messages lus par dossier lors de l'apprentissage en lot. */
    constexpr int kSpamLearnMaxMessages = 300;

    /**
     * @brief Cherche, dans l'arborescence d'un compte, le premier dossier sélectionnable d'un type donné.
     * @param folders Arborescence JSON des dossiers du compte.
     * @param type Type de dossier recherché (« junk », « inbox »).
     * @return Chemin complet du dossier, ou chaîne vide s'il n'existe pas.
     */
    hstring FindSpamFolderByType(JsonArray const& folders, hstring const& type)
    {
        for (auto const& value : folders)
        {
            auto folder = value.GetObject();
            if (folder.GetNamedString(L"type", L"") == type && folder.GetNamedBoolean(L"selectable", true))
                return folder.GetNamedString(L"full_name");
            if (auto found = FindSpamFolderByType(folder.GetNamedArray(L"children", JsonArray{}), type); !found.empty())
                return found;
        }
        return {};
    }

    /**
     * @brief Lit un entier d'un résultat JSON du moteur (0 si le JSON est invalide ou le champ absent).
     * @param json Texte JSON.
     * @param name Nom du champ numérique.
     * @return Valeur du champ.
     */
    int SpamJsonInt(hstring const& json, wchar_t const* name)
    {
        JsonObject object{ nullptr };
        if (!json.empty() && JsonObject::TryParse(json, object))
            return static_cast<int>(object.GetNamedNumber(name, 0));
        return 0;
    }
}

namespace winrt::EvolutionWin::implementation
{
    /**
     * @brief Transmet au moteur les réglages du filtre anti-spam (activation et seuil).
     *        Appelée au démarrage du moteur et à chaque changement de réglage.
     */
    void MainWindow::ApplySpamSettings()
    {
        if (!m_core)
            return;

        auto& settings = ::EvolutionWin::Settings::Instance();
        int threshold = settings.GetInt(L"spam.threshold", kSpamDefaultThreshold);
        threshold = (std::min)(99, (std::max)(50, threshold));
        m_core->SpamConfigure(L"", settings.GetBoolean(L"spam.enabled", true), threshold);
    }

    /**
     * @brief Met à jour le texte d'état de l'apprentissage (nombre de messages appris, filtre opérationnel ou non).
     */
    void MainWindow::RefreshSpamStats()
    {
        if (!m_core)
            return;

        m_core->SpamStats([this](hstring const& json, hstring const& error)
        {
            auto text = SpamStatsText();
            if (!text)
                return;

            if (!error.empty() || json.empty())
            {
                text.Text(L"");
                return;
            }

            hstring spam = to_hstring(SpamJsonInt(json, L"spam"));
            hstring ham = to_hstring(SpamJsonInt(json, L"ham"));
            JsonObject object{ nullptr };
            bool ready = JsonObject::TryParse(json, object) && object.GetNamedBoolean(L"ready", false);
            if (ready)
                text.Text(Fmt(Tr(L"Appris : {0} indésirable(s), {1} légitime(s). Le filtre est opérationnel."), { spam, ham }));
            else
                text.Text(Fmt(Tr(L"Appris : {0} indésirable(s), {1} légitime(s). Le filtre n'est pas encore opérationnel : il lui faut au moins {2} messages de chaque sorte."),
                    { spam, ham, to_hstring(SpamJsonInt(json, L"min_trained")) }));
        });
    }

    /**
     * @brief Initialise les contrôles de la section Courrier indésirable des Paramètres à partir des réglages.
     *        Appelée chaque fois que l'on ouvre les Paramètres (relit aussi l'état de l'apprentissage).
     */
    void MainWindow::InitSpamSettingsView()
    {
        auto& settings = ::EvolutionWin::Settings::Instance();
        int threshold = (std::min)(99, (std::max)(50, settings.GetInt(L"spam.threshold", kSpamDefaultThreshold)));

        bool previous = m_updating_settings_view;
        m_updating_settings_view = true;
        if (auto toggle = SpamEnabledSwitch())
            toggle.IsOn(settings.GetBoolean(L"spam.enabled", true));
        if (auto slider = SpamThresholdSlider())
            slider.Value(threshold);
        if (auto label = SpamThresholdText())
            label.Text(to_hstring(threshold) + L" %");
        m_updating_settings_view = previous;

        RefreshSpamStats();
    }

    /**
     * @brief Apprend des messages (indésirables ou légitimes) puis exécute la suite (en général leur déplacement).
     *        Si le filtre est désactivé, la suite s'exécute immédiatement sans apprentissage.
     *        L'apprentissage doit précéder le déplacement : il lit les messages dans leur dossier d'origine.
     * @param uids Identifiants des messages.
     * @param account Compte des messages.
     * @param folder Dossier d'origine des messages.
     * @param is_spam true pour apprendre comme indésirables, false comme légitimes.
     * @param then Suite à exécuter, que l'apprentissage réussisse ou non.
     */
    void MainWindow::TrainSpamThenMove(std::vector<hstring> const& uids, hstring const& account, hstring const& folder,
        bool is_spam, std::function<void()> const& then)
    {
        if (!m_core || uids.empty() || !::EvolutionWin::Settings::Instance().GetBoolean(L"spam.enabled", true))
        {
            then();
            return;
        }

        m_core->SpamTrain(account, folder, uids, is_spam, [this, then](hstring const&, hstring const&)
        {
            then();
            RefreshSpamStats();
        });
    }

    /**
     * @brief Active ou désactive le classement automatique des nouveaux messages.
     */
    void MainWindow::OnSpamEnabledToggled(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (!m_initialized || m_updating_settings_view)
            return;

        if (auto toggle = sender.try_as<ToggleSwitch>())
        {
            ::EvolutionWin::Settings::Instance().SetBoolean(L"spam.enabled", toggle.IsOn());
            ApplySpamSettings();
        }
    }

    /**
     * @brief Change le seuil de classement en indésirable (pourcentage).
     */
    void MainWindow::OnSpamThresholdChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
    {
        int threshold = static_cast<int>(args.NewValue() + 0.5);
        if (auto label = SpamThresholdText())
            label.Text(to_hstring(threshold) + L" %");

        if (!m_initialized || m_updating_settings_view)
            return;

        ::EvolutionWin::Settings::Instance().SetNumber(L"spam.threshold", threshold);
        ApplySpamSettings();
    }

    /**
     * @brief Apprend en lot : les messages des dossiers Indésirables comme indésirables et ceux des
     *        Boîtes de réception comme légitimes (les plus récents de chaque dossier, tous comptes).
     */
    void MainWindow::OnSpamLearnClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_core || !m_accounts)
            return;

        struct Job { hstring account; hstring folder; bool is_spam; };
        std::vector<Job> jobs;
        for (auto const& value : m_accounts)
        {
            hstring uid = value.GetObject().GetNamedString(L"uid", L"");
            auto it = m_account_folders.find(std::wstring{ uid });
            if (uid.empty() || it == m_account_folders.end() || !it->second)
                continue;
            if (auto junk = FindSpamFolderByType(it->second, L"junk"); !junk.empty())
                jobs.push_back({ uid, junk, true });
            if (auto inbox = FindSpamFolderByType(it->second, L"inbox"); !inbox.empty())
                jobs.push_back({ uid, inbox, false });
        }

        if (jobs.empty())
        {
            ShowStatus(Tr(L"Aucun dossier Indésirables ou Boîte de réception à analyser."), InfoBarSeverity::Warning);
            return;
        }

        if (auto button = SpamLearnButton())
            button.IsEnabled(false);
        ShowStatus(Tr(L"Apprentissage du filtre anti-spam en cours…"), InfoBarSeverity::Informational);

        auto pending = std::make_shared<size_t>(jobs.size());
        auto learned = std::make_shared<int>(0);
        for (auto const& job : jobs)
        {
            m_core->SpamTrainFolder(job.account, job.folder, job.is_spam, kSpamLearnMaxMessages,
                [this, pending, learned](hstring const& json, hstring const&)
                {
                    *learned += SpamJsonInt(json, L"trained");
                    if (--(*pending) > 0)
                        return;
                    if (auto button = SpamLearnButton())
                        button.IsEnabled(true);
                    ShowStatus(Fmt(Tr(L"Apprentissage terminé : {0} message(s) appris."), { to_hstring(*learned) }),
                        InfoBarSeverity::Success);
                    RefreshSpamStats();
                });
        }
    }

    /**
     * @brief Réinitialise l'apprentissage du filtre après confirmation.
     */
    void MainWindow::OnSpamResetClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_core)
            return;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(Tr(L"Réinitialiser l'apprentissage ?")));
        dialog.Content(box_value(Tr(L"Tout ce que le filtre anti-spam a appris sera effacé. Il ne classera plus rien automatiquement avant d'avoir de nouveau appris sur vos messages.")));
        dialog.PrimaryButtonText(Tr(L"Réinitialiser"));
        dialog.CloseButtonText(Tr(L"Annuler"));
        dialog.DefaultButton(ContentDialogButton::Close);
        dialog.ShowAsync().Completed([this](IAsyncOperation<ContentDialogResult> const& operation, AsyncStatus)
        {
            if (operation.GetResults() != ContentDialogResult::Primary || !m_core)
                return;

            m_core->SpamReset([this](hstring const&, hstring const& error)
            {
                if (!error.empty())
                {
                    ShowStatus(Tr(L"Réinitialisation de l'apprentissage : ") + error, InfoBarSeverity::Error);
                    return;
                }
                ShowStatus(Tr(L"L'apprentissage du filtre anti-spam a été réinitialisé."), InfoBarSeverity::Success);
                RefreshSpamStats();
            });
        });
    }

    /**
     * @brief Informe que des messages ont été classés automatiquement dans le dossier Indésirables.
     * @param object Événement « spam-filtered » du moteur (compte, dossier, nombre de messages).
     */
    void MainWindow::ShowSpamFilteredNotice(JsonObject const& object)
    {
        int count = static_cast<int>(object.GetNamedNumber(L"count", 0));
        if (count <= 0)
            return;

        ShowStatus(Fmt(Tr(L"{0} message(s) classé(s) automatiquement dans Indésirables."), { to_hstring(count) }),
            InfoBarSeverity::Informational);
    }
}
