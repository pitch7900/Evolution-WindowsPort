#pragma once
/**
 * @file ImmersiveReader.h
 * @brief Logique pure du lecteur immersif (vue de lecture plein écran épurée) :
 *        réglages (taille du texte, espacement, thème), normalisation et génération du document HTML.
 *        Aucune dépendance à WinRT : testé unitairement (tests/unit/test_immersive.cpp).
 */

#include <string>
#include <string_view>

namespace EvolutionWin::Immersive
{
    /** Taille minimale, maximale, par défaut et pas du texte (en pixels CSS). */
    constexpr int kMinFontSize = 14;
    constexpr int kMaxFontSize = 48;
    constexpr int kDefaultFontSize = 22;
    constexpr int kFontSizeStep = 2;

    /** Niveaux d'espacement des lignes : 0 serré, 1 normal, 2 aéré, 3 très aéré. */
    constexpr int kSpacingLevels = 4;
    constexpr int kDefaultSpacing = 1;

    /** Thèmes : 0 clair, 1 sépia, 2 sombre. */
    constexpr int kThemeCount = 3;
    constexpr int kDefaultTheme = 0;

    /** Réglages du lecteur immersif. */
    struct Options
    {
        int font_size{ kDefaultFontSize };
        int spacing{ kDefaultSpacing };
        int theme{ kDefaultTheme };
    };

    /**
     * @brief Ramène une valeur dans un intervalle.
     * @param value Valeur à borner.
     * @param low Borne basse.
     * @param high Borne haute.
     * @return La valeur bornée.
     */
    constexpr int Clamp(int value, int low, int high)
    {
        return value < low ? low : (value > high ? high : value);
    }

    /**
     * @brief Normalise des réglages lus sur disque (valeurs hors limites ramenées dans les bornes).
     * @param options Réglages bruts.
     * @return Réglages valides.
     */
    constexpr Options Normalize(Options options)
    {
        options.font_size = Clamp(options.font_size, kMinFontSize, kMaxFontSize);
        options.spacing = Clamp(options.spacing, 0, kSpacingLevels - 1);
        options.theme = Clamp(options.theme, 0, kThemeCount - 1);
        return options;
    }

    /**
     * @brief Agrandit ou réduit le texte d'un pas.
     * @param options Réglages courants.
     * @param direction +1 pour agrandir, -1 pour réduire.
     * @return Réglages modifiés et normalisés.
     */
    constexpr Options StepFontSize(Options options, int direction)
    {
        options.font_size += direction * kFontSizeStep;
        return Normalize(options);
    }

    /**
     * @brief Passe au niveau d'espacement suivant (retour au plus serré après le plus aéré).
     * @param options Réglages courants.
     * @return Réglages modifiés.
     */
    constexpr Options CycleSpacing(Options options)
    {
        options.spacing = (Clamp(options.spacing, 0, kSpacingLevels - 1) + 1) % kSpacingLevels;
        return options;
    }

    /**
     * @brief Passe au thème suivant (clair, sépia, sombre).
     * @param options Réglages courants.
     * @return Réglages modifiés.
     */
    constexpr Options CycleTheme(Options options)
    {
        options.theme = (Clamp(options.theme, 0, kThemeCount - 1) + 1) % kThemeCount;
        return options;
    }

    /**
     * @brief Hauteur de ligne d'un niveau d'espacement, en pourcentage de la taille du texte.
     * @param spacing Niveau d'espacement.
     * @return 140, 170, 200 ou 240.
     */
    constexpr int LineHeightPercent(int spacing)
    {
        constexpr int table[kSpacingLevels] = { 140, 170, 200, 240 };
        return table[Clamp(spacing, 0, kSpacingLevels - 1)];
    }

    /**
     * @brief Échappe un texte pour l'insérer dans du HTML.
     * @param text Texte brut.
     * @return Texte avec &, <, >, " et ' remplacés par leurs entités.
     */
    inline std::wstring EscapeHtml(std::wstring_view text)
    {
        std::wstring out;
        out.reserve(text.size() + text.size() / 8);
        for (wchar_t c : text)
        {
            switch (c)
            {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            case L'\'': out += L"&#39;"; break;
            default: out += c; break;
            }
        }
        return out;
    }

