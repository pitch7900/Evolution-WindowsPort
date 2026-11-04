#include <windows.h>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
/**







 * @file test_helpers.cpp







 * @brief Tests unitaires pour les fonctions utilitaires et algorithmes de l'interface WinUI (MainWindow.Helpers).







 *







 * Valide les comportements suivants pour garantir la non-regression entre versions :







 * - Extraction du nom de l'expediteur (SenderName)







 * - Nettoyage des noms de fichiers Windows (SanitizeFileName)







 * - Filtrage des adresses propres pour repondre a tous (WithoutAddress)







 * - Echappement des caracteres speciaux HTML (EscapeHtml)







 * - Formatage des citations de reponse (Quote)







 * - Comparaison sémantique de versions (SemVer)







 */







#include "../include/test_framework.h"







#include <cwctype>







#include <string>







#include <string_view>







#include <vector>

#include <filesystem>

#include <fstream>







#include <sstream>







/* --- Re-implementation autonome des fonctions pures de MainWindow.Helpers pour isolation de test --- */







/**







 * @brief Extrait le nom affiche a partir d'une chaine d'adresse de type "Nom <adresse>".







 * @param from Chaine d'origine (ex: "\"Pierre Dupont\" <pierre@example.com>").







 * @return Nom extrait sans guillemets, ou la chaine brute si aucun chevron n'est present.







 */







static std::wstring SenderName(std::wstring_view from)







{







    auto pos = from.find(L" <");







    if (pos != std::wstring_view::npos && pos > 0)







    {







        auto name = from.substr(0, pos);







        if (name.size() >= 2 && name.front() == L'"' && name.back() == L'"')







            name = name.substr(1, name.size() - 2);







        return std::wstring{ name };







    }







    return std::wstring{ from };







}







/**







 * @brief Assainit une chaine pour la rendre utilisable comme nom de fichier valide sous Windows 11.







 * Remplace les caracteres interdits (\ / : * ? " < > |) et les caracteres non imprimables (< 32) par '_'.







 * @param name Nom de fichier propose.







 * @param fallback Nom de secours utilise si le resultat nettoye est vide.







 * @return Nom de fichier nettoye.







 */







static std::wstring SanitizeFileName(std::wstring const& name, std::wstring const& fallback)







{







    std::wstring clean = name;







    for (auto& c : clean)







    {







        if (wcschr(L"\\/:*?\"<>|", c) || c < 32)







            c = L'_';







    }







    return clean.empty() ? fallback : clean;







}







/**







 * @brief Filtre une liste d'adresses separees par des virgules pour retirer l'adresse de l'utilisateur.







 * Utilise lors de l'action "Repondre a tous".







 * @param addresses Liste complete des destinataires sous forme de chaine.







 * @param own_address Adresse email de l'utilisateur a exclure (insensible a la casse).







 * @return Liste filtree sans l'adresse de l'utilisateur.







 */







static std::wstring WithoutAddress(std::wstring_view addresses, std::wstring_view own_address)







{







    std::wstring result;







    size_t start = 0;







    auto to_lower = [](std::wstring_view sv) {







        std::wstring low;







        low.reserve(sv.size());







        for (wchar_t c : sv)







            low.push_back(static_cast<wchar_t>(std::towlower(c)));







        return low;







    };







    std::wstring lower_own = to_lower(own_address);







    while (start <= addresses.size())







    {







        size_t end = addresses.find(L',', start);







        if (end == std::wstring_view::npos)







            end = addresses.size();







        std::wstring part{ addresses.substr(start, end - start) };







        size_t first_not_space = part.find_first_not_of(L' ');







        if (first_not_space != std::wstring::npos)







            part.erase(0, first_not_space);







        else







            part.clear();







        size_t last_not_space = part.find_last_not_of(L' ');







        if (last_not_space != std::wstring::npos)







            part.erase(last_not_space + 1);







        std::wstring lower_part = to_lower(part);







        if (!part.empty() && (lower_own.empty() || lower_part.find(lower_own) == std::wstring::npos))







        {







            if (!result.empty())







                result += L", ";







            result += part;







        }







        start = end + 1;







    }







    return result;







}







/**







 * @brief Echappe les caracteres speciaux HTML d'un texte brut pour injection securisee dans le corps du message.







 * @param text Texte brut source.







 * @return Chaine contenant les entites HTML echappees (&amp;, &lt;, &gt;, &quot;, <br>).







 */







static std::wstring EscapeHtml(std::wstring_view text)







{







    std::wstring out;







    out.reserve(text.size() + 32);







    for (wchar_t c : text)







    {







        switch (c)







        {







        case L'&': out += L"&amp;"; break;







        case L'<': out += L"&lt;"; break;







        case L'>': out += L"&gt;"; break;







        case L'"': out += L"&quot;"; break;







        case L'\r': break;







        case L'\n': out += L"<br>"; break;







        default: out += c; break;







        }







    }







    return out;







}







/**







 * @brief Formate une citation de texte brut en prefixant chaque ligne par "> ".







 * @param text Texte brut du message cite.







 * @return Texte formate pret pour une reponse textuelle.







 */







static std::wstring Quote(std::wstring_view text)







{







    std::wstring quoted = L"> ";







    for (auto c : text)







    {







        if (c == L'\r')







            continue;







        quoted += c;







        if (c == L'\n')







            quoted += L"> ";







    }







    return quoted;







}







/**







 * @brief Verifie si une chaine commence par un prefixe donne, sans distinction majuscule/minuscule.







 * @param text Chaine principale a verifier.







 * @param prefix Prefixe recherche.







 * @return true si le prefixe correspond, false sinon.







 */







static bool StartsWithNoCase(std::wstring_view text, std::wstring_view prefix)







{







    if (text.size() < prefix.size())







        return false;







    for (size_t i = 0; i < prefix.size(); ++i)







    {







        if (std::towlower(text[i]) != std::towlower(prefix[i]))







            return false;







    }







    return true;







}







/**







 * @brief Compare deux versions numeriques sémantiques (SemVer x.y.z) pour la mise a jour automatique.







 * Ignore le prefixe eventuel 'v'.







 * @param v1 Premiere version (ex: "0.9.5" ou "v0.9.5").







 * @param v2 Deuxieme version (ex: "0.9.6").







 * @return -1 si v1 < v2, 0 si v1 == v2, 1 si v1 > v2.







 */







static int CompareSemVer(std::string const& v1, std::string const& v2)







{







    auto parse_parts = [](std::string const& ver) {







        std::vector<int> parts;







        size_t start = (ver.size() > 0 && (ver[0] == 'v' || ver[0] == 'V')) ? 1 : 0;







        std::stringstream ss(ver.substr(start));







        std::string token;







        while (std::getline(ss, token, '.')) {







            parts.push_back(std::atoi(token.c_str()));







        }







        while (parts.size() < 3) parts.push_back(0);







        return parts;







    };







    auto p1 = parse_parts(v1);







    auto p2 = parse_parts(v2);







    for (size_t i = 0; i < 3; ++i)







    {







        if (p1[i] < p2[i]) return -1;







        if (p1[i] > p2[i]) return 1;







    }







    return 0;







}







/**







 * @brief Extrait et nettoie le texte d'un courriel pour la lecture vocale (SpeechSynthesis).







 * @param from Nom ou adresse de l'expediteur.







 * @param subject Objet du message.







 * @param text Corps en texte brut si disponible.







 * @param html Corps HTML si disponible.







 * @return Texte fluide debarrasse de balises et pret pour la synthese vocale.







 */







static std::wstring ExtractPlainTextForSpeech(







    std::wstring const& from,







    std::wstring const& subject,







    std::wstring const& text,







    std::wstring const& html)







{







    std::wstring speech;







    if (!from.empty())







    {







        speech += L"Message de " + SenderName(from) + L".\n";







    }







    if (!subject.empty())







    {







        speech += L"Objet : " + subject + L".\n\n";







    }







    std::wstring body_clean;







    if (!text.empty())







    {







        body_clean = text;







    }







    else if (!html.empty())







    {







        bool in_tag = false;







        for (size_t i = 0; i < html.size(); ++i)







        {







            wchar_t c = html[i];







            if (c == L'<')







            {







                in_tag = true;







                if (i + 3 <= html.size() && (_wcsnicmp(&html[i], L"<br", 3) == 0 || _wcsnicmp(&html[i], L"</p", 3) == 0 || (i + 5 <= html.size() && _wcsnicmp(&html[i], L"</div", 5) == 0)))







                {







                    body_clean += L'\n';







                }







            }







            else if (c == L'>')







            {







                in_tag = false;







            }







            else if (!in_tag)







            {







                body_clean += c;







            }







        }







    }







    auto replace_all = [](std::wstring& str, const std::wstring& from_str, const std::wstring& to_str) {







        size_t start_pos = 0;







        while ((start_pos = str.find(from_str, start_pos)) != std::wstring::npos) {







            str.replace(start_pos, from_str.length(), to_str);







            start_pos += to_str.length();







        }







    };







    replace_all(body_clean, L"&nbsp;", L" ");







    replace_all(body_clean, L"&amp;", L"&");







    replace_all(body_clean, L"&lt;", L"<");







    replace_all(body_clean, L"&gt;", L">");







    replace_all(body_clean, L"&quot;", L"\"");







    replace_all(body_clean, L"&#39;", L"'");







    speech += body_clean;







    return speech;







}







