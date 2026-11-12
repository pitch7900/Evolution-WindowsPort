#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "CategoryStore.h"
#include "RuleStore.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/* Règles de messagerie : éditeur (Paramètres > Courrier), éditeur de règle rapide (clic droit sur un
 * message), exécution à la réception et sur un dossier, envoi des règles au serveur (Microsoft Graph
 * pour Microsoft 365, ManageSieve pour IMAP). La logique pure est dans Rules.h. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace
{
    namespace R = ::EvolutionWin::Rules;
    using ::EvolutionWin::RuleStore;

    /** @brief Destination possible d'une action de classement. */
    struct FolderTarget
    {
        std::wstring account;   ///< Compte
        std::wstring folder;    ///< Chemin complet du dossier
        hstring label;          ///< Texte affiché
    };

    /**
     * @brief Ajoute à la liste les dossiers sélectionnables d'un arbre, en profondeur.
     * @param folders Tableau JSON de dossiers (avec « children »).
     * @param account Compte propriétaire.
     * @param account_name Nom affiché du compte.
     * @param out Liste complétée.
     */
    void FlattenFolders(JsonArray const& folders, std::wstring const& account, hstring const& account_name, std::vector<FolderTarget>& out)
    {
        for (auto const& value : folders)
        {
            auto folder = value.GetObject();
            hstring full = folder.GetNamedString(L"full_name", L"");
            if (!full.empty() && folder.GetNamedBoolean(L"selectable", true))
                out.push_back(FolderTarget{ account, std::wstring{ full }, account_name + L" › " + full });
            FlattenFolders(folder.GetNamedArray(L"children", JsonArray{}), account, account_name, out);
        }
    }

    /**
     * @brief Comptes proposés pour les règles (non locaux).
     * @param accounts Comptes du moteur.
     * @return Paires (identifiant, nom, protocole).
     */
    std::vector<std::tuple<std::wstring, hstring, hstring>> RuleAccounts(JsonArray const& accounts)
    {
        std::vector<std::tuple<std::wstring, hstring, hstring>> list;
        if (!accounts)
            return list;
        for (auto const& value : accounts)
        {
            auto account = value.GetObject();
            hstring uid = account.GetNamedString(L"uid", L"");
            if (uid.empty() || account.GetNamedBoolean(L"local", false))
                continue;
            list.emplace_back(std::wstring{ uid }, account.GetNamedString(L"name", uid), account.GetNamedString(L"protocol", L""));
        }
        return list;
    }

    /** @brief Libellé d'un critère. */
    hstring FieldLabel(R::Field field)
    {
        switch (field)
        {
        case R::Field::Sender: return Tr(L"L'expéditeur");
        case R::Field::SenderDomain: return Tr(L"Le domaine de l'expéditeur");
        case R::Field::Subject: return Tr(L"L'objet");
        case R::Field::Recipients: return Tr(L"Les destinataires (À ou Cc)");
        case R::Field::Header: return Tr(L"L'en-tête");
        case R::Field::HasAttachment: return Tr(L"Le message a une pièce jointe");
        }
        return L"";
    }

    /** @brief Libellé d'une comparaison. */
    hstring OpLabel(R::Op op)
    {
        switch (op)
        {
        case R::Op::Contains: return Tr(L"contient");
        case R::Op::NotContains: return Tr(L"ne contient pas");
        case R::Op::Equals: return Tr(L"est égal à");
        case R::Op::StartsWith: return Tr(L"commence par");
        case R::Op::EndsWith: return Tr(L"se termine par");
        }
        return L"";
    }

    /** @brief Libellé d'un type d'action. */
    hstring ActionLabel(R::ActionType type)
    {
        switch (type)
        {
        case R::ActionType::MoveToFolder: return Tr(L"Déplacer vers un dossier");
        case R::ActionType::CopyToFolder: return Tr(L"Copier vers un dossier");
        case R::ActionType::MarkRead: return Tr(L"Marquer comme lu");
        case R::ActionType::MarkFlagged: return Tr(L"Poser un drapeau");
        case R::ActionType::AssignCategory: return Tr(L"Affecter une catégorie");
        case R::ActionType::Delete: return Tr(L"Supprimer");
        }
        return L"";
    }

    /**
     * @brief Résumé d'une règle sur une ligne : critères puis actions.
     * @param rule Règle décrite.
     * @return Texte localisé.
     */
    hstring DescribeRule(R::Rule const& rule)
    {
        std::wstring text;
        for (size_t i = 0; i < rule.conditions.size(); ++i)
        {
            auto const& c = rule.conditions[i];
            if (i)
                text += rule.match_all ? std::wstring{ Tr(L" ET ") } : std::wstring{ Tr(L" OU ") };
            if (c.field == R::Field::HasAttachment)
                text += std::wstring{ FieldLabel(c.field) };
            else if (c.field == R::Field::Header)
                text += std::wstring{ Fmt(Tr(L"{0} {1} {2} « {3} »"), { FieldLabel(c.field), hstring{ c.header }, OpLabel(c.op), hstring{ c.text } }) };
            else
                text += std::wstring{ Fmt(Tr(L"{0} {1} « {2} »"), { FieldLabel(c.field), OpLabel(c.op), hstring{ c.text } }) };
        }
        text += L" → ";
        for (size_t i = 0; i < rule.actions.size(); ++i)
        {
            auto const& a = rule.actions[i];
            if (i)
                text += L", ";
            if (a.type == R::ActionType::MoveToFolder || a.type == R::ActionType::CopyToFolder)
                text += std::wstring{ Fmt(Tr(L"{0} : {1}"), { ActionLabel(a.type), hstring{ a.folder } }) };
            else if (a.type == R::ActionType::AssignCategory)
                text += std::wstring{ Fmt(Tr(L"{0} : {1}"), { ActionLabel(a.type), hstring{ a.category } }) };
            else
                text += std::wstring{ ActionLabel(a.type) };
        }
        return hstring{ text };
    }

    /** @brief Étape asynchrone d'un traitement : appelle @c next une fois terminée. */
    using Step = std::function<void(std::function<void()>)>;

    /**
     * @brief Exécute des étapes l'une après l'autre (chaque action du moteur attend la précédente).
     * @param steps Étapes.
     * @param index Étape à lancer.
     * @param done Appelé après la dernière étape.
     */
    void RunSteps(std::shared_ptr<std::vector<Step>> steps, size_t index, std::function<void()> done)
    {
        if (index >= steps->size())
        {
            if (done)
                done();
            return;
        }
        (*steps)[index]([steps, index, done]() { RunSteps(steps, index + 1, done); });
    }
}

namespace winrt::EvolutionWin::implementation
{
    /**
     * @brief Enregistre les règles après modification.
     */
    void MainWindow::OnRulesChanged()
    {
        ::EvolutionWin::RuleStore::Save();
    }

