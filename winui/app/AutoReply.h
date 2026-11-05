#pragma once

/* Réponses automatiques (absence du bureau) : fonctions pures (sans dépendance
 * à WinRT) partagées par l'application et par les tests unitaires : réglages,
 * sérialisation JSON, validation, conversion des dates et chaînes localisées
 * (français / anglais). */

#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

namespace EvolutionWin::AutoReply
{
    /** @brief Réglages de réponses automatiques d'un compte (format JSON d'evo-core). */
    struct Settings
    {
        std::string provider;                  ///< "microsoft365" ou "gmail"
        bool single_message = false;           ///< un seul message pour toutes les audiences (Gmail)
        bool enabled = false;                  ///< réponses automatiques actives
        std::string external_audience = "all"; ///< "none", "known" ou "all"
        int64_t start = 0;                     ///< début en secondes Unix, 0 = sans limite
        int64_t end = 0;                       ///< fin en secondes Unix, 0 = sans limite
        std::string subject;                   ///< objet de la réponse (Gmail)
        std::string internal_message;          ///< message pour l'organisation (texte brut, UTF-8)
        std::string external_message;          ///< message pour l'extérieur (texte brut, UTF-8)
    };

    /** @brief Résultat de la validation d'un formulaire de réponses automatiques. */
    enum class FormError
    {
        None,            ///< formulaire valide
        EndBeforeStart,  ///< la fin n'est pas postérieure au début
        IncompleteRange, ///< une seule borne saisie alors que le fournisseur exige les deux
        EmptyMessage     ///< réponses activées sans aucun message
    };