/* --- Fonctions de test unitaire --- */







/**







 * @brief Test de l'extraction de nom d'expediteur.







 */







static void test_sender_name_parsing(void)







{







    TEST_CASE("SenderName: Nom complet avec chevrons");







    {







        std::wstring res = SenderName(L"Pierre Dupont <pierre@example.com>");







        ASSERT_TRUE(res == L"Pierre Dupont");







    }







    TEST_CASE_END();







    TEST_CASE("SenderName: Nom entre guillemets doubles avec virgule");







    {







        std::wstring res = SenderName(L"\"Dupont, Pierre\" <pierre@example.com>");







        ASSERT_TRUE(res == L"Dupont, Pierre");







    }







    TEST_CASE_END();







    TEST_CASE("SenderName: Adresse email brute sans nom");







    {







        std::wstring res = SenderName(L"pierre@example.com");







        ASSERT_TRUE(res == L"pierre@example.com");







    }







    TEST_CASE_END();







    TEST_CASE("SenderName: Chaine vide");







    {







        std::wstring res = SenderName(L"");







        ASSERT_TRUE(res.empty());







    }







    TEST_CASE_END();







}







/**







 * @brief Test de l'assainissement de noms de fichiers Windows.







 */







static void test_sanitize_filename(void)







{







    TEST_CASE("SanitizeFileName: Chaine standard deja valide");







    {







        std::wstring res = SanitizeFileName(L"Calendrier_Projet", L"defaut");







        ASSERT_TRUE(res == L"Calendrier_Projet");







    }







    TEST_CASE_END();







    TEST_CASE("SanitizeFileName: Remplacement des caracteres interdits Windows");







    {







        std::wstring res = SanitizeFileName(L"Rapport: 2026/11/02 *v1* <test> | dossier? \"export\"", L"defaut");







        ASSERT_TRUE(res == L"Rapport_ 2026_11_02 _v1_ _test_ _ dossier_ _export_");







    }







    TEST_CASE_END();







    TEST_CASE("SanitizeFileName: Caracteres de controle inferieurs a 32");







    {







        std::wstring input = L"Test\tTab\nNewline\rCR";







        std::wstring res = SanitizeFileName(input, L"defaut");







        ASSERT_TRUE(res == L"Test_Tab_Newline_CR");







    }







    TEST_CASE_END();







    TEST_CASE("SanitizeFileName: Repli si chaine vide");







    {







        std::wstring res = SanitizeFileName(L"", L"secours");







        ASSERT_TRUE(res == L"secours");







    }







    TEST_CASE_END();







}







/**







 * @brief Test du filtrage de l'adresse de l'utilisateur pour Repondre a tous.







 */







static void test_without_address(void)







{







    TEST_CASE("WithoutAddress: Suppression de l'adresse utilisateur dans une liste");







    {







        std::wstring list = L"alice@test.com, pierre@example.com, bob@test.com";







        std::wstring res = WithoutAddress(list, L"pierre@example.com");







        ASSERT_TRUE(res == L"alice@test.com, bob@test.com");







    }







    TEST_CASE_END();







    TEST_CASE("WithoutAddress: Insensibilite a la casse");







    {







        std::wstring list = L"Alice@Test.Com, PIERRE@EXAMPLE.COM";







        std::wstring res = WithoutAddress(list, L"pierre@example.com");







        ASSERT_TRUE(res == L"Alice@Test.Com");







    }







    TEST_CASE_END();







    TEST_CASE("WithoutAddress: Seul destinataire present et supprime");







    {







        std::wstring list = L"pierre@example.com";







        std::wstring res = WithoutAddress(list, L"pierre@example.com");







        ASSERT_TRUE(res.empty());







    }







    TEST_CASE_END();







    TEST_CASE("WithoutAddress: Adresse avec nom associe");







    {







        std::wstring list = L"Alice <alice@test.com>, Pierre Dupont <pierre@example.com>";







        std::wstring res = WithoutAddress(list, L"pierre@example.com");







        ASSERT_TRUE(res == L"Alice <alice@test.com>");







    }







    TEST_CASE_END();







}







/**







 * @brief Test d'echappement HTML pour securiser les citations.







 */







static void test_escape_html(void)







{







    TEST_CASE("EscapeHtml: Echappement de balises et caracteres speciaux");







    {







        std::wstring input = L"<b>Attention & alerte \"test\"</b>";







        std::wstring res = EscapeHtml(input);







        ASSERT_TRUE(res == L"&lt;b&gt;Attention &amp; alerte &quot;test&quot;&lt;/b&gt;");







    }







    TEST_CASE_END();







    TEST_CASE("EscapeHtml: Conversion des retours a la ligne CRLF en <br>");







    {







        std::wstring input = L"Ligne 1\r\nLigne 2\nLigne 3";







        std::wstring res = EscapeHtml(input);







        ASSERT_TRUE(res == L"Ligne 1<br>Ligne 2<br>Ligne 3");







    }







    TEST_CASE_END();







}







/**







 * @brief Test de citation textuelle avec prefixe '> '.







 */







static void test_quote(void)







{







    TEST_CASE("Quote: Formatage d'une seule ligne");







    {







        std::wstring input = L"Bonjour Pierre";







        std::wstring res = Quote(input);







        ASSERT_TRUE(res == L"> Bonjour Pierre");







    }







    TEST_CASE_END();







    TEST_CASE("Quote: Formatage de plusieurs lignes avec CRLF");







    {







        std::wstring input = L"Premiere ligne\r\nSeconde ligne\r\nTroisieme ligne";







        std::wstring res = Quote(input);







        ASSERT_TRUE(res == L"> Premiere ligne\n> Seconde ligne\n> Troisieme ligne");







    }







    TEST_CASE_END();







}







/**







 * @brief Test de verification de prefixe sans distinction de casse.







 */







static void test_starts_with_nocase(void)







{







    TEST_CASE("StartsWithNoCase: Prefixes Re: et TR:");







    {







        ASSERT_TRUE(StartsWithNoCase(L"Re: Reunion projet", L"re:"));







        ASSERT_TRUE(StartsWithNoCase(L"RE: Reunion projet", L"re:"));







        ASSERT_TRUE(StartsWithNoCase(L"TR: Document joint", L"tr:"));







        ASSERT_TRUE(StartsWithNoCase(L"Fwd: Notification", L"FWD:"));







        ASSERT_FALSE(StartsWithNoCase(L"Reunion de rentree", L"re:"));







    }







    TEST_CASE_END();







    TEST_CASE("StartsWithNoCase: Prefixe plus long que la chaine");







    {







        ASSERT_FALSE(StartsWithNoCase(L"Re", L"Re: Long prefix"));







    }







    TEST_CASE_END();







}







/**







 * @brief Test de comparaison SemVer pour le moteur de mise a jour GitHub.







 */







static void test_semver_comparison(void)







{







    TEST_CASE("SemVer: Detection de nouvelle version disponible");







    {







        ASSERT_TRUE(CompareSemVer("0.9.5", "0.9.6") < 0);







        ASSERT_TRUE(CompareSemVer("0.9.4", "0.9.6") < 0);







        ASSERT_TRUE(CompareSemVer("0.8.0", "0.9.0") < 0);







    }







    TEST_CASE_END();







    TEST_CASE("SemVer: Egalite de version avec ou sans prefixe 'v'");







    {







        ASSERT_EQ_INT(0, CompareSemVer("0.9.6", "0.9.6"));







        ASSERT_EQ_INT(0, CompareSemVer("v0.9.6", "0.9.6"));







        ASSERT_EQ_INT(0, CompareSemVer("0.9.6", "v0.9.6"));







    }







    TEST_CASE_END();







    TEST_CASE("SemVer: Comparaison numerique stricte (piege 0.9.10 > 0.9.9)");







    {







        /* En comparaison textuelle, "0.9.10" < "0.9.9", ce qui serait un bogue majeur ! */







        ASSERT_TRUE(CompareSemVer("0.9.10", "0.9.9") > 0);







        ASSERT_TRUE(CompareSemVer("0.10.0", "0.9.6") > 0);







        ASSERT_TRUE(CompareSemVer("1.0.0", "0.9.6") > 0);







    }







    TEST_CASE_END();







}







/**







 * @brief Detection autonome de mots-cles de pieces jointes pour tests unitaires.







 */