    /**
     * @brief Applique à un message la décision des règles (copies, indicateurs, catégories, puis déplacement ou suppression).
     *
     * Les actions s'enchaînent dans l'ordre pour que le déplacement final ne précède pas les indicateurs.
     *
     * @param account Compte du message.
     * @param folder Dossier du message.
     * @param uid Identifiant du message.
     * @param outcome Décision calculée par R::Evaluate().
     * @param done Appelé à la fin de toutes les actions.
     */
    static void ExecuteOutcome(std::shared_ptr<::EvolutionWin::CoreBridge> core, hstring const& account, hstring const& folder, hstring const& uid,
                               R::Outcome const& outcome, std::function<void()> done)
    {
        auto steps = std::make_shared<std::vector<Step>>();
        auto flag = [&](hstring const& name, bool value)
        {
            steps->push_back([core, account, folder, uid, name, value](std::function<void()> next)
            {
                core->SetMessageFlags(account, folder, uid, name, value, [next](hstring const&, hstring const&) { next(); });
            });
        };
        if (outcome.mark_read)
            flag(L"seen", true);
        if (outcome.mark_flagged)
            flag(L"flagged", true);
        for (auto const& category : outcome.categories)
            flag(hstring{ category }, true);
        for (auto const& copy : outcome.copies)
        {
            hstring dst_account = copy.account.empty() ? account : hstring{ copy.account };
            hstring dst_folder{ copy.folder };
            steps->push_back([core, account, folder, uid, dst_account, dst_folder](std::function<void()> next)
            {
                core->CopyMessage(account, folder, uid, dst_account, dst_folder, [next](hstring const&, hstring const&) { next(); });
            });
        }
        if (outcome.has_move)
        {
            hstring dst_account = outcome.move.account.empty() ? account : hstring{ outcome.move.account };
            hstring dst_folder{ outcome.move.folder };
            steps->push_back([core, account, folder, uid, dst_account, dst_folder](std::function<void()> next)
            {
                core->MoveMessage(account, folder, uid, dst_account, dst_folder, [next](hstring const&, hstring const&) { next(); });
            });
        }
        else if (outcome.remove)
        {
            steps->push_back([core, account, folder, uid](std::function<void()> next)
            {
                core->DeleteMessage(account, folder, uid, [next](hstring const&, hstring const&) { next(); });
            });
        }
        RunSteps(steps, 0, std::move(done));
    }

    /**
     * @brief Passe des messages dans les règles locales et exécute les décisions.
     *
     * À la réception, seuls les messages non lus, pas encore traités et datés après la création des règles
     * sont concernés ; « Exécuter maintenant » traite tous les messages donnés. Le Cc et les en-têtes ne sont
     * lus (sans marquer le message comme lu) que si une règle applicable en a besoin.
     *
     * @param account Compte du dossier.
     * @param folder Dossier.
     * @param messages Messages au format JSON du moteur.
     * @param at_reception true pour le traitement automatique, false pour une exécution manuelle.
     */
    void MainWindow::ApplyRulesToMessages(hstring account, hstring folder, JsonArray messages, bool at_reception)
    {
        namespace Store = ::EvolutionWin;
        std::wstring account_key{ account };
        auto rules = std::make_shared<std::vector<R::Rule>>();
        bool needs_details = false;
        for (auto const& rule : Store::RuleStore::Rules())
        {
            if (!rule.enabled || rule.server || !R::AppliesToAccount(rule, account_key))
                continue;
            rules->push_back(rule);
            needs_details = needs_details || R::NeedsDetails(rule);
        }
        if (rules->empty() || !messages)
        {
            if (!at_reception)
                ShowStatus(Tr(L"Aucune règle active ne s'applique à ce compte."), InfoBarSeverity::Warning);
            return;
        }

        struct Progress
        {
            int pending{ 0 };
            int matched{ 0 };
            bool listed{ false };
        };
        auto progress = std::make_shared<Progress>();
        auto core = m_core;
        auto finish_all = [this, progress, account, folder, at_reception]()
        {
            if (!progress->listed || progress->pending > 0)
                return;
            progress->listed = false;
            if (progress->matched > 0)
            {
                if (account == m_account && folder == m_folder)
                    ScheduleListRefresh();
                if (!at_reception)
                    ShowStatus(Fmt(Tr(L"Règles exécutées : {0} message(s) traité(s)."), { to_hstring(progress->matched) }), InfoBarSeverity::Success);
            }
            else if (!at_reception)
                ShowStatus(Tr(L"Règles exécutées : aucun message ne correspond."), InfoBarSeverity::Informational);
        };

        bool processed_changed = false;
        for (auto const& value : messages)
        {
            auto object = value.GetObject();
            hstring uid = object.GetNamedString(L"uid", L"");
            if (uid.empty())
                continue;
            if (at_reception)
            {
                if (object.GetNamedBoolean(L"seen", false))
                    continue;
                auto key = R::ProcessedSet::Key(account_key, std::wstring{ folder }, std::wstring{ uid });
                if (!Store::RuleStore::Processed().Add(key))
                    continue;
                processed_changed = true;
            }

            R::MessageData data;
            data.from = std::wstring{ object.GetNamedString(L"from", L"") };
            data.to = std::wstring{ object.GetNamedString(L"to", L"") };
            data.subject = std::wstring{ object.GetNamedString(L"subject", L"") };
            data.has_attachment = object.GetNamedBoolean(L"attachments", false);
            int64_t date = static_cast<int64_t>(object.GetNamedNumber(L"date", 0));

            auto decide = [core, rules, progress, finish_all, account, folder, uid, account_key, date, at_reception](R::MessageData const& message)
            {
                R::Outcome outcome = R::Evaluate(*rules, message, account_key, date, at_reception);
                if (!outcome.matched)
                {
                    --progress->pending;
                    finish_all();
                    return;
                }
                ++progress->matched;
                ExecuteOutcome(core, account, folder, uid, outcome, [progress, finish_all]()
                {
                    --progress->pending;
                    finish_all();
                });
            };

            ++progress->pending;
            if (!needs_details)
            {
                decide(data);
                continue;
            }
            m_core->GetMessageHeaders(account, folder, uid, [data, decide](hstring const& json, hstring const&) mutable
            {
                JsonObject details;
                if (!json.empty() && JsonObject::TryParse(json, details))
                {
                    data.cc = std::wstring{ details.GetNamedString(L"cc", L"") };
                    data.details_known = true;
                    if (details.HasKey(L"headers"))
                    {
                        auto headers = details.GetNamedObject(L"headers");
                        for (auto const& pair : headers)
                            data.headers[::EvolutionWin::Categories::FoldName(std::wstring_view{ pair.Key() })] = std::wstring{ pair.Value().GetString() };
                    }
                }
                decide(data);
            });
        }
        if (processed_changed)
            Store::RuleStore::SaveProcessed();
        progress->listed = true;
        finish_all();
    }