    /**
     * @brief Transforme un texte brut en paragraphes HTML : les lignes vides séparent les paragraphes,
     *        les retours à la ligne simples deviennent des <br>. Les fins de ligne CRLF sont acceptées.
     * @param text Texte brut du message.
     * @return Suite d'éléments <p> (vide si le texte est vide).
     */
    inline std::wstring ParagraphsToHtml(std::wstring_view text)
    {
        std::wstring out;
        std::wstring current;
        bool has_content = false;

        auto flush = [&]()
        {
            if (has_content)
                out += L"<p>" + current + L"</p>\n";
            current.clear();
            has_content = false;
        };

        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find(L'\n', start);
            std::wstring_view line = text.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start);
            while (!line.empty() && (line.back() == L'\r' || line.back() == L' ' || line.back() == L'\t'))
                line.remove_suffix(1);

            if (line.empty())
                flush();
            else
            {
                if (has_content)
                    current += L"<br>";
                current += EscapeHtml(line);
                has_content = true;
            }

            if (end == std::wstring_view::npos)
                break;
            start = end + 1;
        }
        flush();
        return out;
    }

    /**
     * @brief Couleurs (fond, texte, texte secondaire) d'un thème.
     * @param theme Numéro de thème.
     * @param[out] background Couleur de fond CSS.
     * @param[out] foreground Couleur du texte CSS.
     * @param[out] muted Couleur du texte secondaire CSS.
     */
    inline void ThemeColors(int theme, std::wstring& background, std::wstring& foreground, std::wstring& muted)
    {
        switch (Clamp(theme, 0, kThemeCount - 1))
        {
        case 1: background = L"#f4ecd8"; foreground = L"#3b2f1e"; muted = L"#7a6a52"; break;
        case 2: background = L"#1b1b1f"; foreground = L"#e6e6ea"; muted = L"#9a9aa3"; break;
        default: background = L"#ffffff"; foreground = L"#1a1a1a"; muted = L"#666666"; break;
        }
    }

    /**
     * @brief Script minimal du document : transmet à l'application les touches Échap (quitter),
     *        + / - (taille du texte), T (thème) et L (espacement) par message WebView2. Le document
     *        ne contient que du texte échappé : aucun contenu distant ni code du message.
     */
    inline constexpr wchar_t kKeyScript[] =
        L"function p(m){window.chrome.webview.postMessage(m);}"
        L"document.addEventListener('keydown',function(e){var k=e.key;"
        L"if(k==='Escape')p('close');else if(k==='+'||k==='=')p('bigger');else if(k==='-')p('smaller');"
        L"else if(k==='t'||k==='T')p('theme');else if(k==='l'||k==='L')p('spacing');});";

    /**
     * @brief Construit le document HTML complet du lecteur immersif (régénéré à chaque changement
     *        de réglage).
     * @param subject Objet du message.
     * @param sender Expéditeur (affiché sous l'objet).
     * @param date Date affichée (chaîne déjà formatée, éventuellement vide).
     * @param body Corps du message en texte brut.
     * @param options Réglages (normalisés ici).
     * @return Document HTML.
     */
    inline std::wstring BuildHtml(std::wstring_view subject, std::wstring_view sender, std::wstring_view date,
        std::wstring_view body, Options options)
    {
        options = Normalize(options);
        std::wstring background, foreground, muted;
        ThemeColors(options.theme, background, foreground, muted);

        std::wstring html = L"<!DOCTYPE html><html><head><meta charset=\"utf-8\">";
        html += L"<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'\">";
        html += L"<style>";
        html += L"html{background:" + background + L";}";
        html += L"body{margin:0;padding:72px 24px 96px 24px;color:" + foreground + L";background:" + background + L";";
        html += L"font-family:\"Segoe UI Variable Text\",\"Segoe UI\",sans-serif;font-size:" + std::to_wstring(options.font_size) + L"px;";
        html += L"line-height:" + std::to_wstring(LineHeightPercent(options.spacing)) + L"%;word-break:break-word;}";
        html += L"main{max-width:720px;margin:0 auto;}";
        html += L"h1{font-size:1.5em;line-height:1.25;margin:0 0 .3em 0;}";
        html += L".meta{color:" + muted + L";font-size:.7em;margin-bottom:2em;}";
        html += L"p{margin:0 0 1em 0;}";
        html += L"</style></head><body><main>";
        html += L"<h1>" + EscapeHtml(subject) + L"</h1>";
        html += L"<div class=\"meta\">" + EscapeHtml(sender);
        if (!date.empty())
            html += L" · " + EscapeHtml(date);
        html += L"</div>";
        html += ParagraphsToHtml(body);
        html += L"</main><script>";
        html += kKeyScript;
        html += L"</script></body></html>";
        return html;
    }
}