static bool HasAttachmentKeywords(std::wstring_view text)







{







    if (text.empty())







        return false;







    std::wstring lower;







    lower.reserve(text.size());







    for (wchar_t ch : text)







        lower.push_back(std::towlower(ch));







    static const wchar_t* const patterns[] = {







        L"piÃ¨ce jointe",







        L"piÃ¨ces jointes",







        L"piece jointe",







        L"pieces jointes",







        L"ci-joint",







        L"ci-jointe",







        L"ci-joints",







        L"ci-jointes",







        L"en piÃ¨ce jointe",







        L"en piece jointe",







        L"en pj",







        L"attachment",







        L"attachments",







        L"attached file",







        L"attached files"







    };







    for (const auto* p : patterns)







    {







        if (lower.find(p) != std::wstring::npos)







            return true;







    }







    return false;







}







static void test_attachment_keywords(void)







{







    TEST_CASE("HasAttachmentKeywords: Detection en francais et anglais");







    {







        ASSERT_TRUE(HasAttachmentKeywords(L"Veuillez trouver la piÃ¨ce jointe"));







        ASSERT_TRUE(HasAttachmentKeywords(L"Le devis est ci-joint pour validation"));







        ASSERT_TRUE(HasAttachmentKeywords(L"Voir le document en piece jointe svp"));







        ASSERT_TRUE(HasAttachmentKeywords(L"Please find the attachment below"));







        ASSERT_TRUE(HasAttachmentKeywords(L"En PJ les comptes-rendus"));







        ASSERT_FALSE(HasAttachmentKeywords(L"Bonjour Pierre, rdv a 14h aujourd'hui"));







        ASSERT_FALSE(HasAttachmentKeywords(L""));







    }







    TEST_CASE_END();







}







static std::wstring BuildStandaloneHtmlMessage(







    std::wstring_view from,







    std::wstring_view to,







    std::wstring_view date,







    std::wstring_view subject,







    std::wstring_view html_body,







    std::wstring_view text_body)







{







    std::wstring out;







    out += L"<!DOCTYPE html>\n<html>\n<head>\n";







    out += L"<meta charset=\"utf-8\">\n";







    out += L"<title>";







    out += subject.empty() ? L"(Sans objet)" : EscapeHtml(subject);







    out += L"</title>\n</head>\n<body>\n";







    out += L"<div class=\"email-header\">\n";







    out += L"  <h1>" + (subject.empty() ? std::wstring{ L"(Sans objet)" } : EscapeHtml(subject)) + L"</h1>\n";







    out += L"  <div>De : " + EscapeHtml(from) + L"</div>\n";







    if (!to.empty())







        out += L"  <div>A : " + EscapeHtml(to) + L"</div>\n";







    if (!date.empty())







        out += L"  <div>Date : " + EscapeHtml(date) + L"</div>\n";







    out += L"</div>\n";







    out += L"<div class=\"email-body\">\n";







    if (!html_body.empty())







        out += html_body;







    else







        out += L"<pre>" + EscapeHtml(text_body) + L"</pre>";







    out += L"\n</div>\n</body>\n</html>";







    return out;







}







static void test_build_standalone_html_message(void)







{







    TEST_CASE("BuildStandaloneHtmlMessage: Document autonome avec HTML et metadonnees");







    {







        std::wstring html = BuildStandaloneHtmlMessage(







            L"expediteur@test.com",







            L"destinataire@test.com",







            L"02/11/2026 23:15",







            L"Rapport mensuel & synthese",







            L"<p>Contenu HTML riche</p>",







            L"");







        ASSERT_TRUE(html.find(L"<!DOCTYPE html>") != std::wstring::npos);







        ASSERT_TRUE(html.find(L"Rapport mensuel &amp; synthese") != std::wstring::npos);







        ASSERT_TRUE(html.find(L"expediteur@test.com") != std::wstring::npos);







        ASSERT_TRUE(html.find(L"<p>Contenu HTML riche</p>") != std::wstring::npos);







    }







    TEST_CASE_END();







    TEST_CASE("BuildStandaloneHtmlMessage: Repli texte brut avec balises echappees");







    {







        std::wstring html = BuildStandaloneHtmlMessage(







            L"client@test.com",







            L"",







            L"",







            L"",







            L"",







            L"Attention <script>alert(1)</script> & test");







        ASSERT_TRUE(html.find(L"(Sans objet)") != std::wstring::npos);







        ASSERT_TRUE(html.find(L"&lt;script&gt;alert(1)&lt;/script&gt; &amp; test") != std::wstring::npos);







        ASSERT_TRUE(html.find(L"<pre>") != std::wstring::npos);







    }







    TEST_CASE_END();







}







/**







 * @brief Test de l'extraction et du nettoyage de texte pour la synthese vocale.







 */







static void test_extract_plain_text_for_speech(void)







{







    TEST_CASE("ExtractPlainTextForSpeech: Extraction propre pour synthese vocale");







    {







        std::wstring speech = ExtractPlainTextForSpeech(







            L"\"Dupont, Pierre\" <pierre@example.com>",







            L"Reunion de projet",







            L"Bonjour Pierre,\nVoici le rapport.",







            L"");







        ASSERT_TRUE(speech.find(L"Message de Dupont, Pierre.") != std::wstring::npos);







        ASSERT_TRUE(speech.find(L"Objet : Reunion de projet.") != std::wstring::npos);







        ASSERT_TRUE(speech.find(L"Bonjour Pierre,\nVoici le rapport.") != std::wstring::npos);







    }







    TEST_CASE_END();







    TEST_CASE("ExtractPlainTextForSpeech: Nettoyage des balises HTML et entites");







    {







        std::wstring speech = ExtractPlainTextForSpeech(







            L"info@service.fr",







            L"Mise a jour",







            L"",







            L"<p>Bonjour &amp; bienvenue !</p><br/>Consultez <b>nos offres</b> &quot;speciales&quot; &nbsp;!");







        ASSERT_TRUE(speech.find(L"<p>") == std::wstring::npos);







        ASSERT_TRUE(speech.find(L"<b>") == std::wstring::npos);







        ASSERT_TRUE(speech.find(L"Bonjour & bienvenue !") != std::wstring::npos);







        ASSERT_TRUE(speech.find(L"\"speciales\"") != std::wstring::npos);







    }







    TEST_CASE_END();







}







/**







 * @brief Point d'entree principal de la suite de tests des helpers.







 * @return 0 si tous les tests reussissent, 1 sinon.







 */











enum class AttachmentPreviewKind



{



    Image,



    Text,



    Unsupported



};







static AttachmentPreviewKind DetectAttachmentPreviewKind(std::wstring_view filename)



{



    auto dot = filename.rfind(L'.');



    if (dot == std::wstring_view::npos || dot + 1 >= filename.size())



        return AttachmentPreviewKind::Unsupported;







    std::wstring ext{ filename.substr(dot) };



    for (auto& c : ext)



        c = static_cast<wchar_t>(std::towlower(c));







    if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".gif" ||



        ext == L".bmp" || ext == L".webp" || ext == L".ico" || ext == L".tif" || ext == L".tiff")



    {



        return AttachmentPreviewKind::Image;



    }







    if (ext == L".txt" || ext == L".csv" || ext == L".tsv" || ext == L".log" ||



        ext == L".json" || ext == L".xml" || ext == L".ini" || ext == L".cfg" ||



        ext == L".conf" || ext == L".md" || ext == L".patch" || ext == L".diff" ||



        ext == L".yaml" || ext == L".yml" || ext == L".sql" || ext == L".c" ||



        ext == L".cpp" || ext == L".h" || ext == L".hpp" || ext == L".cs" ||



        ext == L".js" || ext == L".ts" || ext == L".py")



    {



        return AttachmentPreviewKind::Text;



    }







    return AttachmentPreviewKind::Unsupported;



}







/**



 * @brief Test de la détection de format d'aperçu sécurisé de pièce jointe.



 */



static void test_attachment_preview_kind(void)



