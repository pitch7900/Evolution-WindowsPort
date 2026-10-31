#pragma once

/*
 * Bridge between evo-core (C, callbacks on its own threads, UTF-8 JSON) and
 * the user interface: every callback is handed over to the UI thread
 * through its DispatcherQueue, with the JSON converted to an hstring.
 */

#include "evo-core.h"

namespace EvolutionWin
{
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
                winrt::to_string(uid).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        void Refresh()
        {
            evo_core_refresh(m_core);
        }

        /* not SendMessage: a Win32 macro */
        void SendMail(winrt::hstring const& message_json, CoreHandler handler)
        {
            evo_core_send_message(m_core, winrt::to_string(message_json).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
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
                winrt::to_string(uid).c_str(), winrt::to_string(flag).c_str(), value ? 1 : 0,
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Supprime un message (vers la corbeille du compte ou définitivement).
         * Paramètres :
         *   account : identifiant du compte
         *   folder  : chemin complet du dossier
         *   uid     : identifiant du message à supprimer
         *   handler : rappel sur le thread de l'interface */
        void DeleteMessage(winrt::hstring const& account, winrt::hstring const& folder, winrt::hstring const& uid,
                           CoreHandler handler)
        {
            evo_core_delete_message(m_core, winrt::to_string(account).c_str(), winrt::to_string(folder).c_str(),
                winrt::to_string(uid).c_str(), &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Déplace un message d'un dossier vers un autre dossier.
         * Paramètres :
         *   src_account : identifiant du compte source
         *   src_folder  : chemin complet du dossier source
         *   uid         : identifiant du message à déplacer
         *    dst_account : identifiant du compte de destination
         *   dst_folder  : chemin complet du dossier de destination
         *   handler     : rappel sur le thread de l'interface */
        void MoveMessage(winrt::hstring const& src_account, winrt::hstring const& src_folder, winrt::hstring const& uid,
                         winrt::hstring const& dst_account, winrt::hstring const& dst_folder, CoreHandler handler)
        {
            evo_core_move_message(m_core, winrt::to_string(src_account).c_str(), winrt::to_string(src_folder).c_str(),
                winrt::to_string(uid).c_str(), winrt::to_string(dst_account).c_str(), winrt::to_string(dst_folder).c_str(),
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
                winrt::to_string(uid).c_str(), attachment_index, winrt::to_string(dest_path).c_str(),
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

        /* Crée un nouveau compte IMAP / SMTP dans evolution-data-server.
         * Paramètres :
         *   account_json : configuration JSON du compte
         *   handler      : rappel sur le thread de l'interface */
        void AddAccount(winrt::hstring const& account_json, CoreHandler handler)
        {
            evo_core_add_account(m_core, winrt::to_string(account_json).c_str(),
                &CoreBridge::OnResult, NewCall(std::move(handler)));
        }

        /* Crée un compte Microsoft 365 / Exchange Online (Microsoft Graph).
         * Paramètres :
         *   account_json : JSON {"email", "display_name", "full_name", "client_id"?, "tenant"?}
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

        /* Liste ou recherche les contacts des carnets d'adresses activés.
         * Paramètres :
         *   query   : texte recherché (ou chaîne vide pour tous)
         *   handler : rappel sur le thread de l'interface */
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
                if (*call->alive)
                    call->handler(json_text, error_text);
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
                if (*alive && self->m_on_event)
                    self->m_on_event(event_text, json_text);
            });
        }

        EvoCore* m_core{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_queue{ nullptr };
        CoreEventHandler m_on_event;
        std::shared_ptr<bool> m_alive{ std::make_shared<bool>(true) };
    };
}
