#pragma once

/* Vues unifiées multi-comptes (Boîte de réception unifiée, Envoyés, Brouillons
 * et Corbeille de tous les comptes) : fonctions pures (sans dépendance à WinRT)
 * partagées par l'application et par les tests unitaires. Un message d'une vue
 * unifiée garde son compte et son dossier d'origine ; son identifiant affiché
 * est composite pour rester unique d'un compte à l'autre. */

#include <string>
#include <string_view>
#include <vector>

namespace EvolutionWin::UnifiedViews
{
    /** Identifiant de compte factice des nœuds de l'arborescence des vues unifiées. */
    constexpr wchar_t kAccountId[] = L"*unified";

    /** Séparateur des éléments d'un identifiant composite (code ASCII 31). */
    constexpr wchar_t kUidSeparator = L'\x1F';

    /** Nombre minimal de comptes de courrier pour proposer les vues unifiées. */
    constexpr size_t kMinAccounts = 2;

    /** Nombre maximal de messages chargés dans une vue unifiée. */
    constexpr unsigned kMaxMessages = 500;

    /**
     * @brief Indique si un identifiant de compte désigne une vue unifiée.
     * @param account Identifiant de compte (ou factice) à tester.
     * @return true pour l'identifiant des vues unifiées.
     */
    inline bool IsUnifiedAccount(std::wstring_view account)
    {
        return account == kAccountId;
    }

    /**
     * @brief Identifiant réel d'un message (celui que connaît le moteur).
     * Un identifiant composite « uid&lt;SEP&gt;compte&lt;SEP&gt;dossier » est réduit à
     * « uid » ; un identifiant ordinaire est rendu tel quel.
     * @param uid Identifiant affiché du message.
     * @return Identifiant à transmettre au moteur.
     */
    inline std::wstring RealUid(std::wstring_view uid)
    {
        size_t cut = uid.find(kUidSeparator);
        return std::wstring{ cut == std::wstring_view::npos ? uid : uid.substr(0, cut) };
    }

    /**
     * @brief Variante UTF-8 de RealUid (le pont avec le moteur transmet du texte UTF-8).
     * @param uid Identifiant affiché du message, en UTF-8.
     * @return Identifiant à transmettre au moteur.
     */
    inline std::string RealUid(std::string_view uid)
    {
        size_t cut = uid.find(static_cast<char>(kUidSeparator));
        return std::string{ cut == std::string_view::npos ? uid : uid.substr(0, cut) };
    }

    /**
     * @brief Construit l'identifiant composite d'un message.
     * @param uid Identifiant réel du message dans son dossier.
     * @param account Compte d'origine.
     * @param folder Dossier d'origine (chemin complet).
     * @return Identifiant unique entre comptes et dossiers.
     */
    inline std::wstring MakeUid(std::wstring_view uid, std::wstring_view account, std::wstring_view folder)
    {
        std::wstring result{ uid };
        result += kUidSeparator;
        result += account;
        result += kUidSeparator;
        result += folder;
        return result;
    }

    /**
     * @brief Rôles de dossier proposés en vue unifiée, dans l'ordre d'affichage.
     * @return Les rôles « inbox », « sent », « drafts » et « trash ».
     */
    inline std::vector<std::wstring_view> Roles()
    {
        return { L"inbox", L"sent", L"drafts", L"trash" };
    }

    /**
     * @brief Indique si un rôle de dossier dispose d'une vue unifiée.
     * @param role Rôle de dossier (« inbox », « sent »...).
     * @return true si le rôle fait partie des vues unifiées.
     */
    inline bool IsUnifiedRole(std::wstring_view role)
    {
        for (auto const& known : Roles())
            if (known == role)
                return true;
        return false;
    }

    /**
     * @brief Titre d'une vue unifiée.
     * @param role Rôle de dossier.
     * @param english true pour le libellé anglais.
     * @return Libellé de la vue (vide si le rôle n'a pas de vue unifiée).
     */
    inline std::wstring Title(std::wstring_view role, bool english)
    {
        if (role == L"inbox")
            return english ? L"Unified Inbox" : L"Boîte de réception unifiée";
        if (role == L"sent")
            return english ? L"All Sent Items" : L"Tous les éléments envoyés";
        if (role == L"drafts")
            return english ? L"All Drafts" : L"Tous les brouillons";
        if (role == L"trash")
            return english ? L"All Deleted Items" : L"Tous les éléments supprimés";
        return {};
    }

    /** @brief Compte de courrier tel que listé par le moteur. */
    struct Account
    {
        std::wstring uid;
        bool enabled{ true };
        bool local{ false };
    };

    /**
     * @brief Comptes inclus dans les vues unifiées : comptes distants activés
     * (le dossier local « Sur cet ordinateur » et les comptes désactivés en
     * sont exclus), hors comptes écartés par l'utilisateur.
     * @param accounts Comptes du moteur.
     * @param excluded Identifiants de comptes exclus (préférences).
     * @return Identifiants de comptes retenus, dans l'ordre d'entrée.
     */
    inline std::vector<std::wstring> IncludedAccounts(std::vector<Account> const& accounts,
                                                      std::vector<std::wstring> const& excluded = {})
    {
        std::vector<std::wstring> included;
        for (auto const& account : accounts)
        {
            if (!account.enabled || account.local || account.uid.empty())
                continue;
            bool skip = false;
            for (auto const& uid : excluded)
                if (uid == account.uid)
                    skip = true;
            if (!skip)
                included.push_back(account.uid);
        }
        return included;
    }

    /**
     * @brief Indique si les vues unifiées ont un sens (plusieurs comptes retenus).
     * @param included Comptes retenus.
     * @return true à partir de kMinAccounts comptes.
     */
    inline bool ShouldOffer(std::vector<std::wstring> const& included)
    {
        return included.size() >= kMinAccounts;
    }

    /**
     * @brief Liste des comptes séparés par des virgules, telle qu'attendue par le moteur.
     * @param included Comptes retenus.
     * @return Identifiants séparés par des virgules (vide : aucun).
     */
    inline std::wstring ToCsv(std::vector<std::wstring> const& included)
    {
        std::wstring csv;
        for (auto const& uid : included)
        {
            if (!csv.empty())
                csv += L',';
            csv += uid;
        }
        return csv;
    }

    /**
     * @brief Nombre de non lus affiché sur une vue unifiée : seule la boîte de
     * réception en compte (les autres dossiers n'ont pas de notion de non lu).
     * @param role Rôle de dossier.
     * @param unread_sum Somme des non-lus des dossiers de ce rôle.
     * @return Valeur à afficher.
     */
    inline int DisplayedUnread(std::wstring_view role, int unread_sum)
    {
        return role == L"inbox" && unread_sum > 0 ? unread_sum : 0;
    }
}