{



    TEST_CASE("DetectAttachmentPreviewKind: Images supportees");



    {



        ASSERT_TRUE(DetectAttachmentPreviewKind(L"photo.PNG") == AttachmentPreviewKind::Image);



        ASSERT_TRUE(DetectAttachmentPreviewKind(L"capture.jpg") == AttachmentPreviewKind::Image);



        ASSERT_TRUE(DetectAttachmentPreviewKind(L"schema.webp") == AttachmentPreviewKind::Image);



        ASSERT_TRUE(DetectAttachmentPreviewKind(L"icone.ico") == AttachmentPreviewKind::Image);



    }



    TEST_CASE_END();







    TEST_CASE("DetectAttachmentPreviewKind: Fichiers texte brut et code securises");



    {



        ASSERT_TRUE(DetectAttachmentPreviewKind(L"rapport.csv") == AttachmentPreviewKind::Text);



        ASSERT_TRUE(DetectAttachmentPreviewKind(L"donnees.json") == AttachmentPreviewKind::Text);



        ASSERT_TRUE(DetectAttachmentPreviewKind(L"journal.log") == AttachmentPreviewKind::Text);



        ASSERT_TRUE(DetectAttachmentPreviewKind(L"config.xml") == AttachmentPreviewKind::Text);



        ASSERT_TRUE(DetectAttachmentPreviewKind(L"source.cpp") == AttachmentPreviewKind::Text);



    }



    TEST_CASE_END();







    TEST_CASE("DetectAttachmentPreviewKind: Types a risque ou non supportes");



    {



        ASSERT_TRUE(DetectAttachmentPreviewKind(L"programme.exe") == AttachmentPreviewKind::Unsupported);



        ASSERT_TRUE(DetectAttachmentPreviewKind(L"script.vbs") == AttachmentPreviewKind::Unsupported);



        ASSERT_TRUE(DetectAttachmentPreviewKind(L"archive.zip") == AttachmentPreviewKind::Unsupported);



        ASSERT_TRUE(DetectAttachmentPreviewKind(L"sans_extension") == AttachmentPreviewKind::Unsupported);



    }



    TEST_CASE_END();



}









static std::wstring FormatContactSuggestion(std::wstring_view full_name, std::wstring_view email)

{

    if (full_name.empty() && email.empty())

        return L"(Sans nom)";

    if (email.empty())

        return std::wstring{ full_name };

    if (full_name.empty())

        return std::wstring{ email };

    return std::wstring{ full_name } + L" <" + std::wstring{ email } + L">";

}



/**

 * @brief Test du formatage des suggestions de contacts pour la recherche rapide.

 */

static void test_format_contact_suggestion(void)

{

    TEST_CASE("FormatContactSuggestion: Nom et adresse renseignes");

    {

        std::wstring res = FormatContactSuggestion(L"Pierre Dupont", L"pierre@example.com");

        ASSERT_TRUE(res == L"Pierre Dupont <pierre@example.com>");

    }

    TEST_CASE_END();



    TEST_CASE("FormatContactSuggestion: Nom seul sans adresse");

    {

        std::wstring res = FormatContactSuggestion(L"Service Support", L"");

        ASSERT_TRUE(res == L"Service Support");

    }

    TEST_CASE_END();



    TEST_CASE("FormatContactSuggestion: Adresse seule sans nom");

    {

        std::wstring res = FormatContactSuggestion(L"", L"contact@societe.com");

        ASSERT_TRUE(res == L"contact@societe.com");

    }

    TEST_CASE_END();



    TEST_CASE("FormatContactSuggestion: Champs vides avec repli par defaut");

    {

        std::wstring res = FormatContactSuggestion(L"", L"");

        ASSERT_TRUE(res == L"(Sans nom)");

    }

    TEST_CASE_END();

}





static double GetCalendarHourHeight(int scale_minutes)

{

    switch (scale_minutes)

    {

    case 15:

        return 120.0;

    case 60:

        return 48.0;

    case 30:

    default:

        return 72.0;

    }

}



/**

 * @brief Test du calcul de la hauteur d'une heure selon l'échelle de temps de la grille.

 */

static void test_calendar_time_scale(void)

{

    TEST_CASE("CalendarHourHeight: Echelle 60 minutes");

    {

        ASSERT_EQ_INT(48, static_cast<int>(GetCalendarHourHeight(60)));

    }

    TEST_CASE_END();



    TEST_CASE("CalendarHourHeight: Echelle 30 minutes (defaut)");

    {

        ASSERT_EQ_INT(72, static_cast<int>(GetCalendarHourHeight(30)));

    }

    TEST_CASE_END();



    TEST_CASE("CalendarHourHeight: Echelle 15 minutes");

    {

        ASSERT_EQ_INT(120, static_cast<int>(GetCalendarHourHeight(15)));

    }

    TEST_CASE_END();

}




static int GetTimezoneUtcOffsetHours(std::wstring_view tz, bool is_dst = false)
{
    if (tz == L"UTC") return 0;
    if (tz == L"Europe/London") return is_dst ? 1 : 0;
    if (tz == L"America/New_York") return is_dst ? -4 : -5;
    if (tz == L"America/Los_Angeles") return is_dst ? -7 : -8;
    if (tz == L"Europe/Paris") return is_dst ? 2 : 1;
    if (tz == L"Asia/Tokyo") return 9;
    return 0;
}

static std::wstring GetTimezoneShortLabel(std::wstring_view tz)
{
    if (tz == L"UTC") return L"UTC";
    if (tz == L"America/New_York") return L"NYC";
    if (tz == L"America/Los_Angeles") return L"LA";
    if (tz == L"Europe/Paris") return L"PAR";
    if (tz == L"Europe/London") return L"LON";
    if (tz == L"Asia/Tokyo") return 9 == 9 ? L"TYO" : L"";
    return L"TZ2";
}

static int CalculateEquivalentHour(int local_hour, int local_offset, int target_offset)
{
    int diff = target_offset - local_offset;
    int target = (local_hour + diff) % 24;
    if (target < 0)
        target += 24;
    return target;
}

static bool MatchesMessageSearch(
    std::wstring_view query,
    std::wstring_view subject,
    std::wstring_view from,
    std::vector<std::wstring> const& labels)
{
    if (query.empty())
        return true;

    std::wstring needle{ query };
    for (auto& ch : needle) ch = static_cast<wchar_t>(std::towlower(ch));

    std::wstring haystack = std::wstring{ subject } + L" " + std::wstring{ from };
    for (auto const& label : labels)
    {
        haystack += L" cat:" + label + L" category:" + label + L" " + label;
    }
    for (auto& ch : haystack) ch = static_cast<wchar_t>(std::towlower(ch));

    return haystack.find(needle) != std::wstring::npos;
}

static void test_message_category_search(void)
{
    std::vector<std::wstring> labels = { L"Urgent", L"Projet Alpha", L"Comptabilite" };

    TEST_CASE("MatchesMessageSearch: Recherche standard sujet/expediteur");
    {
        ASSERT_EQ_INT(1, MatchesMessageSearch(L"facture", L"Facture #1234", L"compta@example.com", labels) ? 1 : 0);
        ASSERT_EQ_INT(1, MatchesMessageSearch(L"compta", L"Facture #1234", L"compta@example.com", labels) ? 1 : 0);
        ASSERT_EQ_INT(0, MatchesMessageSearch(L"devis", L"Facture #1234", L"compta@example.com", labels) ? 1 : 0);
    }
    TEST_CASE_END();

    TEST_CASE("MatchesMessageSearch: Syntaxe cat: et category:");
    {
        ASSERT_EQ_INT(1, MatchesMessageSearch(L"cat:urgent", L"Point hebdomadaire", L"chef@example.com", labels) ? 1 : 0);
        ASSERT_EQ_INT(1, MatchesMessageSearch(L"category:projet alpha", L"Reunion", L"chef@example.com", labels) ? 1 : 0);
        ASSERT_EQ_INT(0, MatchesMessageSearch(L"cat:personnel", L"Reunion", L"chef@example.com", labels) ? 1 : 0);
    }
    TEST_CASE_END();

    TEST_CASE("MatchesMessageSearch: Mot-cle direct de categorie");
    {
        ASSERT_EQ_INT(1, MatchesMessageSearch(L"urgent", L"Discussion", L"ami@example.com", labels) ? 1 : 0);
    }
    TEST_CASE_END();
}

static void test_second_timezone_helpers(void)
{
    TEST_CASE("SecondTimezone: Decalage UTC des fuseaux");
    {
        ASSERT_EQ_INT(0, GetTimezoneUtcOffsetHours(L"UTC"));
        ASSERT_EQ_INT(-5, GetTimezoneUtcOffsetHours(L"America/New_York", false));
        ASSERT_EQ_INT(-4, GetTimezoneUtcOffsetHours(L"America/New_York", true));
        ASSERT_EQ_INT(1, GetTimezoneUtcOffsetHours(L"Europe/Paris", false));
        ASSERT_EQ_INT(2, GetTimezoneUtcOffsetHours(L"Europe/Paris", true));
        ASSERT_EQ_INT(9, GetTimezoneUtcOffsetHours(L"Asia/Tokyo", false));
    }
    TEST_CASE_END();

    TEST_CASE("SecondTimezone: Libelles courts des fuseaux");
    {
        ASSERT_EQ_INT(1, GetTimezoneShortLabel(L"UTC") == L"UTC" ? 1 : 0);
        ASSERT_EQ_INT(1, GetTimezoneShortLabel(L"America/New_York") == L"NYC" ? 1 : 0);
        ASSERT_EQ_INT(1, GetTimezoneShortLabel(L"Europe/Paris") == L"PAR" ? 1 : 0);
        ASSERT_EQ_INT(1, GetTimezoneShortLabel(L"Asia/Tokyo") == L"TYO" ? 1 : 0);
    }
    TEST_CASE_END();

    TEST_CASE("SecondTimezone: Conversion d'heures et passage de minuit");
    {
        // 14h Paris (UTC+1) -> 8h New York (UTC-5)
        ASSERT_EQ_INT(8, CalculateEquivalentHour(14, 1, -5));
        // 2h du matin Paris (UTC+1) -> 20h la veille New York (UTC-5)
        ASSERT_EQ_INT(20, CalculateEquivalentHour(2, 1, -5));
        // 22h Paris (UTC+1) -> 6h le lendemain Tokyo (UTC+9)
        ASSERT_EQ_INT(6, CalculateEquivalentHour(22, 1, 9));
    }
    TEST_CASE_END();
}
static uint64_t PurgeDirectoryFiles(std::wstring const& dir_path)

