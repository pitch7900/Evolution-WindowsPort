#pragma once

/*
 * Bridge between evo-core (C, callbacks on its own threads, UTF-8 JSON) and
 * the user interface: every callback is handed over to the UI thread
 * through its DispatcherQueue, with the JSON converted to an hstring.
 */

#include "evo-core.h"
#include "ErrorLog.h"
#include "UnifiedViews.h"

namespace EvolutionWin
{
    /* Identifiant de message transmis au moteur (UTF-8) : dans une vue unifiée
       l'identifiant affiché est composite, seul l'uid réel est connu du moteur */
    inline std::string CoreUid(winrt::hstring const& uid)
    {
        return UnifiedViews::RealUid(winrt::to_string(uid));
    }

    /* json is empty when error is set */
    using CoreHandler = std::function<void (winrt::hstring const& json, winrt::hstring const& error)>;
    using CoreEventHandler = std::function<void (winrt::hstring const& event, winrt::hstring const& json)>;

    class CoreBridge
    {
    public:
        CoreBridge(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& queue, CoreEventHandler on_event)
            : m_queue(queue), m_on_event(std::move(on_event))
        {
            m_core = evo_core_new(&CoreBridge::OnEvent, this);
        }

        ~CoreBridge()
        {
            /* no callback after this: pending UI work items check m_alive */
            *m_alive = false;
            evo_core_free(m_core);
        }

        CoreBridge(CoreBridge const&) = delete;
        CoreBridge& operator=(CoreBridge const&) = delete;