    /**
     * @brief Traitement automatique à l'arrivée de nouveaux messages : relit le dossier depuis le cache
     *        local et passe les messages non traités dans les règles.
     * @param account Compte.
     * @param folder Dossier (boîte de réception).
     */
    void MainWindow::ApplyRulesToNewMail(hstring account, hstring folder)
    {
        bool any = false;
        for (auto const& rule : ::EvolutionWin::RuleStore::Rules())
            any = any || (rule.enabled && !rule.server && R::AppliesToAccount(rule, std::wstring{ account }));
        if (!any || account.empty() || folder.empty())
            return;
        m_core->ReloadMessages(account, folder, 200, [this, account, folder](hstring const& json, hstring const& error)
        {
            JsonArray messages;
            if (error.empty() && JsonArray::TryParse(json, messages))
                ApplyRulesToMessages(account, folder, messages, true);
        });
    }

    /**
     * @brief « Exécuter les règles maintenant » : passe tous les messages d'un dossier dans les règles locales.
     * @param account Compte.
     * @param folder Dossier.
     */
    void MainWindow::RunRulesOnFolder(hstring account, hstring folder)
    {
        ShowStatus(Tr(L"Exécution des règles en cours…"), InfoBarSeverity::Informational);
        m_core->ListMessages(account, folder, 0, [this, account, folder](hstring const& json, hstring const& error)
        {
            JsonArray messages;
            if (!error.empty() || !JsonArray::TryParse(json, messages))
            {
                ShowStatus(Fmt(Tr(L"Impossible de lire le dossier : {0}"), { error }), InfoBarSeverity::Error);
                return;
            }
            ApplyRulesToMessages(account, folder, messages, false);
        });
    }

    /**
     * @brief Ajoute « Exécuter les règles sur ce dossier » à un menu contextuel de dossier.
     * @param menu Menu à compléter.
     * @param account Compte du dossier.
     * @param folder Chemin complet du dossier.
     */
    void MainWindow::AppendRuleFolderItem(MenuFlyout const& menu, hstring const& account, hstring const& folder)
    {
        MenuFlyoutItem item;
        item.Text(Tr(L"Exécuter les règles sur ce dossier"));
        FontIcon icon;
        icon.Glyph(L"\xE768");
        item.Icon(icon);
        item.Click([this, account, folder](IInspectable const&, RoutedEventArgs const&) { RunRulesOnFolder(account, folder); });
        menu.Items().Append(item);
    }

    /**
     * @brief Envoie les règles « serveur » d'un compte : Microsoft Graph (Microsoft 365) ou script Sieve (IMAP).
     * @param account Compte visé.
     */
    fire_and_forget MainWindow::SyncServerRulesFor(hstring account)
    {
        auto strong = get_strong();
        std::wstring account_key{ account };
        hstring protocol;
        for (auto const& entry : RuleAccounts(m_accounts))
            if (std::get<0>(entry) == account_key)
                protocol = std::get<2>(entry);

        auto const& rules = ::EvolutionWin::RuleStore::Rules();
        if (protocol == L"microsoft365")
        {
            auto plan = R::PlanGraphSync(rules, account_key);
            hstring skipped_note;
            if (!plan.skipped.empty())
                skipped_note = Fmt(Tr(L"{0} règle(s) non convertible(s) ignorée(s) : {1}"), { to_hstring(static_cast<int32_t>(plan.skipped.size())), hstring{ plan.skipped.front() } });
            ShowStatus(Tr(L"Envoi des règles à Exchange Online…"), InfoBarSeverity::Informational);
            m_core->SyncServerRules(account, hstring{ R::GraphRulesArray(plan) }, [this, skipped_note](hstring const& json, hstring const& error)
            {
                if (!error.empty())
                {
                    ShowStatus(Fmt(Tr(L"Échec de l'envoi des règles : {0}"), { error }), InfoBarSeverity::Error);
                    return;
                }
                JsonObject result;
                JsonObject::TryParse(json, result);
                hstring text = Fmt(Tr(L"Règles envoyées à Exchange Online : {0} créée(s), {1} remplacée(s)."),
                    { to_hstring(static_cast<int32_t>(result ? result.GetNamedNumber(L"created", 0) : 0)),
                      to_hstring(static_cast<int32_t>(result ? result.GetNamedNumber(L"removed", 0) : 0)) });
                if (!skipped_note.empty())
                    text = text + L" " + skipped_note;
                ShowStatus(text, skipped_note.empty() ? InfoBarSeverity::Success : InfoBarSeverity::Warning);
            });
            co_return;
        }
        if (protocol != L"imapx")
        {
            ShowStatus(Tr(L"Les règles côté serveur ne sont disponibles que pour Microsoft 365 / Exchange Online et les serveurs IMAP compatibles Sieve."), InfoBarSeverity::Warning);
            co_return;
        }

        auto script = R::ToSieve(rules, account_key);
        if (script.text.empty())
        {
            ShowStatus(Tr(L"Aucune règle « sur le serveur » convertible en Sieve pour ce compte."), InfoBarSeverity::Warning);
            co_return;
        }
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;
        m_dialog_open = true;
        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(Tr(L"Envoyer les règles au serveur IMAP")));
        dialog.PrimaryButtonText(Tr(L"Envoyer et activer"));
        dialog.CloseButtonText(Tr(L"Annuler"));
        TextBlock warning;
        warning.TextWrapping(TextWrapping::Wrap);
        warning.Text(Tr(L"Le script « evolution » sera envoyé au serveur (ManageSieve, port 4190, connexion chiffrée) et deviendra le script actif : il remplace le script Sieve actuellement actif sur le serveur."));
        dialog.Content(warning);
        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;
        if (result != ContentDialogResult::Primary)
            co_return;