{

    uint64_t bytes_freed = 0;

    try

    {

        std::filesystem::path p(dir_path);

        if (std::filesystem::exists(p) && std::filesystem::is_directory(p))

        {

            for (auto const& entry : std::filesystem::directory_iterator(p))

            {

                if (entry.is_regular_file())

                {

                    std::error_code ec;

                    auto sz = entry.file_size(ec);

                    if (!ec) bytes_freed += sz;

                    std::filesystem::remove(entry.path(), ec);

                }

            }

        }

    }

    catch (...) {}

    return bytes_freed;

}





static bool IsAutoStartupEnabled()
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        wchar_t buffer[MAX_PATH] = { 0 };
        DWORD size = sizeof(buffer);
        DWORD type = 0;
        LONG res = RegQueryValueExW(hKey, L"EvolutionWin", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}

static bool SetAutoStartupEnabled(bool enable)
{
    HKEY hKey = nullptr;
    LONG res = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey);
    if (res != ERROR_SUCCESS)
        return false;

    if (enable)
    {
        wchar_t exe_path[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
        std::wstring command = L"\"" + std::wstring{ exe_path } + L"\"";
        res = RegSetValueExW(hKey, L"EvolutionWin", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        res = RegDeleteValueW(hKey, L"EvolutionWin");
        if (res == ERROR_FILE_NOT_FOUND)
            res = ERROR_SUCCESS;
    }
    RegCloseKey(hKey);
    return (res == ERROR_SUCCESS);
}


static bool HasReplyPrefix(std::wstring_view subject)
{
    static const std::vector<std::wstring_view> prefixes = {
        L"re:", L"aw:", L"antw:", L"r:", L"rif:", L"sv:", L"vs:", L"res:"
    };
    for (auto const& p : prefixes)
    {
        if (StartsWithNoCase(subject, p))
            return true;
    }
    return false;
}

static bool HasForwardPrefix(std::wstring_view subject)
{
    static const std::vector<std::wstring_view> prefixes = {
        L"tr:", L"fwd:", L"fw:", L"wg:", L"doorst:", L"i:", L"vb:", L"vl:", L"enc:"
    };
    for (auto const& p : prefixes)
    {
        if (StartsWithNoCase(subject, p))
            return true;
    }
    return false;
}

static std::wstring FormatReplySubject(std::wstring_view subject, std::wstring_view prefix = L"Re: ")
{
    if (HasReplyPrefix(subject))
        return std::wstring{ subject };
    std::wstring p{ prefix.empty() ? L"Re: " : prefix };
    if (p.back() != L' ' && p.back() != L':')
        p += L": ";
    else if (p.back() == L':')
        p += L" ";
    return p + std::wstring{ subject };
}

static std::wstring FormatForwardSubject(std::wstring_view subject, std::wstring_view prefix = L"TR: ")
{
    if (HasForwardPrefix(subject))
        return std::wstring{ subject };
    std::wstring p{ prefix.empty() ? L"TR: " : prefix };
    if (p.back() != L' ' && p.back() != L':')
        p += L": ";
    else if (p.back() == L':')
        p += L" ";
    return p + std::wstring{ subject };
}

static void test_multilingual_subject_prefixes(void)
{
    TEST_CASE("Prefixes: Detection multilingue et formatage reponse / transfert");
    {
        /* Detection des prefixes de reponse multilingues */
        ASSERT_TRUE(HasReplyPrefix(L"Re: Reunion projet"));
        ASSERT_TRUE(HasReplyPrefix(L"RE: Important"));
        ASSERT_TRUE(HasReplyPrefix(L"AW: Projektplan"));      /* Allemand (Antwort) */
        ASSERT_TRUE(HasReplyPrefix(L"Antw: Vraag"));          /* Neerlandais (Antwoord) */
        ASSERT_TRUE(HasReplyPrefix(L"R: Aggiornamento"));     /* Italien (Risposta) */
        ASSERT_TRUE(HasReplyPrefix(L"SV: Status"));           /* Scandinave (Svar) */
        ASSERT_FALSE(HasReplyPrefix(L"Remise des prix"));      /* Faux positif evite */

        /* Detection des prefixes de transfert multilingues */
        ASSERT_TRUE(HasForwardPrefix(L"TR: Document joint"));
        ASSERT_TRUE(HasForwardPrefix(L"Fwd: Monthly report"));
        ASSERT_TRUE(HasForwardPrefix(L"FW: Invoice"));
        ASSERT_TRUE(HasForwardPrefix(L"WG: Unterlagen"));      /* Allemand (Weiterleitung) */
        ASSERT_TRUE(HasForwardPrefix(L"I: Documento"));        /* Italien (Inoltro) */
        ASSERT_TRUE(HasForwardPrefix(L"ENC: Resumen"));        /* Espagnol (Encaminado) */
        ASSERT_FALSE(HasForwardPrefix(L"Travaux en cours"));   /* Faux positif evite */

        /* Formatage intelligent de reponse : ne pas dupliquer si deja present */
        ASSERT_TRUE(FormatReplySubject(L"Re: Reunion projet") == L"Re: Reunion projet");
        ASSERT_TRUE(FormatReplySubject(L"AW: Projektplan") == L"AW: Projektplan");
        ASSERT_TRUE(FormatReplySubject(L"Nouveau sujet") == L"Re: Nouveau sujet");
        ASSERT_TRUE(FormatReplySubject(L"Nouveau sujet", L"AW: ") == L"AW: Nouveau sujet");

        /* Formatage intelligent de transfert */
        ASSERT_TRUE(FormatForwardSubject(L"TR: Document") == L"TR: Document");
        ASSERT_TRUE(FormatForwardSubject(L"Fwd: Document") == L"Fwd: Document");
        ASSERT_TRUE(FormatForwardSubject(L"Bilan annuel") == L"TR: Bilan annuel");
        ASSERT_TRUE(FormatForwardSubject(L"Bilan annuel", L"WG: ") == L"WG: Bilan annuel");
    }
    TEST_CASE_END();
}

static void test_auto_startup_registry(void)
{
    TEST_CASE("AutoStartup: Inscription et desinscription dans HKCU\\...\\Run");
    {
        bool initial_state = IsAutoStartupEnabled();

        /* Test d'activation */
        bool set_ok = SetAutoStartupEnabled(true);
        ASSERT_TRUE(set_ok);
        ASSERT_TRUE(IsAutoStartupEnabled());

        /* Test de desactivation */
        bool unset_ok = SetAutoStartupEnabled(false);
        ASSERT_TRUE(unset_ok);
        ASSERT_FALSE(IsAutoStartupEnabled());

        /* Restauration de l'etat d'origine */
        SetAutoStartupEnabled(initial_state);
        ASSERT_EQ_INT(initial_state ? 1 : 0, IsAutoStartupEnabled() ? 1 : 0);
    }
    TEST_CASE_END();
}

static void test_purge_directory_files(void)

{

    TEST_CASE("PurgeDirectoryFiles: Nettoyage des fichiers temporaires");

    {

        std::filesystem::path test_dir = std::filesystem::temp_directory_path() / L"EvoTestPurge";

        std::filesystem::create_directories(test_dir);



        auto f1 = test_dir / L"file1.tmp";

        auto f2 = test_dir / L"file2.tmp";

        auto f3 = test_dir / L"file3.tmp";



        { std::ofstream out(f1); out << "Contenu 1"; }

        { std::ofstream out(f2); out << "Contenu de test 2 plus long"; }

        { std::ofstream out(f3); out << "Contenu 3"; }



        ASSERT_TRUE(std::filesystem::exists(f1));

        ASSERT_TRUE(std::filesystem::exists(f2));

        ASSERT_TRUE(std::filesystem::exists(f3));



        uint64_t freed = PurgeDirectoryFiles(test_dir.wstring());

        ASSERT_TRUE(freed > 0);

        ASSERT_FALSE(std::filesystem::exists(f1));

        ASSERT_FALSE(std::filesystem::exists(f2));

        ASSERT_FALSE(std::filesystem::exists(f3));



        /* Verification du repertoire dedie au glisser-deposer (Drag & Drop) */
        std::wstring dnd_dir = test_dir.wstring() + L"\\dragdrop\\";
        std::filesystem::create_directories(dnd_dir);
        auto eml_test = std::filesystem::path(dnd_dir) / L"message_export.eml";
        { std::ofstream out(eml_test); out << "From: me@example.com\r\nSubject: Test EML\r\n\r\nCorps"; }
        ASSERT_TRUE(std::filesystem::exists(eml_test));
        uint64_t dnd_freed = PurgeDirectoryFiles(dnd_dir);
        ASSERT_TRUE(dnd_freed > 0);
        ASSERT_FALSE(std::filesystem::exists(eml_test));

        std::filesystem::remove_all(test_dir);

    }

    TEST_CASE_END();

}




struct CategoryDefinition
{
    std::wstring name;
    uint8_t r{ 0 };
    uint8_t g{ 0 };
    uint8_t b{ 0 };
    std::wstring shortcut;
};

static std::vector<CategoryDefinition> GetDefaultCategories()
{
    return {
        { L"Catégorie rouge",    232, 17,  35,  L"Ctrl+F2" },
        { L"Catégorie orange",   255, 140, 0,   L"Ctrl+F3" },
        { L"Catégorie jaune",    255, 185, 0,   L"Ctrl+F4" },
        { L"Catégorie verte",    16,  124, 65,  L"Ctrl+F5" },
        { L"Catégorie bleue",    0,   120, 212, L"Ctrl+F6" },
        { L"Catégorie violette", 136, 23,  152, L"Ctrl+F7" },
        { L"Important",          209, 52,  56,  L"" },
        { L"Travail",            0,   99,  177, L"" },
        { L"Personnel",          0,   130, 114, L"" },
        { L"À suivre",           227, 0,   140, L"" }
    };
}

static bool GetCategoryColor(std::wstring_view category_name, uint8_t& out_r, uint8_t& out_g, uint8_t& out_b)
{
    auto cats = GetDefaultCategories();
    for (auto const& c : cats)
    {
        if (c.name.size() == category_name.size())
        {
            bool eq = true;
            for (size_t i = 0; i < c.name.size(); ++i)
            {
                if (std::towlower(c.name[i]) != std::towlower(category_name[i]))
                {
                    eq = false;
                    break;
                }
            }
            if (eq)
            {
                out_r = c.r; out_g = c.g; out_b = c.b;
                return true;
            }
        }
    }

    uint32_t hash = 5381;
    for (wchar_t ch : category_name)
        hash = ((hash << 5) + hash) + ch;

    static const uint8_t fallback_palette[][3] = {
        { 0, 120, 212 }, { 16, 124, 65 }, { 255, 140, 0 }, { 232, 17, 35 },
        { 136, 23, 152 }, { 0, 153, 188 }, { 135, 100, 184 }, { 180, 0, 158 }
    };
    size_t idx = hash % (sizeof(fallback_palette) / sizeof(fallback_palette[0]));
    out_r = fallback_palette[idx][0];
    out_g = fallback_palette[idx][1];
    out_b = fallback_palette[idx][2];
    return false;
}

static void test_category_definitions_and_colors(void)
{
    TEST_CASE("Categories: Definitions standards et calcul des couleurs");
    {
        auto cats = GetDefaultCategories();
        ASSERT_TRUE(cats.size() >= 10);

        uint8_t r = 0, g = 0, b = 0;
        bool found = GetCategoryColor(L"Catégorie rouge", r, g, b);
        ASSERT_TRUE(found);
        ASSERT_EQ_INT(232, r);
        ASSERT_EQ_INT(17, g);
        ASSERT_EQ_INT(35, b);

        found = GetCategoryColor(L"catégorie rouge", r, g, b);
        ASSERT_TRUE(found);

        found = GetCategoryColor(L"Travail", r, g, b);
        ASSERT_TRUE(found);
        ASSERT_EQ_INT(0, r);
        ASSERT_EQ_INT(99, g);
        ASSERT_EQ_INT(177, b);

        found = GetCategoryColor(L"Projet Inconnu 123", r, g, b);
        ASSERT_FALSE(found);
        ASSERT_TRUE(r != 0 || g != 0 || b != 0);
    }
    TEST_CASE_END();
}


static size_t FindInRawMime(std::wstring_view raw_content, std::wstring_view needle)
{
    if (needle.empty() || raw_content.empty())
        return std::wstring::npos;

    std::wstring haystack_lower{ raw_content };
    for (auto& ch : haystack_lower) ch = static_cast<wchar_t>(std::towlower(ch));

    std::wstring needle_lower{ needle };
    for (auto& ch : needle_lower) ch = static_cast<wchar_t>(std::towlower(ch));

    return haystack_lower.find(needle_lower);
}

static void test_raw_mime_search(void)
{
    TEST_CASE("RawMime: Recherche d'en-tetes et frontieres MIME RFC 822");
    {
        std::wstring raw = L"From: alice@example.com\r\nTo: bob@example.com\r\nSubject: Test MIME\r\nContent-Type: multipart/mixed; boundary=\"--boundary-123\"\r\n\r\n----boundary-123\r\nContent-Type: text/plain\r\n\r\nHello World\r\n";

        size_t pos_sub = FindInRawMime(raw, L"subject:");
        ASSERT_TRUE(pos_sub != std::wstring::npos);

        size_t pos_sub_case = FindInRawMime(raw, L"SUBJECT: TEST");
        ASSERT_TRUE(pos_sub_case != std::wstring::npos);

        size_t pos_bound = FindInRawMime(raw, L"--boundary-123");
        ASSERT_TRUE(pos_bound != std::wstring::npos);

        size_t pos_not_found = FindInRawMime(raw, L"X-Custom-Header: None");
        ASSERT_TRUE(pos_not_found == std::wstring::npos);
    }
    TEST_CASE_END();
}

struct ShortcutItem
{
    std::wstring category;
    std::wstring action;
    std::wstring keys_outlook;
    std::wstring keys_evolution;
};

static std::vector<ShortcutItem> GetKeyboardShortcuts()
{
    return {
        { L"Navigation", L"Aller au module Courrier", L"Ctrl+Alt+1", L"Ctrl+1" },
        { L"Navigation", L"Aller au module Calendrier", L"Ctrl+Alt+2", L"Ctrl+2" },
        { L"Navigation", L"Aller au module Contacts", L"Ctrl+Alt+3", L"Ctrl+3" },
        { L"Navigation", L"Aller au module Tâches", L"Ctrl+Alt+4", L"Ctrl+4" },
        { L"Navigation", L"Rechercher dans les éléments", L"Ctrl+E", L"Ctrl+F" },
        { L"Navigation", L"Afficher les raccourcis clavier", L"F1", L"F1" },

        { L"Courrier", L"Nouveau message", L"Ctrl+N", L"Ctrl+N" },
        { L"Courrier", L"Répondre à l'expéditeur", L"Ctrl+R", L"Ctrl+R" },
        { L"Courrier", L"Répondre à tous les destinataires", L"Ctrl+Maj+R", L"Ctrl+Maj+R" },
        { L"Courrier", L"Transférer le message", L"Ctrl+F", L"Ctrl+J" },
        { L"Courrier", L"Transférer en pièce jointe (.eml)", L"Ctrl+Maj+F", L"Ctrl+Alt+F" },
        { L"Courrier", L"Enregistrer sous le format .eml", L"Ctrl+S", L"Ctrl+S" },
        { L"Courrier", L"Afficher la source brute MIME (RFC 822)", L"Ctrl+Maj+U", L"Ctrl+U" },
        { L"Courrier", L"Attribuer rapidement une catégorie", L"Ctrl+F2", L"Ctrl+F2" },
        { L"Courrier", L"Marquer comme lu", L"Ctrl+Q", L"Ctrl+K" },
        { L"Courrier", L"Marquer comme non lu", L"Ctrl+U", L"Ctrl+Maj+K" },
        { L"Courrier", L"Envoyer et recevoir (synchronisation)", L"F9", L"F9" },
        { L"Courrier", L"Déplacer vers la Corbeille", L"Suppr", L"Suppr" },
        { L"Courrier", L"Supprimer définitivement", L"Maj+Suppr", L"Maj+Suppr" },
        { L"Courrier", L"Archiver le message sélectionné", L"Retour arrière", L"Ctrl+Alt+A" },
        { L"Courrier", L"Imprimer le message", L"Ctrl+P", L"Ctrl+P" },

        { L"Calendrier", L"Nouveau rendez-vous / événement", L"Ctrl+N", L"Ctrl+Maj+E" },
        { L"Calendrier", L"Atteindre une date précise", L"Ctrl+G", L"Ctrl+T" },
        { L"Calendrier", L"Revenir à la date du jour", L"Ctrl+T", L"Ctrl+J" },
        { L"Calendrier", L"Actualiser le calendrier", L"F5", L"F5" },

        { L"Contacts", L"Nouveau contact", L"Ctrl+N", L"Ctrl+Maj+C" },
        { L"Contacts", L"Écrire au contact sélectionné", L"Ctrl+Maj+C", L"Ctrl+N" },

        { L"Tâches", L"Nouvelle tâche", L"Ctrl+Maj+K", L"Ctrl+Maj+T" },
        { L"Tâches", L"Afficher / masquer le volet des tâches", L"Alt+F2", L"F7" }
    };
}

static std::vector<ShortcutItem> FilterKeyboardShortcuts(std::vector<ShortcutItem> const& all, std::wstring_view query)
{
    if (query.empty())
        return all;

    std::wstring q_lower{ query };
    for (auto& ch : q_lower) ch = static_cast<wchar_t>(std::towlower(ch));

    std::vector<ShortcutItem> result;
    for (auto const& item : all)
    {
        auto match = [&](std::wstring const& str) {
            std::wstring s_lower{ str };
            for (auto& ch : s_lower) ch = static_cast<wchar_t>(std::towlower(ch));
            return s_lower.find(q_lower) != std::wstring::npos;
        };

        if (match(item.category) || match(item.action) || match(item.keys_outlook) || match(item.keys_evolution))
        {
            result.push_back(item);
        }
    }
    return result;
}

static std::string WstringToNarrow(std::wstring const& wstr)
{
    std::string res;
    for (wchar_t c : wstr) res.push_back(static_cast<char>(c));
    return res;
}

static void test_keyboard_shortcuts_helpers(void)
{
    TEST_CASE("Shortcuts: Liste complete et recherche multi-criteres");
    {
        auto all = GetKeyboardShortcuts();
        ASSERT_TRUE(all.size() >= 20);

        /* Recherche globale F1 */
        auto f1_list = FilterKeyboardShortcuts(all, L"F1");
        ASSERT_TRUE(f1_list.size() >= 1);
        std::string cat_str = WstringToNarrow(f1_list[0].category);
        ASSERT_EQ_STR("Navigation", cat_str.c_str());

        /* Recherche par categorie "Courrier" */
        auto mail_list = FilterKeyboardShortcuts(all, L"courrier");
        ASSERT_TRUE(mail_list.size() >= 10);

        /* Recherche par combinaison de touches "Ctrl+S" */
        auto save_list = FilterKeyboardShortcuts(all, L"Ctrl+S");
        ASSERT_TRUE(save_list.size() >= 1);
        std::string save_action = WstringToNarrow(save_list[0].action);
        ASSERT_EQ_STR("Enregistrer sous le format .eml", save_action.c_str());

        /* Verification de la divergence de profil Outlook vs Evolution pour Transferer */
        auto forward_list = FilterKeyboardShortcuts(all, L"Transférer");
        ASSERT_TRUE(forward_list.size() >= 1);
        std::string fwd_outlook = WstringToNarrow(forward_list[0].keys_outlook);
        std::string fwd_evo = WstringToNarrow(forward_list[0].keys_evolution);
        ASSERT_EQ_STR("Ctrl+F", fwd_outlook.c_str());
        ASSERT_EQ_STR("Ctrl+J", fwd_evo.c_str());

        /* Recherche infructueuse */
        auto empty_list = FilterKeyboardShortcuts(all, L"Inconnu_XYZ_999");
        ASSERT_TRUE(empty_list.empty());
    }
    TEST_CASE_END();
}


static bool InitializeJobObjectTest()
{
    static HANDLE s_hJobObject = NULL;
    if (!s_hJobObject)
    {
        s_hJobObject = CreateJobObjectW(NULL, NULL);
        if (s_hJobObject)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
            jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
            if (SetInformationJobObject(s_hJobObject, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli)))
            {
                AssignProcessToJobObject(s_hJobObject, GetCurrentProcess());
                return true;
            }
        }
    }
    return s_hJobObject != NULL;
}