        void ListAccounts(CoreHandler handler)
        {
            evo_core_list_accounts(m_core, &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        void ListFolders(winrt::hstring const& account, CoreHandler handler)
        {
            evo_core_list_folders(m_core, winrt::to_string(account).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        void ListMessages(winrt::hstring const& account, winrt::hstring const& folder, unsigned max_count, CoreHandler handler)
        {
            evo_core_list_messages(m_core, winrt::to_string(account).c_str(), winrt::to_string(folder).c_str(),
                max_count, &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* not GetMessage: a Win32 macro */
        void FetchMessage(winrt::hstring const& account, winrt::hstring const& folder, winrt::hstring const& uid, CoreHandler handler)
        {
            evo_core_get_message(m_core, winrt::to_string(account).c_str(), winrt::to_string(folder).c_str(),
                CoreUid(uid).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        void Refresh()
        {
            evo_core_refresh(m_core);
        }

        /* Envoyer/Recevoir tous les dossiers de tous les comptes (F9) */
        void RefreshAll()
        {
            evo_core_refresh_all(m_core);
        }

        /* Active ou désactive le mode hors connexion global du moteur.
         * Paramètres :
         *   offline : true pour travailler hors connexion, false pour se reconnecter */
        void SetOffline(bool offline)
        {
            evo_core_set_offline(m_core, offline ? 1 : 0);
        }

        /* Indique si le moteur est actuellement en mode hors connexion.
         * Retourne : true si hors connexion, false si en ligne. */
        bool GetOffline() const
        {
            return evo_core_get_offline(m_core) != 0;
        }

        /* Relance la tentative de connexion / d'authentification pour un compte donné.
         * Paramètres :
         *   account : identifiant du compte à reconnecter */
        void ReconnectAccount(winrt::hstring const& account)
        {
            evo_core_reconnect_account(m_core, winrt::to_string(account).c_str());
        }

        void CancelOperations()
        {
            evo_core_cancel_operations(m_core);
        }

        /* Récupère les informations de quota d'un dossier / compte (ex. IMAP QUOTA).
         * Paramètres :
         *   account : identifiant du compte
         *   folder  : chemin complet du dossier (ex. "INBOX")
         *   handler : rappel recevant le JSON avec has_quota, used, total, percent */
        void GetFolderQuota(winrt::hstring const& account, winrt::hstring const& folder, CoreHandler handler)
        {
            evo_core_get_folder_quota(m_core, winrt::to_string(account).c_str(), winrt::to_string(folder).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Enregistre un message dans le dossier des brouillons (Drafts).
         * Paramètres :
         *   draft_json : chaîne JSON décrivant le brouillon
         *   handler    : rappel recevant le statut de sauvegarde */
        void SaveDraft(winrt::hstring const& draft_json, CoreHandler handler)
        {
            evo_core_save_draft(m_core, winrt::to_string(draft_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* not SendMessage: a Win32 macro */
        void SendMail(winrt::hstring const& message_json, CoreHandler handler)
        {
            evo_core_send_message(m_core, winrt::to_string(message_json).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Prépare la signature / le chiffrement S/MIME d'un message en rédaction : indique si un
         * certificat personnel existe (sinon il sera créé) et quels destinataires n'ont pas de certificat connu. */
        void CryptoCheck(winrt::hstring const& request_json, CoreHandler handler)
        {
            evo_core_crypto_check(m_core, winrt::to_string(request_json).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Traite et envoie tous les messages en attente dans la Boîte d'envoi. */
        void SendUnsentMessages()
        {
            evo_core_send_unsent_messages(m_core);
        }

        /* Modifie un indicateur ("seen" ou "flagged") sur un message.
         * Paramètres :
         *   account : identifiant du compte
         *   folder  : chemin complet du dossier
         *   uid     : identifiant du message
         *   flag    : nom de l'indicateur ("seen" ou "flagged")
         *   value   : true pour activer, false pour retirer
         *   handler : rappel sur le thread de l'interface */
        void SetMessageFlags(winrt::hstring const& account, winrt::hstring const& folder, winrt::hstring const& uid,
                             winrt::hstring const& flag, bool value, CoreHandler handler)
        {
            evo_core_set_message_flags(m_core, winrt::to_string(account).c_str(), winrt::to_string(folder).c_str(),
                CoreUid(uid).c_str(), winrt::to_string(flag).c_str(), value ? 1 : 0,
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Traite la demande d'accusé de lecture d'un message : envoie un MDN
         * (RFC 8098) ou marque seulement le message comme traité ($MDNSent).
         * Paramètres :
         *   send    : true pour envoyer l'accusé, false pour « Ne pas envoyer »
         *   handler : rappel recevant {"sent", "processed", "to"} */
        void SendReadReceipt(winrt::hstring const& account, winrt::hstring const& folder, winrt::hstring const& uid,
                             bool send, CoreHandler handler)
        {
            evo_core_send_read_receipt(m_core, winrt::to_string(account).c_str(), winrt::to_string(folder).c_str(),
                CoreUid(uid).c_str(), send ? 1 : 0, &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Répond à une invitation iCalendar d'un message : met à jour l'agenda,
         * envoie (ou non) la réponse iTIP à l'organisateur, mémorise la réponse.
         * Paramètres :
         *   response   : "accepted", "tentative", "declined" ou "remove" (réunion annulée)
         *   send_reply : true pour envoyer la réponse à l'organisateur
         *   handler    : rappel recevant {"response", "partstat", "calendar", "calendar_error"?, "sent", "to"} */
        void RespondInvitation(winrt::hstring const& account, winrt::hstring const& folder, winrt::hstring const& uid,
                               winrt::hstring const& response, bool send_reply, CoreHandler handler)
        {
            evo_core_respond_invitation(m_core, winrt::to_string(account).c_str(), winrt::to_string(folder).c_str(),
                CoreUid(uid).c_str(), winrt::to_string(response).c_str(), send_reply ? 1 : 0,
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Définit ou efface le suivi d'un message.
         * Paramètres :
         *   label        : libellé ("" pour effacer)
         *   due_by       : échéance en secondes Unix (texte) ou ""
         *   completed_on : date de fin en secondes Unix (texte) ou ""
         *   handler      : rappel recevant {"flagged", "follow_up", "due_by", "completed_on"} */
        void SetFollowUp(winrt::hstring const& account, winrt::hstring const& folder, winrt::hstring const& uid,
                         winrt::hstring const& label, winrt::hstring const& due_by, winrt::hstring const& completed_on,
                         CoreHandler handler)
        {
            evo_core_set_follow_up(m_core, winrt::to_string(account).c_str(), winrt::to_string(folder).c_str(),
                CoreUid(uid).c_str(), winrt::to_string(label).c_str(), winrt::to_string(due_by).c_str(),
                winrt::to_string(completed_on).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Lit les réponses automatiques (absence) d'un compte Microsoft 365 ou Gmail.
         * Paramètres :
         *   account : identifiant du compte
         *   handler : rappel recevant {"provider", "single_message", "enabled",
         *             "external_audience", "start", "end", "subject",
         *             "internal_message", "external_message"} */
        void GetAutoReply(winrt::hstring const& account, CoreHandler handler)
        {
            evo_core_get_auto_reply(m_core, winrt::to_string(account).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Enregistre les réponses automatiques (absence) d'un compte.
         * Paramètres :
         *   account       : identifiant du compte
         *   settings_json : réglages (mêmes champs que GetAutoReply)
         *   handler       : rappel recevant les réglages enregistrés */
        void SetAutoReply(winrt::hstring const& account, winrt::hstring const& settings_json, CoreHandler handler)
        {
            evo_core_set_auto_reply(m_core, winrt::to_string(account).c_str(),
                winrt::to_string(settings_json).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Lit les réglages de synchronisation hors connexion d'un compte (et d'un dossier).
         * Paramètres :
         *   account : identifiant du compte
         *   folder  : chemin complet du dossier, vide pour le compte seul
         *   handler : rappel recevant {"supported", "stay_synchronized", "limit_by_age",
         *             "limit_unit", "limit_value", "folder_offline_sync"...} */
        void GetOfflineSync(winrt::hstring const& account, winrt::hstring const& folder, CoreHandler handler)
        {
            evo_core_get_offline_sync(m_core, winrt::to_string(account).c_str(),
                winrt::to_string(folder).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Enregistre les réglages de synchronisation hors connexion.
         * Paramètres :
         *   settings_json : champs à modifier (voir evo_core_set_offline_sync)
         *   handler       : rappel recevant {} */
        void SetOfflineSync(winrt::hstring const& settings_json, CoreHandler handler)
        {
            evo_core_set_offline_sync(m_core, winrt::to_string(settings_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Supprime un message (vers la corbeille du compte ou définitivement).
         * Paramètres :
         *   account : identifiant du compte
         *   folder  : chemin complet du dossier
         *   uid     : identifiant du message à supprimer
         *   handler : rappel sur le thread de l'interface */
        /* Action sur tout un dossier : "mark-all-read" ou "empty".
         * Paramètres :
         *   account, folder : dossier visé
         *   action          : "mark-all-read" ou "empty"
         *   handler         : rappel recevant {"count": n, "permanent": true|false} */
        void FolderAction(winrt::hstring const& account, winrt::hstring const& folder, winrt::hstring const& action,
                          CoreHandler handler)
        {
            evo_core_folder_action(m_core, winrt::to_string(account).c_str(), winrt::to_string(folder).c_str(),
                winrt::to_string(action).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        void DeleteMessage(winrt::hstring const& account, winrt::hstring const& folder, winrt::hstring const& uid,
                           CoreHandler handler)
        {
            evo_core_delete_message(m_core, winrt::to_string(account).c_str(), winrt::to_string(folder).c_str(),
                CoreUid(uid).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Déplace un message d'un dossier vers un autre dossier.
         * Paramètres :
         *   src_account : identifiant du compte source
         *   src_folder  : chemin complet du dossier source
         *   uid         : identifiant du message à déplacer
         *    dst_account : identifiant du compte de destination
         *   dst_folder  : chemin complet du dossier de destination
         *   handler     : rappel sur le thread de l'interface */
        /* Copie un message dans un autre dossier (l'original reste en place). */
        void CopyMessage(winrt::hstring const& src_account, winrt::hstring const& src_folder, winrt::hstring const& uid,
                         winrt::hstring const& dst_account, winrt::hstring const& dst_folder, CoreHandler handler)
        {
            evo_core_copy_message(m_core, winrt::to_string(src_account).c_str(), winrt::to_string(src_folder).c_str(),
                CoreUid(uid).c_str(), winrt::to_string(dst_account).c_str(), winrt::to_string(dst_folder).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        void MoveMessage(winrt::hstring const& src_account, winrt::hstring const& src_folder, winrt::hstring const& uid,
                         winrt::hstring const& dst_account, winrt::hstring const& dst_folder, CoreHandler handler)
        {
            evo_core_move_message(m_core, winrt::to_string(src_account).c_str(), winrt::to_string(src_folder).c_str(),
                CoreUid(uid).c_str(), winrt::to_string(dst_account).c_str(), winrt::to_string(dst_folder).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Crée un dossier.
         * Paramètres :
         *   account : identifiant du compte
         *   parent  : chemin complet du parent ("" : premier niveau du compte)
         *   name    : nom du nouveau dossier
         *   handler : rappel recevant {"full_name": "..."} */
        void CreateFolder(winrt::hstring const& account, winrt::hstring const& parent, winrt::hstring const& name,
                          CoreHandler handler)
        {
            evo_core_create_folder(m_core, winrt::to_string(account).c_str(), winrt::to_string(parent).c_str(),
                winrt::to_string(name).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Renomme un dossier (il garde son parent).
         * Paramètres :
         *   account  : identifiant du compte
         *   folder   : chemin complet du dossier
         *   new_name : nouveau nom (sans chemin)
         *   handler  : rappel recevant {"full_name": "..."} */
        void RenameFolder(winrt::hstring const& account, winrt::hstring const& folder, winrt::hstring const& new_name,
                          CoreHandler handler)
        {
            evo_core_rename_folder(m_core, winrt::to_string(account).c_str(), winrt::to_string(folder).c_str(),
                winrt::to_string(new_name).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Déplace (copy = false) ou copie (copy = true) un dossier sous un
         * autre parent du même compte.
         * Paramètres :
         *   account : identifiant du compte
         *   folder  : chemin complet du dossier
         *   parent  : chemin complet du nouveau parent ("" : premier niveau)
         *   copy    : copier les messages au lieu de déplacer le dossier
         *   handler : rappel recevant {"full_name": "..."} (et "count" pour une copie) */
        void MoveOrCopyFolder(winrt::hstring const& account, winrt::hstring const& folder, winrt::hstring const& parent,
                              bool copy, CoreHandler handler)
        {
            (copy ? evo_core_copy_folder : evo_core_move_folder)(m_core, winrt::to_string(account).c_str(),
                winrt::to_string(folder).c_str(), winrt::to_string(parent).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Supprime un dossier sans sous-dossiers, et définitivement ses messages.
         * Paramètres :
         *   account : identifiant du compte
         *   folder  : chemin complet du dossier
         *   handler : rappel recevant {"count": n} */
        void DeleteFolder(winrt::hstring const& account, winrt::hstring const& folder, CoreHandler handler)
        {
            evo_core_delete_folder(m_core, winrt::to_string(account).c_str(), winrt::to_string(folder).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Recherche les messages d'un dossier correspondant à une requête.
         * Paramètres :
         *   account   : identifiant du compte
         *   folder    : chemin complet du dossier
         *   query     : texte recherché
         *   max_count : nombre maximal de résultats (0 pour tous)
         *   handler   : rappel sur le thread de l'interface */
        void SearchMessages(winrt::hstring const& account, winrt::hstring const& folder, winrt::hstring const& query,
                            unsigned max_count, CoreHandler handler)
        {
            evo_core_search_messages(m_core, winrt::to_string(account).c_str(), winrt::to_string(folder).c_str(),
                winrt::to_string(query).c_str(), max_count, &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Recherche les messages avec une portée spécifique (dossier courant, sous-dossiers, compte courant, tous les comptes).
         * Paramètres :
         *   account   : identifiant du compte
         *   folder    : chemin complet du dossier
         *   scope     : portée ("folder", "subfolders", "account", "all")
         *   query     : texte recherché
         *   max_count : nombre maximal de résultats (0 pour tous)
         *   handler   : rappel sur le thread de l'interface */
        void SearchMessagesScoped(winrt::hstring const& account, winrt::hstring const& folder,
                                  winrt::hstring const& scope, winrt::hstring const& query,
                                  unsigned max_count, CoreHandler handler)
        {
            evo_core_search_messages_scoped(m_core, winrt::to_string(account).c_str(), winrt::to_string(folder).c_str(),
                winrt::to_string(scope).c_str(), winrt::to_string(query).c_str(), max_count,
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Charge une vue unifiée : les messages d'un rôle de dossier de tous les comptes.
         * Paramètres :
         *   role      : rôle du dossier ("inbox", "sent", "drafts", "trash")
         *   accounts  : comptes inclus, séparés par des virgules ("" : tous)
         *   max_count : nombre maximal de messages (0 pour tous)
         *   handler   : rappel recevant le tableau JSON des messages (uid composite,
         *               compte et dossier d'origine) */
        void ListUnified(winrt::hstring const& role, winrt::hstring const& accounts, unsigned max_count, CoreHandler handler)
        {
            evo_core_list_unified(m_core, winrt::to_string(role).c_str(), winrt::to_string(accounts).c_str(), max_count,
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Enregistre une pièce jointe d'un message dans un fichier local.
         * Paramètres :
         *   account          : identifiant du compte
         *   folder           : chemin complet du dossier
         *   uid              : identifiant du message
         *   attachment_index : indice de la pièce jointe
         *   dest_path        : chemin complet du fichier de destination
         *   handler          : rappel sur le thread de l'interface */
        void SaveAttachment(winrt::hstring const& account, winrt::hstring const& folder, winrt::hstring const& uid,
                            unsigned attachment_index, winrt::hstring const& dest_path, CoreHandler handler)
        {
            evo_core_save_attachment(m_core, winrt::to_string(account).c_str(), winrt::to_string(folder).c_str(),
                CoreUid(uid).c_str(), attachment_index, winrt::to_string(dest_path).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Exporte un ou plusieurs messages d'un dossier sous forme de fichiers .eml
         * temporaires pour les joindre à un courriel.
         * Paramètres :
         *   request_json : JSON {"account": "...", "folder": "...", "uids": [...]}
         *   handler      : rappel recevant le tableau JSON des chemins .eml */
        void ExportMessagesEml(winrt::hstring const& request_json, CoreHandler handler)
        {
            evo_core_export_messages_eml(m_core, winrt::to_string(request_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Enregistre le mot de passe d'un compte ou serveur d'envoi dans le
         * gestionnaire d'identifiants Windows.
         * Paramètres :
         *   source_uid : identifiant de la source EDS
         *   password   : mot de passe saisi par l'utilisateur
         *   handler    : rappel sur le thread de l'interface */
        void SavePassword(winrt::hstring const& source_uid, winrt::hstring const& password, CoreHandler handler)
        {
            evo_core_save_password(m_core, winrt::to_string(source_uid).c_str(), winrt::to_string(password).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Teste la connectivité aux serveurs entrant et sortant.
         * Paramètres :
         *   account_json : JSON décrivant les serveurs à tester
         *   handler      : rappel recevant le résultat du test */
        void TestConnection(winrt::hstring const& account_json, CoreHandler handler)
        {
            evo_core_test_connection(m_core, winrt::to_string(account_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Récupère la configuration détaillée d'un compte existant.
         * Paramètres :
         *   account_uid : identifiant unique du compte
         *   handler     : rappel recevant les propriétés du compte */
        void GetAccount(winrt::hstring const& account_uid, CoreHandler handler)
        {
            evo_core_get_account(m_core, winrt::to_string(account_uid).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Modifie la configuration d'un compte de messagerie existant.
         * Paramètres :
         *   account_json : modifications à appliquer au format JSON
         *   handler      : rappel recevant {"uid": "..."} */
        void ModifyAccount(winrt::hstring const& account_json, CoreHandler handler)
        {
            evo_core_modify_account(m_core, winrt::to_string(account_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Crée un nouveau compte IMAP / SMTP dans evolution-data-server.
         * Paramètres :
         *   account_json : configuration JSON du compte
         *   handler      : rappel sur le thread de l'interface */
        void AddAccount(winrt::hstring const& account_json, CoreHandler handler)
        {
            evo_core_add_account(m_core, winrt::to_string(account_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Détecte le type d'un compte et ses serveurs à partir de l'adresse.
         * Paramètres :
         *   email   : adresse électronique
         *   handler : rappel recevant {"type", "source", "provider", "user",
         *             "imap_host", "imap_port", "imap_security", "smtp_host",
         *             "smtp_port", "smtp_security", "oauth2", "ews_url"} */
        void DetectAccount(winrt::hstring const& email, CoreHandler handler)
        {
            evo_core_detect_account(m_core, winrt::to_string(email).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Crée un compte Gmail / Google Workspace (courrier, agendas, contacts).
         * Paramètres :
         *   account_json : JSON {"email", "display_name", "full_name"}
         *   handler      : rappel recevant {"uid", "collection_uid"} */
        void AddGoogleAccount(winrt::hstring const& account_json, CoreHandler handler)
        {
            evo_core_add_google_account(m_core, winrt::to_string(account_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Active l'agenda et les contacts Google d'un compte Gmail existant.
         * Paramètres :
         *   email   : adresse du compte Gmail
         *   handler : rappel recevant {"collection_uid", "created", "already_enabled"} */
        void EnableGoogleServices(winrt::hstring const& email, CoreHandler handler)
        {
            evo_core_enable_google_services(m_core, winrt::to_string(email).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Crée un compte Microsoft 365 / Exchange Online (Microsoft Graph).
         * Paramètres :
         *   account_json : JSON {"email", "display_name", "full_name", "client_id"?, "tenant"?},
         *                  ou compte Exchange : {"backend": "ews", "hosturl", "user"?, "password"?, ...}
         *   handler      : rappel recevant {"uid", "collection_uid"} */
        void AddMicrosoft365Account(winrt::hstring const& account_json, CoreHandler handler)
        {
            evo_core_add_microsoft365_account(m_core, winrt::to_string(account_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Connexion OAuth2, étape 1 : page de connexion et adresse de redirection.
         * Paramètres :
         *   source_uid : uid du compte ou de sa collection
         *   handler    : rappel recevant {"source_uid", "uri", "redirect_uri", "service"} */
        void OAuth2Begin(winrt::hstring const& source_uid, CoreHandler handler)
        {
            evo_core_oauth2_begin(m_core, winrt::to_string(source_uid).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Connexion OAuth2 avec adresse de redirection personnalisée (loopback RFC 8252).
         * Paramètres :
         *   source_uid   : uid du compte ou de sa collection
         *   redirect_uri : URI de redirection locale (ex. http://127.0.0.1:<port>/oauth2callback)
         *   handler      : rappel recevant {"source_uid", "uri", "redirect_uri", "service"} */
        void OAuth2Begin(winrt::hstring const& source_uid, winrt::hstring const& redirect_uri, CoreHandler handler)
        {
            if (redirect_uri.empty())
                OAuth2Begin(source_uid, std::move(handler));
            else
                evo_core_oauth2_begin_with_redirect(m_core, winrt::to_string(source_uid).c_str(),
                    winrt::to_string(redirect_uri).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Connexion OAuth2, étape 2 : adresse de redirection atteinte (avec le code).
         * Paramètres :
         *   source_uid   : source_uid renvoyé par OAuth2Begin
         *   redirect_uri : adresse complète atteinte
         *   handler      : rappel recevant {} en cas de succès */
        void OAuth2Finish(winrt::hstring const& source_uid, winrt::hstring const& redirect_uri, CoreHandler handler)
        {
            evo_core_oauth2_finish(m_core, winrt::to_string(source_uid).c_str(),
                winrt::to_string(redirect_uri).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Supprime un compte de messagerie d'evolution-data-server.
         * Paramètres :
         *   account_uid : identifiant du compte à supprimer
         *   handler     : rappel sur le thread de l'interface */
        /* Désigne le compte par défaut des nouveaux messages.
         * Paramètres :
         *   account_uid : uid du compte
         *   handler     : rappel recevant {} */
        void SetDefaultAccount(winrt::hstring const& account_uid, CoreHandler handler)
        {
            evo_core_set_default_account(m_core, winrt::to_string(account_uid).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        void RemoveAccount(winrt::hstring const& account_uid, CoreHandler handler)
        {
            evo_core_remove_account(m_core, winrt::to_string(account_uid).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Liste les événements des agendas activés dans l'intervalle donné.
         * Paramètres :
         *   start_unix : début de la période en secondes Unix
         *   end_unix   : fin de la période en secondes Unix
         *   handler    : rappel sur le thread de l'interface */
        void ListEvents(long long start_unix, long long end_unix, CoreHandler handler)
        {
            evo_core_list_events(m_core, start_unix, end_unix,
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Détail d'un événement (organisateur, participants, lecture seule…). */
        void GetEvent(winrt::hstring const& calendar_uid, winrt::hstring const& uid, CoreHandler handler)
        {
            evo_core_get_event(m_core, winrt::to_string(calendar_uid).c_str(), CoreUid(uid).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Modifie un événement : JSON {"calendar_uid", "uid", "summary", ...}. */
        void ModifyEvent(winrt::hstring const& event_json, CoreHandler handler)
        {
            evo_core_modify_event(m_core, winrt::to_string(event_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Actualise les agendas auprès des serveurs distants. */
        void RefreshCalendars(CoreHandler handler)
        {
            evo_core_refresh_calendars(m_core, &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Événements des seuls agendas calendar_uids (séparés par des virgules). */
        void ListCalendarEvents(long long start_unix, long long end_unix, winrt::hstring const& calendar_uids, CoreHandler handler)
        {
            evo_core_list_calendar_events(m_core, start_unix, end_unix, winrt::to_string(calendar_uids).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Crée un événement dans un agenda.
         * Paramètres :
         *   event_json : description JSON de l'événement
         *   handler    : rappel sur le thread de l'interface */
        void CreateEvent(winrt::hstring const& event_json, CoreHandler handler)
        {
            evo_core_create_event(m_core, winrt::to_string(event_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Supprime un événement d'un agenda.
         * Paramètres :
         *   calendar_uid : identifiant de l'agenda
         *   event_uid    : identifiant de l'événement
         *   handler      : rappel sur le thread de l'interface */
        void DeleteEvent(winrt::hstring const& calendar_uid, winrt::hstring const& event_uid, CoreHandler handler)
        {
            evo_core_delete_event(m_core, winrt::to_string(calendar_uid).c_str(), winrt::to_string(event_uid).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Liste les tâches des listes de tâches activées.
         * Paramètres :
         *   list_uid : identifiant de la liste de tâches (vide pour toutes)
         *   handler  : rappel recevant le tableau JSON des tâches */
        void ListTasks(winrt::hstring const& list_uid, CoreHandler handler)
        {
            evo_core_list_tasks(m_core, winrt::to_string(list_uid).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Crée une tâche.
         * Paramètres :
         *   task_json : description JSON de la tâche (voir evo_core_create_task)
         *   handler   : rappel recevant {"uid", "list_uid"} */
        void CreateTask(winrt::hstring const& task_json, CoreHandler handler)
        {
            evo_core_create_task(m_core, winrt::to_string(task_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Modifie les champs indiqués d'une tâche.
         * Paramètres :
         *   task_json : JSON {"list_uid", "uid", champs modifiés...}
         *   handler   : rappel recevant {} */
        void ModifyTask(winrt::hstring const& task_json, CoreHandler handler)
        {
            evo_core_modify_task(m_core, winrt::to_string(task_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Supprime une tâche.
         * Paramètres :
         *   list_uid : identifiant de la liste de tâches
         *   task_uid : identifiant de la tâche
         *   handler  : rappel recevant {} */
        void DeleteTask(winrt::hstring const& list_uid, winrt::hstring const& task_uid, CoreHandler handler)
        {
            evo_core_delete_task(m_core, winrt::to_string(list_uid).c_str(), winrt::to_string(task_uid).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Liste ou recherche les contacts des carnets d'adresses activés.
         * Paramètres :
         *   query   : texte recherché (ou chaîne vide pour tous)
         *   handler : rappel sur le thread de l'interface */
        /* Destinataires pour l'autocomplétion (tous les carnets, GAL comprise). */
        void SearchRecipients(winrt::hstring const& text, int max_count, CoreHandler handler)
        {
            evo_core_search_recipients(m_core, winrt::to_string(text).c_str(), max_count,
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Recherche dans l'annuaire de l'organisation (GAL : Microsoft 365, Google
         * Workspace) : [{"name", "email", "title", "department", "company", "office",
         * "phone", "mobile", "account", "provider"}, ...]. */
        void SearchGal(winrt::hstring const& text, int max_count, CoreHandler handler)
        {
            evo_core_search_gal(m_core, winrt::to_string(text).c_str(), max_count,
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Annuaire d'un seul compte (identifiant ou nom affiché). */
        void SearchGalAccount(winrt::hstring const& account, winrt::hstring const& text, int max_count, CoreHandler handler)
        {
            evo_core_search_gal_account(m_core, winrt::to_string(account).c_str(), winrt::to_string(text).c_str(), max_count,
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Envoie par messagerie les invitations (iTIP REQUEST) d'un événement à ses participants,
         * depuis l'adresse du compte sender_uid : {"sent": nombre, "to": "adresses"}. */
        void SendEventInvitations(winrt::hstring const& sender_uid, winrt::hstring const& calendar_uid,
                                  winrt::hstring const& event_uid, CoreHandler handler)
        {
            evo_core_send_event_invitations(m_core, winrt::to_string(sender_uid).c_str(),
                winrt::to_string(calendar_uid).c_str(), winrt::to_string(event_uid).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Groupes de calendriers d'Outlook lus par EWS : [{"name": groupe, "calendars": [{"name", "email"}]}]. */
        void ListNavCalendars(winrt::hstring const& account, CoreHandler handler)
        {
            evo_core_list_nav_calendars(m_core, winrt::to_string(account).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Page d'autorisation Microsoft pour l'accès EWS : {"url": "..."}. */
        void GetEwsConsentUrl(winrt::hstring const& account, CoreHandler handler)
        {
            evo_core_get_ews_consent_url(m_core, winrt::to_string(account).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Groupes de calendriers Outlook du compte : [{"name", "class_id", "calendars": [{"name", "address"}]}]. */
        void ListCalendarGroups(winrt::hstring const& account, CoreHandler handler)
        {
            evo_core_list_calendar_groups(m_core, winrt::to_string(account).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Ajoute une réunion Google Meet à un événement d'un calendrier Gmail : {"link": "..."}. */
        void AddGoogleMeet(winrt::hstring const& calendar_uid, winrt::hstring const& event_uid, CoreHandler handler)
        {
            evo_core_add_google_meet(m_core, winrt::to_string(calendar_uid).c_str(), winrt::to_string(event_uid).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Boîte de réception prioritaire (Microsoft 365) : {"other": ["uid", ...], "complete": bool}. */
        void ListInboxClassification(winrt::hstring const& account, CoreHandler handler)
        {
            evo_core_list_inbox_classification(m_core, winrt::to_string(account).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Classe un message dans « focused » ou « other » (avec règle pour l'expéditeur si sender non vide). */
        void SetMessageClassification(winrt::hstring const& account, winrt::hstring const& uid,
            winrt::hstring const& classification, winrt::hstring const& sender, CoreHandler handler)
        {
            evo_core_set_message_classification(m_core, winrt::to_string(account).c_str(), winrt::to_string(uid).c_str(),
                winrt::to_string(classification).c_str(), winrt::to_string(sender).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Éléments récupérables d'un compte Microsoft 365 : [{"id", "subject", "from", "received"}]. */
        void ListRecoverableItems(winrt::hstring const& account, CoreHandler handler)
        {
            evo_core_list_recoverable_items(m_core, winrt::to_string(account).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Supprime définitivement des éléments récupérables (identifiants séparés par des espaces). */
        void PurgeRecoverableItems(winrt::hstring const& account, winrt::hstring const& ids, CoreHandler handler)
        {
            evo_core_purge_recoverable_items(m_core, winrt::to_string(account).c_str(), winrt::to_string(ids).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Restaure des éléments récupérables dans la Boîte de réception (identifiants séparés par des espaces). */
        void RestoreRecoverableItems(winrt::hstring const& account, winrt::hstring const& ids, CoreHandler handler)
        {
            evo_core_restore_recoverable_items(m_core, winrt::to_string(account).c_str(), winrt::to_string(ids).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Équipe de l'utilisateur (responsable, collaborateurs, collègues) : [{"name", "email", "relation"}]. */
        void ListTeam(winrt::hstring const& account, CoreHandler handler)
        {
            evo_core_list_team(m_core, winrt::to_string(account).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Événements du calendrier d'une personne de l'annuaire Microsoft 365 (lecture seule). */
        void ListPersonEvents(winrt::hstring const& account, winrt::hstring const& email, winrt::hstring const& name,
                              winrt::hstring const& color, long long start_unix, long long end_unix, CoreHandler handler)
        {
            evo_core_list_person_events(m_core, winrt::to_string(account).c_str(), winrt::to_string(email).c_str(),
                winrt::to_string(name).c_str(), winrt::to_string(color).c_str(), start_unix, end_unix,
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Photo de profil d'une personne de l'annuaire Microsoft 365 : {"photo": "data:image/jpeg;base64,..."}. */
        void GetGalPhoto(winrt::hstring const& account, winrt::hstring const& email, CoreHandler handler)
        {
            evo_core_get_gal_photo(m_core, winrt::to_string(account).c_str(), winrt::to_string(email).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Contacts des seuls carnets book_uids (séparés par des virgules). */
        void ListBookContacts(winrt::hstring const& query, winrt::hstring const& book_uids, CoreHandler handler)
        {
            evo_core_list_book_contacts(m_core, winrt::to_string(query).c_str(), winrt::to_string(book_uids).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        void ListContacts(winrt::hstring const& query, CoreHandler handler)
        {
            evo_core_list_contacts(m_core, winrt::to_string(query).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Crée un contact dans un carnet d'adresses.
         * Paramètres :
         *   contact_json : description JSON du contact
         *   handler      : rappel sur le thread de l'interface */
        void CreateContact(winrt::hstring const& contact_json, CoreHandler handler)
        {
            evo_core_create_contact(m_core, winrt::to_string(contact_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Supprime un contact d'un carnet d'adresses.
         * Paramètres :
         *   book_uid    : identifiant du carnet d'adresses
         *   contact_uid : identifiant du contact à supprimer
         *   handler     : rappel sur le thread de l'interface */
        void DeleteContact(winrt::hstring const& book_uid, winrt::hstring const& contact_uid, CoreHandler handler)
        {
            evo_core_delete_contact(m_core, winrt::to_string(book_uid).c_str(), winrt::to_string(contact_uid).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Remplace les catégories d'un contact (champ vCard CATEGORIES).
         * Paramètres :
         *   book_uid        : identifiant du carnet d'adresses
         *   contact_uid     : identifiant du contact
         *   categories_json : tableau JSON de noms (["Travail", ...]), [] pour aucune
         *   handler         : rappel sur le thread de l'interface */
        void SetContactCategories(winrt::hstring const& book_uid, winrt::hstring const& contact_uid,
            winrt::hstring const& categories_json, CoreHandler handler)
        {
            evo_core_set_contact_categories(m_core, winrt::to_string(book_uid).c_str(), winrt::to_string(contact_uid).c_str(),
                winrt::to_string(categories_json).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Modifie un contact existant dans un carnet d'adresses.
         * Paramètres :
         *   contact_json : description JSON modifiée du contact
         *   handler      : rappel sur le thread de l'interface */
        void ModifyContact(winrt::hstring const& contact_json, CoreHandler handler)
        {
            evo_core_modify_contact(m_core, winrt::to_string(contact_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Liste les sources activées (agendas, tâches, mémos, carnets d'adresses).
         * Paramètres :
         *   kinds   : types séparés par des virgules ("calendar,tasks", "contacts"), vide pour tous
         *   handler : rappel recevant [{"uid", "name", "kind"}, ...] */
        void ListSources(winrt::hstring const& kinds, CoreHandler handler)
        {
            evo_core_list_sources(m_core, winrt::to_string(kinds).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Exporte des contacts au format vCard (.vcf).
         * Paramètres :
         *   request_json : JSON {"book_uid": "..." (vide pour tous), "path": "...", "per_contact": false}
         *   handler      : rappel recevant {"count": n, "files": [...]} */
        void ExportContacts(winrt::hstring const& request_json, CoreHandler handler)
        {
            evo_core_export_contacts(m_core, winrt::to_string(request_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Exporte un agenda ou une liste de tâches au format iCalendar (.ics).
         * Paramètres :
         *   request_json : JSON {"source_uid": "...", "path": "...ics"}
         *   handler      : rappel recevant {"count": n} */
        void ExportCalendar(winrt::hstring const& request_json, CoreHandler handler)
        {
            evo_core_export_calendar(m_core, winrt::to_string(request_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Crée un calendrier local vide ou un abonnement à une URL iCalendar.
         * Paramètres :
         *   request_json : JSON {"name": "...", "url": "..." ("" : local), "color": "#rrggbb"}
         *   handler      : rappel recevant {"uid": "..."} */
        void CreateCalendar(winrt::hstring const& request_json, CoreHandler handler)
        {
            evo_core_create_calendar(m_core, winrt::to_string(request_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Liste les signatures de courrier.
         * Paramètres :
         *   handler : rappel recevant [{"uid", "name", "mime_type", "content"}, ...] */
        void ListSignatures(CoreHandler handler)
        {
            evo_core_list_signatures(m_core, &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Crée (uid vide) ou modifie une signature.
         * Paramètres :
         *   signature_json : JSON {"uid", "name", "mime_type", "content"}
         *   handler        : rappel recevant {"uid": "..."} */
        void SaveSignature(winrt::hstring const& signature_json, CoreHandler handler)
        {
            evo_core_save_signature(m_core, winrt::to_string(signature_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Supprime une signature.
         * Paramètres :
         *   signature_uid : uid de la signature
         *   handler       : rappel recevant {} */
        void DeleteSignature(winrt::hstring const& signature_uid, CoreHandler handler)
        {
            evo_core_delete_signature(m_core, winrt::to_string(signature_uid).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Choisit la signature des nouveaux messages d'un compte.
         * Paramètres :
         *   account_uid   : uid du compte
         *   signature_uid : "none", "autogenerated" ou uid d'une signature
         *   handler       : rappel recevant {} */
        void SetAccountSignature(winrt::hstring const& account_uid, winrt::hstring const& signature_uid, CoreHandler handler)
        {
            evo_core_set_account_signature(m_core, winrt::to_string(account_uid).c_str(),
                winrt::to_string(signature_uid).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Transmet la décision de confiance TLS de l'utilisateur au moteur.
         * Paramètres :
         *   trust_id       : identifiant de la demande ("host|sha256")
         *   trust_decision : 0 = refuser, 1 = accepter pour la session, 2 = toujours approuver */
        void RespondTlsTrust(winrt::hstring const& trust_id, int trust_decision)
        {
            evo_core_respond_tls_trust(m_core, winrt::to_string(trust_id).c_str(), trust_decision);
        }

    private:
        struct Call
        {
            CoreHandler handler;
            winrt::Microsoft::UI::Dispatching::DispatcherQueue queue{ nullptr };
            std::shared_ptr<bool> alive;
        };

        Call* NewCall(CoreHandler handler)
        {
            return new Call{ std::move(handler), m_queue, m_alive };
        }

        static winrt::hstring FromUtf8(const char* text)
        {
            return text ? winrt::to_hstring(text) : winrt::hstring{};
        }

        static void OnResult(const char* json, const char* error_message, void* user_data)
        {
            auto call = static_cast<Call*>(user_data);
            winrt::hstring json_text = FromUtf8(json);
            winrt::hstring error_text = FromUtf8(error_message);

            /* runs on a core thread: hand over to the UI thread */
            call->queue.TryEnqueue([call, json_text, error_text]()
            {
                /* an exception escaping a work item stops the application */
                try
                {
                    if (*call->alive)
                        call->handler(json_text, error_text);
                }
                catch (...)
                {
                    LogCurrentException(L"evo-core result");
                }
                delete call;
            });
        }

        static void OnEvent(const char* event, const char* json, void* user_data)
        {
            auto self = static_cast<CoreBridge*>(user_data);
            winrt::hstring event_text = FromUtf8(event);
            winrt::hstring json_text = FromUtf8(json);
            auto alive = self->m_alive;

            self->m_queue.TryEnqueue([self, alive, event_text, json_text]()
            {
                try
                {
                    if (*alive && self->m_on_event)
                        self->m_on_event(event_text, json_text);
                }
                catch (...)
                {
                    LogCurrentException(L"evo-core event");
                }
            });
        }

        EvoCore* m_core{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_queue{ nullptr };
        CoreEventHandler m_on_event;
        std::shared_ptr<bool> m_alive{ std::make_shared<bool>(true) };
    };
}