        ShowStatus(Tr(L"Envoi du script Sieve…"), InfoBarSeverity::Informational);
        hstring note = script.skipped.empty() ? hstring{} : Fmt(Tr(L"{0} règle(s) non convertible(s) ignorée(s) : {1}"), { to_hstring(static_cast<int32_t>(script.skipped.size())), hstring{ script.skipped.front() } });
        m_core->ManageSievePut(account, hstring{ R::SieveScriptName }, hstring{ script.text }, true, L"", [this, note](hstring const&, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(Fmt(Tr(L"Échec de l'envoi du script Sieve : {0}"), { error }), InfoBarSeverity::Error);
                return;
            }
            hstring text = Tr(L"Script Sieve envoyé et activé sur le serveur.");
            if (!note.empty())
                text = text + L" " + note;
            ShowStatus(text, note.empty() ? InfoBarSeverity::Success : InfoBarSeverity::Warning);
        });
    }

    /**
     * @brief Crée une liste déroulante des dossiers de destination, sélection sur (compte, dossier).
     * @param targets Dossiers proposés.
     * @param account Compte de l'action (peut être vide).
     * @param folder Dossier de l'action (peut être vide : aucune sélection).
     * @return Liste déroulante prête à l'emploi.
     */
    static ComboBox MakeFolderCombo(std::vector<FolderTarget> const& targets, std::wstring const& account, std::wstring const& folder)
    {
        ComboBox box;
        box.HorizontalAlignment(HorizontalAlignment::Stretch);
        box.PlaceholderText(Tr(L"Choisir un dossier"));
        int selected = -1;
        for (size_t i = 0; i < targets.size(); ++i)
        {
            box.Items().Append(box_value(targets[i].label));
            if (targets[i].folder == folder && (account.empty() || targets[i].account == account))
                selected = static_cast<int>(i);
        }
        box.SelectedIndex(selected);
        return box;
    }

    /**
     * @brief Crée une liste déroulante des catégories du registre, sélection sur le nom donné.
     * @param current Catégorie actuelle (peut être vide).
     * @return Liste déroulante.
     */
    static ComboBox MakeCategoryCombo(std::wstring const& current)
    {
        ComboBox box;
        box.HorizontalAlignment(HorizontalAlignment::Stretch);
        box.PlaceholderText(Tr(L"Choisir une catégorie"));
        int selected = -1;
        int index = 0;
        for (auto const& item : ::EvolutionWin::CategoryStore::Registry().Items())
        {
            box.Items().Append(box_value(hstring{ item.name }));
            if (::EvolutionWin::Categories::NamesEqual(item.name, current))
                selected = index;
            ++index;
        }
        box.SelectedIndex(selected);
        return box;
    }

    /**
     * @brief Vérifie qu'une règle est complète et cohérente.
     * @param rule Règle à contrôler.
     * @return Message d'erreur localisé, vide si la règle est valide.
     */
    static hstring ValidateRule(R::Rule const& rule)
    {
        if (::EvolutionWin::Categories::TrimName(rule.name).empty())
            return Tr(L"Donnez un nom à la règle.");
        if (rule.conditions.empty())
            return Tr(L"Ajoutez au moins un critère.");
        for (auto const& c : rule.conditions)
        {
            if (c.field == R::Field::HasAttachment)
                continue;
            if (::EvolutionWin::Categories::TrimName(c.text).empty())
                return Tr(L"Chaque critère doit comporter un texte.");
            if (c.field == R::Field::Header && ::EvolutionWin::Categories::TrimName(c.header).empty())
                return Tr(L"Indiquez le nom de l'en-tête (par exemple List-Id).");
        }
        if (rule.actions.empty())
            return Tr(L"Ajoutez au moins une action.");
        for (auto const& a : rule.actions)
        {
            if ((a.type == R::ActionType::MoveToFolder || a.type == R::ActionType::CopyToFolder) && a.folder.empty())
                return Tr(L"Choisissez le dossier de destination.");
            if (a.type == R::ActionType::AssignCategory && ::EvolutionWin::Categories::TrimName(a.category).empty())
                return Tr(L"Choisissez la catégorie à affecter.");
            if (rule.server && (a.type == R::ActionType::MoveToFolder || a.type == R::ActionType::CopyToFolder) && a.account != rule.account)
                return Tr(L"Une règle exécutée sur le serveur ne peut classer que dans les dossiers de son compte.");
        }
        if (rule.server && rule.account.empty())
            return Tr(L"Une règle exécutée sur le serveur doit viser un seul compte.");
        return L"";
    }

    /**
     * @brief Éditeur complet d'une règle : nom, comptes, lieu d'exécution, critères multiples, actions.
     * @param rule Règle éditée (copie de travail).
     * @param is_new true pour une création (identifiant et date de départ attribués à l'enregistrement).
     */
    fire_and_forget MainWindow::EditRuleAsync(R::Rule rule, bool is_new)
    {
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;
        m_dialog_open = true;
        auto strong = get_strong();

        auto work = std::make_shared<R::Rule>(std::move(rule));
        if (work->conditions.empty())
            work->conditions.push_back(R::Condition{});
        if (work->actions.empty())
            work->actions.push_back(R::Action{});

        auto accounts = RuleAccounts(m_accounts);
        auto targets = std::make_shared<std::vector<FolderTarget>>();
        for (auto const& entry : accounts)
        {
            auto found = m_account_folders.find(std::get<0>(entry));
            if (found != m_account_folders.end() && found->second)
                FlattenFolders(found->second, std::get<0>(entry), std::get<1>(entry), *targets);
        }

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(is_new ? Tr(L"Nouvelle règle") : Tr(L"Modifier la règle")));
        dialog.PrimaryButtonText(Tr(L"Enregistrer"));
        dialog.CloseButtonText(Tr(L"Annuler"));
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(12);
        panel.Width(640);

        TextBox name_box;
        name_box.Header(box_value(Tr(L"Nom de la règle")));
        name_box.Text(hstring{ work->name });
        name_box.MaxLength(120);
        panel.Children().Append(name_box);

        ComboBox scope_box;
        scope_box.Header(box_value(Tr(L"Comptes concernés")));
        scope_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        scope_box.Items().Append(box_value(Tr(L"Tous les comptes")));
        int scope_index = 0;
        for (size_t i = 0; i < accounts.size(); ++i)
        {
            scope_box.Items().Append(box_value(std::get<1>(accounts[i])));
            if (std::get<0>(accounts[i]) == work->account)
                scope_index = static_cast<int>(i) + 1;
        }
        scope_box.SelectedIndex(scope_index);
        panel.Children().Append(scope_box);

        ComboBox where_box;
        where_box.Header(box_value(Tr(L"Où exécuter la règle")));
        where_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        where_box.Items().Append(box_value(Tr(L"Sur cet ordinateur, à la réception des messages")));
        where_box.Items().Append(box_value(Tr(L"Sur le serveur (synchronisée, agit aussi application fermée)")));
        where_box.SelectedIndex(work->server ? 1 : 0);
        panel.Children().Append(where_box);

        ComboBox match_box;
        match_box.Header(box_value(Tr(L"Appliquer la règle quand")));
        match_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        match_box.Items().Append(box_value(Tr(L"Tous les critères sont vrais")));
        match_box.Items().Append(box_value(Tr(L"Au moins un critère est vrai")));
        match_box.SelectedIndex(work->match_all ? 0 : 1);
        panel.Children().Append(match_box);

        TextBlock conditions_title;
        conditions_title.Text(Tr(L"Critères"));
        conditions_title.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
        panel.Children().Append(conditions_title);
        StackPanel conditions_host;
        conditions_host.Spacing(6);
        panel.Children().Append(conditions_host);
        Button add_condition;
        add_condition.Content(box_value(Tr(L"Ajouter un critère")));
        panel.Children().Append(add_condition);

        TextBlock actions_title;
        actions_title.Text(Tr(L"Actions"));
        actions_title.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
        panel.Children().Append(actions_title);
        StackPanel actions_host;
        actions_host.Spacing(6);
        panel.Children().Append(actions_host);
        Button add_action;
        add_action.Content(box_value(Tr(L"Ajouter une action")));
        panel.Children().Append(add_action);

        CheckBox stop_box;
        stop_box.Content(box_value(Tr(L"Ne plus appliquer les règles suivantes après celle-ci")));
        stop_box.IsChecked(work->stop);
        panel.Children().Append(stop_box);

        TextBlock error_text;
        error_text.TextWrapping(TextWrapping::Wrap);
        error_text.Foreground(Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::Color{ 255, 196, 43, 28 } });
        error_text.Visibility(Visibility::Collapsed);
        panel.Children().Append(error_text);

        /* reconstruction des lignes de critères et d'actions à partir de la copie de travail */
        auto rebuild_conditions = std::make_shared<std::function<void()>>();
        auto rebuild_actions = std::make_shared<std::function<void()>>();
        auto* rebuild_c = rebuild_conditions.get();
        auto* rebuild_a = rebuild_actions.get();
        auto dispatcher = DispatcherQueue();

        *rebuild_conditions = [work, conditions_host, dispatcher, rebuild_c]()
        {
            conditions_host.Children().Clear();
            for (size_t i = 0; i < work->conditions.size(); ++i)
            {
                auto const condition = work->conditions[i];
                bool is_attachment = condition.field == R::Field::HasAttachment;

                Grid row;
                row.ColumnSpacing(6);
                for (int c = 0; c < 5; ++c)
                {
                    ColumnDefinition column;
                    column.Width(c == 3 ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star) : GridLengthHelper::Auto());
                    row.ColumnDefinitions().Append(column);
                }

                ComboBox field_box;
                field_box.Width(200);
                for (int f = 0; f < R::FieldCount; ++f)
                    field_box.Items().Append(box_value(FieldLabel(static_cast<R::Field>(f))));
                field_box.SelectedIndex(static_cast<int>(condition.field));
                field_box.SelectionChanged([work, i, dispatcher, rebuild_c](IInspectable const& sender, SelectionChangedEventArgs const&)
                {
                    int selected = sender.as<ComboBox>().SelectedIndex();
                    if (i < work->conditions.size() && selected >= 0 && selected != static_cast<int>(work->conditions[i].field))
                    {
                        work->conditions[i].field = static_cast<R::Field>(selected);
                        dispatcher.TryEnqueue([rebuild_c]() { (*rebuild_c)(); });
                    }
                });
                row.Children().Append(field_box);

                if (!is_attachment)
                {
                    ComboBox op_box;
                    op_box.Width(150);
                    for (int o = 0; o < R::OpCount; ++o)
                        op_box.Items().Append(box_value(OpLabel(static_cast<R::Op>(o))));
                    op_box.SelectedIndex(static_cast<int>(condition.op));
                    op_box.SelectionChanged([work, i](IInspectable const& sender, SelectionChangedEventArgs const&)
                    {
                        int selected = sender.as<ComboBox>().SelectedIndex();
                        if (i < work->conditions.size() && selected >= 0)
                            work->conditions[i].op = static_cast<R::Op>(selected);
                    });
                    Grid::SetColumn(op_box, 1);
                    row.Children().Append(op_box);

                    if (condition.field == R::Field::Header)
                    {
                        TextBox header_box;
                        header_box.Width(120);
                        header_box.PlaceholderText(Tr(L"Nom (List-Id…)"));
                        header_box.Text(hstring{ condition.header });
                        header_box.TextChanged([work, i](IInspectable const& sender, TextChangedEventArgs const&)
                        {
                            if (i < work->conditions.size())
                                work->conditions[i].header = std::wstring{ sender.as<TextBox>().Text() };
                        });
                        Grid::SetColumn(header_box, 2);
                        row.Children().Append(header_box);
                    }

                    TextBox text_box;
                    text_box.PlaceholderText(Tr(L"Texte à rechercher"));
                    text_box.Text(hstring{ condition.text });
                    text_box.MaxLength(300);
                    text_box.TextChanged([work, i](IInspectable const& sender, TextChangedEventArgs const&)
                    {
                        if (i < work->conditions.size())
                            work->conditions[i].text = std::wstring{ sender.as<TextBox>().Text() };
                    });
                    Grid::SetColumn(text_box, 3);
                    row.Children().Append(text_box);
                }

                Button remove_button;
                FontIcon remove_icon;
                remove_icon.Glyph(L"\xE74D");
                remove_icon.FontSize(14);
                remove_button.Content(remove_icon);
                remove_button.IsEnabled(work->conditions.size() > 1);
                Controls::ToolTipService::SetToolTip(remove_button, box_value(Tr(L"Retirer ce critère")));
                remove_button.Click([work, i, dispatcher, rebuild_c](IInspectable const&, RoutedEventArgs const&)
                {
                    if (i < work->conditions.size())
                        work->conditions.erase(work->conditions.begin() + i);
                    dispatcher.TryEnqueue([rebuild_c]() { (*rebuild_c)(); });
                });
                Grid::SetColumn(remove_button, 4);
                row.Children().Append(remove_button);

                conditions_host.Children().Append(row);
            }
        };

        *rebuild_actions = [work, actions_host, targets, dispatcher, rebuild_a]()
        {
            actions_host.Children().Clear();
            for (size_t i = 0; i < work->actions.size(); ++i)
            {
                auto const action = work->actions[i];

                Grid row;
                row.ColumnSpacing(6);
                for (int c = 0; c < 3; ++c)
                {
                    ColumnDefinition column;
                    column.Width(c == 1 ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star) : GridLengthHelper::Auto());
                    row.ColumnDefinitions().Append(column);
                }

                ComboBox type_box;
                type_box.Width(220);
                for (int t = 0; t < R::ActionTypeCount; ++t)
                    type_box.Items().Append(box_value(ActionLabel(static_cast<R::ActionType>(t))));
                type_box.SelectedIndex(static_cast<int>(action.type));
                type_box.SelectionChanged([work, i, dispatcher, rebuild_a](IInspectable const& sender, SelectionChangedEventArgs const&)
                {
                    int selected = sender.as<ComboBox>().SelectedIndex();
                    if (i < work->actions.size() && selected >= 0 && selected != static_cast<int>(work->actions[i].type))
                    {
                        work->actions[i].type = static_cast<R::ActionType>(selected);
                        dispatcher.TryEnqueue([rebuild_a]() { (*rebuild_a)(); });
                    }
                });
                row.Children().Append(type_box);

                if (action.type == R::ActionType::MoveToFolder || action.type == R::ActionType::CopyToFolder)
                {
                    auto folder_box = MakeFolderCombo(*targets, action.account, action.folder);
                    folder_box.SelectionChanged([work, i, targets](IInspectable const& sender, SelectionChangedEventArgs const&)
                    {
                        int selected = sender.as<ComboBox>().SelectedIndex();
                        if (i < work->actions.size() && selected >= 0 && static_cast<size_t>(selected) < targets->size())
                        {
                            work->actions[i].account = (*targets)[selected].account;
                            work->actions[i].folder = (*targets)[selected].folder;
                        }
                    });
                    Grid::SetColumn(folder_box, 1);
                    row.Children().Append(folder_box);
                }
                else if (action.type == R::ActionType::AssignCategory)
                {
                    auto category_box = MakeCategoryCombo(action.category);
                    category_box.SelectionChanged([work, i](IInspectable const& sender, SelectionChangedEventArgs const&)
                    {
                        auto selected = sender.as<ComboBox>().SelectedItem();
                        if (i < work->actions.size() && selected)
                            work->actions[i].category = std::wstring{ unbox_value<hstring>(selected) };
                    });
                    Grid::SetColumn(category_box, 1);
                    row.Children().Append(category_box);
                }

                Button remove_button;
                FontIcon remove_icon;
                remove_icon.Glyph(L"\xE74D");
                remove_icon.FontSize(14);
                remove_button.Content(remove_icon);
                remove_button.IsEnabled(work->actions.size() > 1);
                Controls::ToolTipService::SetToolTip(remove_button, box_value(Tr(L"Retirer cette action")));
                remove_button.Click([work, i, dispatcher, rebuild_a](IInspectable const&, RoutedEventArgs const&)
                {
                    if (i < work->actions.size())
                        work->actions.erase(work->actions.begin() + i);
                    dispatcher.TryEnqueue([rebuild_a]() { (*rebuild_a)(); });
                });
                Grid::SetColumn(remove_button, 2);
                row.Children().Append(remove_button);

                actions_host.Children().Append(row);
            }
        };

        add_condition.Click([work, rebuild_c](IInspectable const&, RoutedEventArgs const&)
        {
            work->conditions.push_back(R::Condition{});
            (*rebuild_c)();
        });
        add_action.Click([work, rebuild_a](IInspectable const&, RoutedEventArgs const&)
        {
            work->actions.push_back(R::Action{});
            (*rebuild_a)();
        });
        (*rebuild_conditions)();
        (*rebuild_actions)();

        /* reprise des champs simples dans la copie de travail, puis contrôle */
        auto accounts_copy = std::make_shared<decltype(accounts)>(accounts);
        dialog.PrimaryButtonClick([=](ContentDialog const&, ContentDialogButtonClickEventArgs const& args)
        {
            work->name = ::EvolutionWin::Categories::TrimName(std::wstring_view{ name_box.Text() });
            int scope = scope_box.SelectedIndex();
            work->account = scope > 0 && static_cast<size_t>(scope) <= accounts_copy->size() ? std::get<0>((*accounts_copy)[scope - 1]) : std::wstring{};
            work->server = where_box.SelectedIndex() == 1;
            work->match_all = match_box.SelectedIndex() != 1;
            work->stop = stop_box.IsChecked().Value();
            hstring problem = ValidateRule(*work);
            if (!problem.empty())
            {
                error_text.Text(problem);
                error_text.Visibility(Visibility::Visible);
                args.Cancel(true);
            }
        });

        ScrollViewer scroller;
        scroller.MaxHeight(560);
        scroller.Content(panel);
        dialog.Content(scroller);
        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;
        if (result != ContentDialogResult::Primary)
            co_return;

        auto& all = ::EvolutionWin::RuleStore::Rules();
        if (is_new || work->id.empty())
        {
            work->id = ::EvolutionWin::RuleStore::NewId();
            work->since = ::EvolutionWin::RuleStore::NowUnix();
            all.push_back(*work);
        }
        else
        {
            auto found = std::find_if(all.begin(), all.end(), [&](R::Rule const& r) { return r.id == work->id; });
            if (found != all.end())
                *found = *work;
            else
                all.push_back(*work);
        }
        OnRulesChanged();
        RefreshRulesView();
    }

    /**
     * @brief Éditeur de règle rapide (clic droit sur un message > Créer une règle) : critères préremplis
     *        avec l'expéditeur et l'objet du message, une action au choix, et accès à l'éditeur complet.
     * @param uid Identifiant du message d'origine.
     */
    fire_and_forget MainWindow::ShowQuickRuleDialogAsync(hstring uid)
    {
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        hstring from, subject;
        if (m_all_messages)
        {
            for (auto const& value : m_all_messages)
            {
                auto object = value.GetObject();
                if (object.GetNamedString(L"uid", L"") == uid)
                {
                    from = object.GetNamedString(L"from", L"");
                    subject = object.GetNamedString(L"subject", L"");
                    break;
                }
            }
        }
        auto location = GetMessageLocation(uid);
        hstring account = location.account.empty() ? m_account : location.account;
        hstring folder = location.folder.empty() ? m_folder : location.folder;

        m_dialog_open = true;
        auto strong = get_strong();

        R::Rule seed = R::MakeQuickRule(std::wstring_view{ from }, std::wstring_view{ subject }, std::wstring_view{ account });
        std::wstring account_key{ account };
        std::wstring sender_text, subject_text;
        for (auto const& c : seed.conditions)
            (c.field == R::Field::Sender ? sender_text : subject_text) = c.text;

        auto targets = std::make_shared<std::vector<FolderTarget>>();
        auto found = m_account_folders.find(account_key);
        if (found != m_account_folders.end() && found->second)
        {
            hstring account_name = account;
            for (auto const& entry : RuleAccounts(m_accounts))
                if (std::get<0>(entry) == account_key)
                    account_name = std::get<1>(entry);
            FlattenFolders(found->second, account_key, account_name, *targets);
        }

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(Tr(L"Créer une règle")));
        dialog.PrimaryButtonText(Tr(L"Créer la règle"));
        dialog.SecondaryButtonText(Tr(L"Options avancées…"));
        dialog.CloseButtonText(Tr(L"Annuler"));
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);
        panel.Width(480);

        TextBlock intro;
        intro.Text(Tr(L"Quand un message arrive dans ce compte…"));
        panel.Children().Append(intro);

        CheckBox sender_check;
        sender_check.Content(box_value(Tr(L"de l'expéditeur")));
        sender_check.IsChecked(!sender_text.empty());
        sender_check.IsEnabled(!sender_text.empty());
        TextBox sender_box;
        sender_box.Text(hstring{ sender_text });
        panel.Children().Append(sender_check);
        panel.Children().Append(sender_box);

        CheckBox subject_check;
        subject_check.Content(box_value(Tr(L"dont l'objet contient")));
        subject_check.IsChecked(sender_text.empty() && !subject_text.empty());
        subject_check.IsEnabled(!subject_text.empty());
        TextBox subject_box;
        subject_box.Text(hstring{ subject_text });
        panel.Children().Append(subject_check);
        panel.Children().Append(subject_box);

        ComboBox action_box;
        action_box.Header(box_value(Tr(L"Faire ceci")));
        action_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        for (int t = 0; t < R::ActionTypeCount; ++t)
            action_box.Items().Append(box_value(ActionLabel(static_cast<R::ActionType>(t))));
        action_box.SelectedIndex(0);
        panel.Children().Append(action_box);

        auto folder_box = MakeFolderCombo(*targets, account_key, L"");
        panel.Children().Append(folder_box);
        auto category_box = MakeCategoryCombo(L"");
        category_box.Visibility(Visibility::Collapsed);
        panel.Children().Append(category_box);
        action_box.SelectionChanged([=](IInspectable const&, SelectionChangedEventArgs const&)
        {
            int type = action_box.SelectedIndex();
            folder_box.Visibility(type == 0 || type == 1 ? Visibility::Visible : Visibility::Collapsed);
            category_box.Visibility(type == 4 ? Visibility::Visible : Visibility::Collapsed);
        });

        CheckBox apply_now;
        apply_now.Content(box_value(Tr(L"Appliquer aussi maintenant aux messages de ce dossier")));
        panel.Children().Append(apply_now);

        TextBlock error_text;
        error_text.TextWrapping(TextWrapping::Wrap);
        error_text.Foreground(Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::Color{ 255, 196, 43, 28 } });
        error_text.Visibility(Visibility::Collapsed);
        panel.Children().Append(error_text);

        /* règle construite d'après les champs de la fenêtre */
        auto collect = [=]() -> R::Rule
        {
            R::Rule rule;
            rule.account = account_key;
            rule.name = seed.name;
            if (sender_check.IsChecked().Value())
            {
                R::Condition c;
                c.field = R::Field::Sender;
                c.text = std::wstring{ sender_box.Text() };
                rule.conditions.push_back(c);
            }
            if (subject_check.IsChecked().Value())
            {
                R::Condition c;
                c.field = R::Field::Subject;
                c.text = std::wstring{ subject_box.Text() };
                rule.conditions.push_back(c);
            }
            R::Action action;
            action.type = static_cast<R::ActionType>((std::max)(0, action_box.SelectedIndex()));
            int target = folder_box.SelectedIndex();
            if ((action.type == R::ActionType::MoveToFolder || action.type == R::ActionType::CopyToFolder) && target >= 0 && static_cast<size_t>(target) < targets->size())
            {
                action.account = (*targets)[target].account;
                action.folder = (*targets)[target].folder;
            }
            if (action.type == R::ActionType::AssignCategory && category_box.SelectedItem())
                action.category = std::wstring{ unbox_value<hstring>(category_box.SelectedItem()) };
            rule.actions.push_back(action);
            return rule;
        };

        dialog.PrimaryButtonClick([=](ContentDialog const&, ContentDialogButtonClickEventArgs const& args)
        {
            hstring problem = ValidateRule(collect());
            if (!problem.empty())
            {
                error_text.Text(problem);
                error_text.Visibility(Visibility::Visible);
                args.Cancel(true);
            }
        });

        dialog.Content(panel);
        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result == ContentDialogResult::Secondary)
        {
            EditRuleAsync(collect(), true);
            co_return;
        }
        if (result != ContentDialogResult::Primary)
            co_return;

        R::Rule rule = collect();
        rule.id = ::EvolutionWin::RuleStore::NewId();
        rule.since = ::EvolutionWin::RuleStore::NowUnix();
        ::EvolutionWin::RuleStore::Rules().push_back(rule);
        OnRulesChanged();
        ShowStatus(Fmt(Tr(L"Règle « {0} » créée."), { hstring{ rule.name } }), InfoBarSeverity::Success);
        if (apply_now.IsChecked().Value())
            RunRulesOnFolder(account, folder);
    }

    /**
     * @brief Entrée « Créer une règle… » du menu contextuel d'un message.
     * @param uid Identifiant du message sous le curseur.
     * @return Élément de menu prêt à être ajouté.
     */
    MenuFlyoutItem MainWindow::MakeCreateRuleMenuItem(hstring uid)
    {
        MenuFlyoutItem item;
        item.Text(Tr(L"Créer une règle…"));
        FontIcon icon;
        icon.Glyph(L"\xE713");
        item.Icon(icon);
        item.Click([this, uid](IInspectable const&, RoutedEventArgs const&) { ShowQuickRuleDialogAsync(uid); });
        return item;
    }

    /**
     * @brief Construit l'éditeur de règles dans Paramètres > Courrier : liste des règles, ajout,
     *        exécution manuelle et envoi au serveur.
     */
    void MainWindow::RefreshRulesView()
    {
        auto host = RulesHost();
        if (!host)
            return;
        host.Children().Clear();

        auto resources = Application::Current().Resources();
        auto card_brush = ::EvolutionWin::Helpers::ThemedBrush(L"CardBackgroundFillColorDefaultBrush");
        auto stroke_brush = ::EvolutionWin::Helpers::ThemedBrush(L"CardStrokeColorDefaultBrush");
        auto secondary_brush = ::EvolutionWin::Helpers::ThemedBrush(L"TextFillColorSecondaryBrush");
        auto caption_style = resources.Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>();

        auto& rules = ::EvolutionWin::RuleStore::Rules();
        if (rules.empty())
        {
            TextBlock empty;
            empty.Text(Tr(L"Aucune règle. Créez-en une ici, ou par un clic droit sur un message > Créer une règle."));
            empty.Style(caption_style);
            empty.Foreground(secondary_brush);
            host.Children().Append(empty);
        }

        auto icon_button = [](wchar_t const* glyph, hstring const& tip)
        {
            Button button;
            FontIcon icon;
            icon.Glyph(glyph);
            icon.FontSize(14);
            button.Content(icon);
            Controls::ToolTipService::SetToolTip(button, box_value(tip));
            return button;
        };

        auto accounts = RuleAccounts(m_accounts);
        for (size_t index = 0; index < rules.size(); ++index)
        {
            auto const rule = rules[index];

            Border card;
            card.Background(card_brush);
            card.BorderBrush(stroke_brush);
            card.BorderThickness(ThicknessHelper::FromUniformLength(1));
            card.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
            card.Padding(ThicknessHelper::FromLengths(12, 8, 12, 8));

            Grid row;
            row.ColumnSpacing(8);
            for (int i = 0; i < 5; ++i)
            {
                ColumnDefinition column;
                column.Width(i == 1 ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star) : GridLengthHelper::Auto());
                row.ColumnDefinitions().Append(column);
            }

            CheckBox enabled;
            enabled.IsChecked(rule.enabled);
            enabled.MinWidth(0);
            enabled.VerticalAlignment(VerticalAlignment::Center);
            Controls::ToolTipService::SetToolTip(enabled, box_value(Tr(L"Règle active")));
            enabled.Click([this, index](IInspectable const& sender, RoutedEventArgs const&)
            {
                auto& all = ::EvolutionWin::RuleStore::Rules();
                if (index < all.size())
                    all[index].enabled = sender.as<CheckBox>().IsChecked().Value();
                OnRulesChanged();
            });
            row.Children().Append(enabled);

            StackPanel texts;
            texts.Spacing(2);
            TextBlock title;
            title.Text(hstring{ rule.name });
            title.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
            texts.Children().Append(title);
            TextBlock summary;
            summary.Text(DescribeRule(rule));
            summary.TextWrapping(TextWrapping::Wrap);
            summary.Style(caption_style);
            summary.Foreground(secondary_brush);
            texts.Children().Append(summary);
            hstring scope_name = Tr(L"Tous les comptes");
            for (auto const& entry : accounts)
                if (std::get<0>(entry) == rule.account)
                    scope_name = std::get<1>(entry);
            TextBlock where;
            where.Text(rule.server ? Fmt(Tr(L"Sur le serveur · {0}"), { scope_name }) : Fmt(Tr(L"Sur cet ordinateur · {0}"), { scope_name }));
            where.Style(caption_style);
            where.Foreground(secondary_brush);
            texts.Children().Append(where);
            Grid::SetColumn(texts, 1);
            row.Children().Append(texts);

            auto up = icon_button(L"\xE70E", Tr(L"Donner plus de priorité"));
            up.IsEnabled(index > 0);
            up.VerticalAlignment(VerticalAlignment::Center);
            up.Click([this, index](IInspectable const&, RoutedEventArgs const&)
            {
                auto& all = ::EvolutionWin::RuleStore::Rules();
                if (index > 0 && index < all.size())
                {
                    std::swap(all[index], all[index - 1]);
                    OnRulesChanged();
                    RefreshRulesView();
                }
            });
            Grid::SetColumn(up, 2);
            row.Children().Append(up);

            auto edit = icon_button(L"\xE70F", Tr(L"Modifier la règle"));
            edit.VerticalAlignment(VerticalAlignment::Center);
            edit.Click([this, rule](IInspectable const&, RoutedEventArgs const&) { EditRuleAsync(rule, false); });
            Grid::SetColumn(edit, 3);
            row.Children().Append(edit);

            auto remove = icon_button(L"\xE74D", Tr(L"Supprimer la règle"));
            remove.VerticalAlignment(VerticalAlignment::Center);
            remove.Click([this, index](IInspectable const&, RoutedEventArgs const&)
            {
                auto& all = ::EvolutionWin::RuleStore::Rules();
                if (index < all.size())
                {
                    all.erase(all.begin() + index);
                    OnRulesChanged();
                    RefreshRulesView();
                }
            });
            Grid::SetColumn(remove, 4);
            row.Children().Append(remove);

            card.Child(row);
            host.Children().Append(card);
        }

        StackPanel buttons;
        buttons.Orientation(Orientation::Horizontal);
        buttons.Spacing(8);
        Button add_button;
        add_button.Content(box_value(Tr(L"Nouvelle règle")));
        add_button.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            R::Rule rule;
            for (auto const& entry : RuleAccounts(m_accounts))
                if (std::get<0>(entry) == std::wstring{ m_account })
                    rule.account = std::get<0>(entry);
            EditRuleAsync(rule, true);
        });
        buttons.Children().Append(add_button);

        Button run_button;
        run_button.Content(box_value(Tr(L"Exécuter les règles maintenant…")));
        Controls::ToolTipService::SetToolTip(run_button, box_value(Tr(L"Passe tous les messages d'un dossier dans les règles de cet ordinateur.")));
        run_button.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            FolderPickOptions options;
            options.title = Tr(L"Exécuter les règles sur un dossier");
            options.confirm_label = Tr(L"Exécuter");
            PickFolderAsync(options, [this](hstring const& account, hstring const& folder)
            {
                if (!account.empty() && !folder.empty())
                    RunRulesOnFolder(account, folder);
            });
        });
        buttons.Children().Append(run_button);
        host.Children().Append(buttons);

        /* envoi au serveur */
        TextBlock server_title;
        server_title.Text(Tr(L"Règles côté serveur"));
        server_title.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
        server_title.Margin(ThicknessHelper::FromLengths(0, 8, 0, 0));
        host.Children().Append(server_title);

        TextBlock server_note;
        server_note.Text(Tr(L"Les règles réglées « sur le serveur » sont envoyées à Exchange Online (Microsoft Graph : règles de la boîte de réception) ou à un serveur IMAP compatible Sieve (ManageSieve). Seules les règles créées par Evolution sont remplacées ; vos autres règles ne sont pas touchées."));
        server_note.TextWrapping(TextWrapping::Wrap);
        server_note.Style(caption_style);
        server_note.Foreground(secondary_brush);
        host.Children().Append(server_note);

        StackPanel server_row;
        server_row.Orientation(Orientation::Horizontal);
        server_row.Spacing(8);
        ComboBox account_box;
        account_box.Width(260);
        for (auto const& entry : accounts)
            account_box.Items().Append(box_value(std::get<1>(entry)));
        account_box.SelectedIndex(accounts.empty() ? -1 : 0);
        server_row.Children().Append(account_box);
        Button sync_button;
        sync_button.Content(box_value(Tr(L"Envoyer les règles au serveur")));
        sync_button.Click([this, account_box, accounts](IInspectable const&, RoutedEventArgs const&)
        {
            int selected = account_box.SelectedIndex();
            if (selected >= 0 && static_cast<size_t>(selected) < accounts.size())
                SyncServerRulesFor(hstring{ std::get<0>(accounts[selected]) });
        });
        server_row.Children().Append(sync_button);
        host.Children().Append(server_row);
    }
}