static bool IsSatelliteProcessNameTest(std::wstring_view exe_name)
{
    std::wstring name_lower{ exe_name };
    for (auto& ch : name_lower) ch = static_cast<wchar_t>(std::towlower(ch));

    static const std::wstring_view s_satellites[] = {
        L"evolution-source-registry.exe",
        L"evolution-addressbook-factory.exe",
        L"evolution-calendar-factory.exe",
        L"camel-index-control.exe",
        L"gdbus.exe",
        L"gspawn-win64-helper.exe"
    };

    for (auto const& sat : s_satellites)
    {
        if (name_lower == sat)
            return true;
    }
    return false;
}

static void test_process_lifecycle_job_object()
{
    TEST_CASE("ProcessLifecycle: Encapsulation Job Object et satellites");
    {
        ASSERT_TRUE(InitializeJobObjectTest());
        ASSERT_TRUE(IsSatelliteProcessNameTest(L"evolution-source-registry.exe"));
        ASSERT_TRUE(IsSatelliteProcessNameTest(L"EVOLUTION-SOURCE-REGISTRY.EXE"));
        ASSERT_TRUE(IsSatelliteProcessNameTest(L"evolution-calendar-factory.exe"));
        ASSERT_TRUE(IsSatelliteProcessNameTest(L"evolution-addressbook-factory.exe"));
        ASSERT_TRUE(IsSatelliteProcessNameTest(L"gdbus.exe"));
        ASSERT_TRUE(IsSatelliteProcessNameTest(L"camel-index-control.exe"));
        ASSERT_FALSE(IsSatelliteProcessNameTest(L"explorer.exe"));
        ASSERT_FALSE(IsSatelliteProcessNameTest(L"notepad.exe"));
        ASSERT_FALSE(IsSatelliteProcessNameTest(L"EvolutionWin.exe"));
    }
    TEST_CASE_END();
}


