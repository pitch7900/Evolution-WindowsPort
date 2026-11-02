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



        std::filesystem::remove_all(test_dir);

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







    return test_suite_summary();







}