    /**
     * @brief Échappe une chaîne UTF-8 pour l'insérer dans une chaîne JSON.
     * @param text Texte brut.
     * @return Texte échappé (guillemets, barres obliques inverses, caractères de contrôle).
     */
    inline std::string JsonEscape(std::string_view text)
    {
        std::string out;
        out.reserve(text.size() + 8);
        for (unsigned char c : text)
        {
            switch (c)
            {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                }
                else
                    out += static_cast<char>(c);
            }
        }
        return out;
    }

    /**
     * @brief Sérialise des réglages au format JSON attendu par evo_core_set_auto_reply.
     * @param s Réglages à envoyer.
     * @return Chaîne JSON UTF-8.
     */
    inline std::string BuildSettingsJson(Settings const& s)
    {
        return std::string{ "{\"enabled\":" } + (s.enabled ? "true" : "false") +
            ",\"external_audience\":\"" + JsonEscape(s.external_audience) + "\"" +
            ",\"start\":" + std::to_string(s.start) +
            ",\"end\":" + std::to_string(s.end) +
            ",\"subject\":\"" + JsonEscape(s.subject) + "\"" +
            ",\"internal_message\":\"" + JsonEscape(s.internal_message) + "\"" +
            ",\"external_message\":\"" + JsonEscape(s.external_message) + "\"}";
    }

    /**
     * @brief Valide un formulaire de réponses automatiques avant l'enregistrement.
     * @param s Réglages saisis.
     * @return FormError::None si les réglages sont acceptables.
     */
    inline FormError Validate(Settings const& s)
    {
        if (s.start > 0 && s.end > 0 && s.end <= s.start)
            return FormError::EndBeforeStart;
        if (s.provider == "microsoft365" && ((s.start > 0) != (s.end > 0)))
            return FormError::IncompleteRange;
        if (s.enabled && s.internal_message.empty() && s.external_message.empty())
            return FormError::EmptyMessage;
        return FormError::None;
    }

    /**
     * @brief Indice de la liste déroulante d'audience externe pour une valeur du moteur.
     * @param audience "none", "known" ou "all".
     * @return 0 (aucun), 1 (contacts) ou 2 (tous, valeur par défaut).
     */
    inline int AudienceToIndex(std::string_view audience)
    {
        if (audience == "none")
            return 0;
        if (audience == "known")
            return 1;
        return 2;
    }

    /**
     * @brief Valeur du moteur pour un indice de la liste déroulante d'audience externe.
     * @param index 0, 1 ou 2.
     * @return "none", "known" ou "all".
     */
    inline std::string AudienceFromIndex(int index)
    {
        return index == 0 ? "none" : (index == 1 ? "known" : "all");
    }

    /**
     * @brief Convertit une date et une heure locales en secondes Unix.
     * @param year Année (ex. 2026).
     * @param month Mois (1 à 12).
     * @param day Jour (1 à 31).
     * @param hour Heure (0 à 23).
     * @param minute Minute (0 à 59).
     * @return Secondes Unix, ou 0 si la date n'est pas représentable.
     */
    inline int64_t LocalToEpoch(int year, int month, int day, int hour, int minute)
    {
        std::tm t{};
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min = minute;
        t.tm_isdst = -1;
        std::time_t v = std::mktime(&t);
        return v < 0 ? 0 : static_cast<int64_t>(v);
    }

    /**
     * @brief Décompose des secondes Unix en date et heure locales.
     * @param epoch Secondes Unix.
     * @param local Reçoit la date locale (tm_year depuis 1900, tm_mon de 0 à 11).
     * @return true si la conversion a réussi.
     */
    inline bool EpochToLocal(int64_t epoch, std::tm& local)
    {
        std::time_t v = static_cast<std::time_t>(epoch);
        return localtime_s(&local, &v) == 0;
    }

    /**
     * @brief Indique si la période d'absence est terminée.
     * @param enabled Réponses automatiques actives.
     * @param end Fin de la période en secondes Unix (0 = sans limite).
     * @param now Heure courante en secondes Unix.
     * @return true si actives avec une fin dépassée (rappel de désactivation).
     */
    inline bool IsExpired(bool enabled, int64_t end, int64_t now)
    {
        return enabled && end > 0 && now > end;
    }

    /** @brief Chaînes localisées de la boîte de dialogue des réponses automatiques. */
    enum class Str
    {
        Title, Loading, Save, Cancel, Enable, LimitPeriod, Start, End, Audience,
        AudienceNone, AudienceKnown, AudienceAll, InternalMessage, ExternalMessage,
        SingleMessage, Subject, Saving, Saved, SavedDisabled, LoadError, SaveError,
        ErrEndBeforeStart, ErrIncompleteRange, ErrEmptyMessage, GmailNote, M365Note,
        Unsupported, NoAccount
    };

    /**
     * @brief Indique si l'interface doit être affichée en anglais.
     * @param setting Valeur du réglage « general.language » ("system", "fr", "en", "en-US"...).
     * @param system_locale Langue du système (ex. "fr-FR", "en-US"), utilisée pour « system ».
     * @return true pour l'anglais, false pour le français (langue par défaut).
     */
    inline bool UseEnglish(std::wstring_view setting, std::wstring_view system_locale)
    {
        std::wstring_view lang = (setting.empty() || setting == L"system") ? system_locale : setting;
        return lang == L"en" || lang.substr(0, 3) == L"en-";
    }

    /**
     * @brief Chaîne localisée de la boîte de dialogue.
     * @param id Identifiant de la chaîne.
     * @param english true pour l'anglais, false pour le français.
     * @return Texte à afficher.
     */
    inline wchar_t const* Tr(Str id, bool english)
    {
        struct Entry { wchar_t const* fr; wchar_t const* en; };
        static constexpr Entry table[] = {
            /* Title */ { L"Réponses automatiques (absence)", L"Automatic replies (out of office)" },
            /* Loading */ { L"Lecture des réglages sur le serveur…", L"Reading the settings from the server…" },
            /* Save */ { L"Enregistrer", L"Save" },
            /* Cancel */ { L"Annuler", L"Cancel" },
            /* Enable */ { L"Envoyer des réponses automatiques", L"Send automatic replies" },
            /* LimitPeriod */ { L"Limiter à une période", L"Only send during a time period" },
            /* Start */ { L"Début", L"Start" },
            /* End */ { L"Fin", L"End" },
            /* Audience */ { L"Expéditeurs externes", L"External senders" },
            /* AudienceNone */ { L"Personne (organisation uniquement)", L"None (my organization only)" },
            /* AudienceKnown */ { L"Mes contacts seulement", L"My contacts only" },
            /* AudienceAll */ { L"Tous les expéditeurs externes", L"All external senders" },
            /* InternalMessage */ { L"Message pour mon organisation", L"Message for my organization" },
            /* ExternalMessage */ { L"Message pour les expéditeurs externes", L"Message for external senders" },
            /* SingleMessage */ { L"Message de réponse", L"Reply message" },
            /* Subject */ { L"Objet de la réponse", L"Reply subject" },
            /* Saving */ { L"Enregistrement…", L"Saving…" },
            /* Saved */ { L"Réponses automatiques activées pour « %s »", L"Automatic replies turned on for “%s”" },
            /* SavedDisabled */ { L"Réponses automatiques désactivées pour « %s »", L"Automatic replies turned off for “%s”" },
            /* LoadError */ { L"Impossible de lire les réponses automatiques : ", L"Unable to read the automatic replies: " },
            /* SaveError */ { L"Échec de l'enregistrement : ", L"Saving failed: " },
            /* ErrEndBeforeStart */ { L"La fin de la période doit être postérieure à son début.", L"The end of the period must be after its start." },
            /* ErrIncompleteRange */ { L"Microsoft 365 exige une date de début et une date de fin.", L"Microsoft 365 requires both a start and an end date." },
            /* ErrEmptyMessage */ { L"Saisissez au moins un message de réponse.", L"Enter at least one reply message." },
            /* GmailNote */ { L"Gmail utilise un seul message pour toutes les audiences ; « Personne » limite la réponse à votre domaine.", L"Gmail uses a single message for every audience; “None” restricts the reply to your domain." },
            /* M365Note */ { L"Les mises en forme HTML saisies dans Outlook sont converties en texte brut à l'enregistrement.", L"HTML formatting entered in Outlook is converted to plain text when saved." },
            /* Unsupported */ { L"Les réponses automatiques ne sont disponibles que pour les comptes Microsoft 365 et Gmail.", L"Automatic replies are only available for Microsoft 365 and Gmail accounts." },
            /* NoAccount */ { L"Sélectionnez d'abord un compte de messagerie.", L"Select a mail account first." }
        };
        auto const& e = table[static_cast<int>(id)];
        return english ? e.en : e.fr;
    }
}