static double ClampZoomFactorTest(double factor)
{
    if (factor < 0.5) return 0.5;
    if (factor > 3.0) return 3.0;
    return factor;
}

static double StepZoomInTest(double current)
{
    return ClampZoomFactorTest(std::round((current + 0.10) * 100.0) / 100.0);
}

static double StepZoomOutTest(double current)
{
    return ClampZoomFactorTest(std::round((current - 0.10) * 100.0) / 100.0);
}

static std::wstring FormatZoomPercentageTest(double factor)
{
    int percent = static_cast<int>(std::round(ClampZoomFactorTest(factor) * 100.0));
    return std::to_wstring(percent) + L" %";
}

static void test_reading_zoom_helpers()
{
    TEST_CASE("ReadingZoom: Calculs de paliers et bornes min/max");
    {
        // Bornes
        ASSERT_TRUE(ClampZoomFactorTest(0.2) == 0.5);
        ASSERT_TRUE(ClampZoomFactorTest(4.5) == 3.0);
        ASSERT_TRUE(ClampZoomFactorTest(1.25) == 1.25);

        // Paliers
        ASSERT_TRUE(StepZoomInTest(1.0) == 1.10);
        ASSERT_TRUE(StepZoomInTest(2.95) == 3.0);
        ASSERT_TRUE(StepZoomOutTest(1.0) == 0.90);
        ASSERT_TRUE(StepZoomOutTest(0.55) == 0.50);

        // Formatage pourcentage
        ASSERT_TRUE(FormatZoomPercentageTest(1.0) == L"100 %");
        ASSERT_TRUE(FormatZoomPercentageTest(1.5) == L"150 %");
        ASSERT_TRUE(FormatZoomPercentageTest(0.75) == L"75 %");
    }
    TEST_CASE_END();
}

static double FontSizePresetToZoomTest(std::wstring_view name)
{
    if (name == L"small") return 0.85;
    if (name == L"large") return 1.25;
    if (name == L"extralarge") return 1.5;
    return 1.0;
}

static void test_font_size_preset_to_zoom()
{
    TEST_CASE("FontSize: Correspondance taille de texte de lecture / zoom du lecteur");
    {
        ASSERT_TRUE(FontSizePresetToZoomTest(L"small") == 0.85);
        ASSERT_TRUE(FontSizePresetToZoomTest(L"normal") == 1.0);
        ASSERT_TRUE(FontSizePresetToZoomTest(L"large") == 1.25);
        ASSERT_TRUE(FontSizePresetToZoomTest(L"extralarge") == 1.5);
        ASSERT_TRUE(FontSizePresetToZoomTest(L"inconnu") == 1.0);
        ASSERT_TRUE(FontSizePresetToZoomTest(L"") == 1.0);
    }
    TEST_CASE_END();
}

enum class MessageImportanceLevelTest
{
    Normal = 0,
    High = 1,
    Low = 2
};

