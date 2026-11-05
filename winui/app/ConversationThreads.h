#pragma once

/* Regroupement des messages en discussions : fonctions pures (sans dépendance
 * à WinRT) partagées par l'application et par les tests unitaires. */

#include <windows.h>
#include <cwctype>
#include <map>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

namespace EvolutionWin::Conversations
{
    /**
     * @brief Données d'un message utiles au regroupement par discussion.
     */
    struct ThreadMessage
    {
        std::wstring message_id;               /* empreinte du Message-ID ("" ou "0" : inconnue) */
        std::vector<std::wstring> references;  /* empreintes de In-Reply-To / References */
        std::wstring subject;                  /* objet tel qu'affiché */
    };

    /**
     * @brief Retire les préfixes de réponse / transfert (Re:, TR:, Fwd:, AW:, WG:, ENC:, SV:...) d'un objet.
     * @param subject Objet du message.
     * @param[out] had_prefix Renseigné à true si au moins un préfixe a été retiré (peut être nul).
     * @return Objet sans préfixes, sans espaces de tête, en minuscules (casse française comprise).
     */
    inline std::wstring NormalizeThreadSubject(std::wstring_view subject, bool* had_prefix = nullptr)
    {
        static const wchar_t* const prefixes[] = { L"re:", L"tr:", L"fw:", L"fwd:", L"aw:", L"wg:", L"enc:", L"sv:", L"antw:", L"rv:", L"r:", L"i:" };
        std::wstring text{ subject };
        if (!text.empty())
            ::CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));

        auto trim_front = [&text]()
        {
            size_t n = 0;
            while (n < text.size() && std::iswspace(text[n]))
                ++n;
            text.erase(0, n);
        };

        trim_front();
        bool any = false;
        for (bool stripped = true; stripped;)
        {
            stripped = false;
            for (const wchar_t* prefix : prefixes)
            {
                size_t len = std::wstring_view{ prefix }.size();
                if (text.compare(0, len, prefix) == 0)
                {
                    text.erase(0, len);
                    trim_front();
                    stripped = true;
                    any = true;
                }
            }
        }
        if (had_prefix)
            *had_prefix = any;
        return text;
    }

    /**
     * @brief Calcule l'appartenance de chaque message à une discussion.
     *
     * Deux messages sont de la même discussion s'ils sont reliés par un
     * Message-ID cité dans In-Reply-To / References, s'ils citent le même
     * message absent de la liste, ou (repli) si une réponse / un transfert a
     * le même objet normalisé qu'un autre message.
     * @param messages Messages à regrouper.
     * @return Pour chaque message, l'indice du premier message de sa discussion.
     */
    inline std::vector<size_t> BuildThreads(std::vector<ThreadMessage> const& messages)
    {
        std::vector<size_t> parent(messages.size());
        std::iota(parent.begin(), parent.end(), size_t{ 0 });

        auto find = [&parent](size_t i)
        {
            while (parent[i] != i)
            {
                parent[i] = parent[parent[i]];
                i = parent[i];
            }
            return i;
        };
        auto join = [&](size_t a, size_t b)
        {
            a = find(a);
            b = find(b);
            if (a == b)
                return;
            /* la racine est toujours l'indice le plus petit */
            if (a < b)
                parent[b] = a;
            else
                parent[a] = b;
        };
        auto known = [](std::wstring const& id) { return !id.empty() && id != L"0"; };

        std::map<std::wstring, size_t> by_id;
        for (size_t i = 0; i < messages.size(); ++i)
        {
            if (!known(messages[i].message_id))
                continue;
            auto inserted = by_id.emplace(messages[i].message_id, i);
            if (!inserted.second)
                join(i, inserted.first->second);
        }

        std::map<std::wstring, size_t> cited_by;
        for (size_t i = 0; i < messages.size(); ++i)
        {
            for (auto const& ref : messages[i].references)
            {
                if (!known(ref) || ref == messages[i].message_id)
                    continue;
                auto owner = by_id.find(ref);
                if (owner != by_id.end())
                    join(i, owner->second);
                auto inserted = cited_by.emplace(ref, i);
                if (!inserted.second)
                    join(i, inserted.first->second);
            }
        }

        /* repli sur l'objet : les messages de même objet normalisé sont
         * rassemblés dès que l'un d'eux est une réponse ou un transfert */
        std::vector<std::wstring> bases(messages.size());
        std::map<std::wstring, bool> has_reply;
        for (size_t i = 0; i < messages.size(); ++i)
        {
            bool had_prefix = false;
            bases[i] = NormalizeThreadSubject(messages[i].subject, &had_prefix);
            if (!bases[i].empty() && had_prefix)
                has_reply[bases[i]] = true;
        }
        std::map<std::wstring, size_t> by_subject;
        for (size_t i = 0; i < messages.size(); ++i)
        {
            if (bases[i].empty() || !has_reply.count(bases[i]))
                continue;
            auto inserted = by_subject.emplace(bases[i], i);
            if (!inserted.second)
                join(i, inserted.first->second);
        }

        std::vector<size_t> result(messages.size());
        for (size_t i = 0; i < messages.size(); ++i)
            result[i] = find(i);
        return result;
    }
}