static MessageImportanceLevelTest ParseMessageImportanceTest(std::wstring_view importance, double priority = 3.0)
{
    if (importance == L"high" || importance == L"urgent" || (priority > 0 && priority < 3.0))
        return MessageImportanceLevelTest::High;
    if (importance == L"low" || priority > 3.0)
        return MessageImportanceLevelTest::Low;
    return MessageImportanceLevelTest::Normal;
}

static bool MatchCalendarEventTest(std::wstring_view summary, std::wstring_view location, std::wstring_view description, std::wstring_view query)
{
    if (query.empty())
        return true;

    auto to_lower = [](std::wstring_view s) {
        std::wstring res{ s };
        if (!res.empty())
            ::CharLowerBuffW(res.data(), static_cast<DWORD>(res.size()));
        return res;
    };

    std::wstring q = to_lower(query);
    if (to_lower(summary).find(q) != std::wstring::npos)
        return true;
    if (to_lower(location).find(q) != std::wstring::npos)
        return true;
    if (to_lower(description).find(q) != std::wstring::npos)
        return true;

    return false;
}

static std::wstring FormatFileSizeTest(int64_t bytes)
{
    if (bytes <= 0)
        return L"";
    wchar_t buf[64] = { 0 };
    if (::StrFormatByteSizeW(bytes, buf, static_cast<UINT>(std::size(buf))))
        return std::wstring{ buf };
    if (bytes >= 1024 * 1024)
        return std::to_wstring(bytes / (1024 * 1024)) + L" Mo";
    if (bytes >= 1024)
        return std::to_wstring(bytes / 1024) + L" Ko";
    return std::to_wstring(bytes) + L" o";
}

static void test_localized_file_size_and_dates()
{
    TEST_CASE("Localization: Formatage de taille et de dates localisees");
    {
        ASSERT_TRUE(FormatFileSizeTest(0).empty());
        ASSERT_TRUE(FormatFileSizeTest(-10).empty());

        std::wstring s_kb = FormatFileSizeTest(1024);
        ASSERT_TRUE(!s_kb.empty());
        ASSERT_TRUE(s_kb.find(L"Ko") != std::wstring::npos || s_kb.find(L"KB") != std::wstring::npos);

        std::wstring s_mb = FormatFileSizeTest(1500000);
        ASSERT_TRUE(!s_mb.empty());
        ASSERT_TRUE(s_mb.find(L"Mo") != std::wstring::npos || s_mb.find(L"MB") != std::wstring::npos);

        std::wstring s_gb = FormatFileSizeTest(2147483648LL);
        ASSERT_TRUE(!s_gb.empty());
        ASSERT_TRUE(s_gb.find(L"Go") != std::wstring::npos || s_gb.find(L"GB") != std::wstring::npos);
    }
    TEST_CASE_END();
}

static void test_calendar_search_matching()
{
    TEST_CASE("Calendar: Filtrage recherche titre, lieu et description");
    {
        std::wstring summary = L"Réunion de cadrage trimestriel";
        std::wstring location = L"Salle Concorde A";
        std::wstring desc = L"Présentation des résultats et roadmap Q4";

        ASSERT_TRUE(MatchCalendarEventTest(summary, location, desc, L""));
        ASSERT_TRUE(MatchCalendarEventTest(summary, location, desc, L"cadrage"));
        ASSERT_TRUE(MatchCalendarEventTest(summary, location, desc, L"RÉUNION"));
        ASSERT_TRUE(MatchCalendarEventTest(summary, location, desc, L"concorde"));
        ASSERT_TRUE(MatchCalendarEventTest(summary, location, desc, L"roadmap"));
        ASSERT_TRUE(MatchCalendarEventTest(summary, location, desc, L"Q4"));
        ASSERT_FALSE(MatchCalendarEventTest(summary, location, desc, L"budget"));
        ASSERT_FALSE(MatchCalendarEventTest(summary, location, desc, L"Zénith"));
    }
    TEST_CASE_END();
}

static void test_message_importance_parsing()
{
    TEST_CASE("MessageImportance: Evaluation haute, basse et normale");
    {
        // En-têtes textuels
        ASSERT_TRUE(ParseMessageImportanceTest(L"high") == MessageImportanceLevelTest::High);
        ASSERT_TRUE(ParseMessageImportanceTest(L"urgent") == MessageImportanceLevelTest::High);
        ASSERT_TRUE(ParseMessageImportanceTest(L"low") == MessageImportanceLevelTest::Low);
        ASSERT_TRUE(ParseMessageImportanceTest(L"normal") == MessageImportanceLevelTest::Normal);
        ASSERT_TRUE(ParseMessageImportanceTest(L"") == MessageImportanceLevelTest::Normal);

        // Priorités numériques RFC (1: haute, 3: normale, 5: basse)
        ASSERT_TRUE(ParseMessageImportanceTest(L"", 1.0) == MessageImportanceLevelTest::High);
        ASSERT_TRUE(ParseMessageImportanceTest(L"", 2.0) == MessageImportanceLevelTest::High);
        ASSERT_TRUE(ParseMessageImportanceTest(L"", 3.0) == MessageImportanceLevelTest::Normal);
        ASSERT_TRUE(ParseMessageImportanceTest(L"", 4.0) == MessageImportanceLevelTest::Low);
        ASSERT_TRUE(ParseMessageImportanceTest(L"", 5.0) == MessageImportanceLevelTest::Low);
    }
    TEST_CASE_END();
}

#include "../../winui/app/UpdateIntegrity.h"

static std::wstring ToLowerCopyForTest(std::wstring text)
{
    for (auto& c : text)
        c = static_cast<wchar_t>(std::towlower(c));
    return text;
}

static void test_update_integrity(void)
{
    using namespace EvolutionWin::UpdateIntegrity;
    const std::wstring h1 = L"0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789abcdef";
    const std::wstring h2 = L"fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";

    TEST_CASE("UpdateIntegrity: IsHexSha256 valide la longueur et les chiffres");
    {
        ASSERT_TRUE(IsHexSha256(h1));
        ASSERT_TRUE(!IsHexSha256(L"abc"));
        ASSERT_TRUE(!IsHexSha256(h1.substr(0, 63) + L"g"));
    }
    TEST_CASE_END();

    TEST_CASE("UpdateIntegrity: ParseSha256File au format sha256sum");
    {
        std::wstring file = h1 + L" *Evolution-0.9.8-x64.msi\n";
        ASSERT_TRUE(ParseSha256File(file, L"Evolution-0.9.8-x64.msi") == ToLowerCopyForTest(h1));
        ASSERT_TRUE(ParseSha256File(file, L"evolution-0.9.8-X64.MSI") == ToLowerCopyForTest(h1));
    }
    TEST_CASE_END();

    TEST_CASE("UpdateIntegrity: plusieurs lignes, nom absent ou fichier corrompu");
    {
        std::wstring two = h2 + L"  autre.msi\r\n" + h1 + L" *Evolution.msi\r\n";
        ASSERT_TRUE(ParseSha256File(two, L"Evolution.msi") == ToLowerCopyForTest(h1));
        ASSERT_TRUE(ParseSha256File(two, L"inconnu.msi").empty());
        ASSERT_TRUE(ParseSha256File(h2 + L"\n", L"quelconque.msi") == h2);
        ASSERT_TRUE(ParseSha256File(L"<html>404</html>", L"x.msi").empty());
        ASSERT_TRUE(ParseSha256File(L"", L"x.msi").empty());
    }
    TEST_CASE_END();

    TEST_CASE("UpdateIntegrity: ToHex");
    {
        const unsigned char bytes[] = { 0x00, 0xAB, 0xFF };
        ASSERT_TRUE(ToHex(bytes, 3) == L"00abff");
    }
    TEST_CASE_END();
}

int main(void)







{







    TEST_SUITE_BEGIN("WinUI Helpers & Algorithmes (MainWindow.Helpers)");







    test_sender_name_parsing();







    test_sanitize_filename();







    test_without_address();







    test_escape_html();







    test_quote();







    test_starts_with_nocase();







    test_semver_comparison();







    test_attachment_keywords();







    test_build_standalone_html_message();







    test_extract_plain_text_for_speech();



    test_attachment_preview_kind();

    test_format_contact_suggestion();

    test_calendar_time_scale();

    test_purge_directory_files();
    test_auto_startup_registry();
    test_multilingual_subject_prefixes();
    test_second_timezone_helpers();
    test_message_category_search();











        test_category_definitions_and_colors();
        test_raw_mime_search();
        
        test_keyboard_shortcuts_helpers();
        test_process_lifecycle_job_object();
        test_reading_zoom_helpers();
    test_message_importance_parsing();
    test_calendar_search_matching();
    test_localized_file_size_and_dates();
        test_font_size_preset_to_zoom();
    test_update_integrity();
return test_suite_summary();







}







