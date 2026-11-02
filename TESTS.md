<!-- cspell:disable -->
# Guide des tests unitaires et de non-régression

Ce document détaille l'organisation, l'exécution et l'extension du jeu de tests unitaires d'**Evolution-WindowsPort**, destiné à valider la non-régression entre chaque version sous Windows 11 avec MSVC.

---

## 1. Philosophie et bonnes pratiques

Le jeu de tests a été conçu selon les règles de l'art pour les projets natifs C / C++ sous Windows :

1. **Autonomie et zéro dépendance externe** : aucun téléchargement lourd (GoogleTest, Boost) n'est nécessaire. Un micro-cadre d'en-tête léger ([`tests/include/test_framework.h`](./tests/include/test_framework.h)) fournit des assertions claires (`ASSERT_TRUE`, `ASSERT_EQ_STR`, etc.) et compile instantanément avec le compilateur MSVC natif (`cl.exe`).
2. **Déterminisme et isolation** : les tests unitaires s'exécutent entièrement en local, sans connexion réseau, sans nécessiter de serveur de messagerie externe ni de modifier les données utilisateur (`%LOCALAPPDATA%`).
3. **Automatisation CI/CD et pré-publication** : le script de test retourne un code de sortie standardisé (`0` en cas de succès, `1` en cas d'échec), facilitant son intégration dans GitHub Actions, les hooks de pré-commit ou avant la publication d'une Release (`tools\publish-release.bat`).
4. **Vérification de l'ABI binaire** : un test d'export dynamique vérifie avec `dumpbin` qu'aucun symbole public de la bibliothèque partagée `evo-core.dll` n'a été omis ou cassé lors de la compilation.

---

## 2. Organisation de l'arborescence des tests

```text
Evolution-WindowsPort/
├── tests/
│   ├── include/
│   │   └── test_framework.h           # Micro-framework d'assertions et de synthèse C/C++
│   ├── unit/
│   │   ├── test_helpers.cpp           # Tests unitaires des algorithmes et utilitaires WinUI
│   │   ├── test_evo_core_contracts.c  # Tests des contrats d'interface JSON et robustesse evo-core
│   │   └── test_settings.cpp          # Tests du gestionnaire de préférences et résilience JSON
│   ├── abi/
│   │   └── test_abi_exports.ps1       # Validation d'export des symboles de evo-core.dll
│   └── CMakeLists.txt                 # Configuration CMake / CTest optionnelle
└── tools/
    └── run-tests.bat                  # Script de compilation et d'exécution unifiée
```

---

## 3. Détail des suites de tests

### Suite 1 : Algorithmes et utilitaires WinUI (`test_helpers.cpp`)
Valide les fonctions pures issues de `MainWindow.Helpers.h` :
- **Extraction des expéditeurs (`SenderName`)** : prise en charge des formats RFC 822 / 5322 (`"Nom <email>"`, `"\"Nom, Prénom\" <email>"`, adresses nues, chaînes vides).
- **Assainissement des noms de fichiers (`SanitizeFileName`)** : remplacement strict des caractères interdits sous Windows (`\ / : * ? " < > |`) et des caractères de contrôle ASCII (< 32), avec repli de secours.
- **Répondre à tous (`WithoutAddress`)** : exclusion de l'adresse de l'utilisateur de la liste des destinataires, insensibilité à la casse, gestion des espaces et des listes multi-adresses.
- **Sécurité HTML (`EscapeHtml`)** : échappement des caractères spéciaux (`&`, `<`, `>`, `"`) et conversion des sauts de ligne CRLF en `<br>` pour prévenir toute corruption ou injection lors des citations.
- **Citations de courriel (`Quote`)** : formatage des blocs de texte avec préfixe `> ` et gestion des fins de ligne CRLF.
- **Comparaison sémantique de versions (`SemVer`)** : garantit une comparaison numérique stricte (`x.y.z`) pour le mécanisme de recherche de mise à jour GitHub, évitant le piège textuel où `"0.9.10"` serait considéré inférieur à `"0.9.9"`.

### Suite 2 : Contrats d'interface et robustesse `evo-core` (`test_evo_core_contracts.c`)
Vérifie la robustesse du moteur C et ses échanges avec l'interface :
- **Découpage des secrets Windows Credential Manager** : validation de l'algorithme de partitionnement et de réassemblage des jetons OAuth2 volumineux (> 2 560 octets), éliminant l'erreur `CredWrite` survenue en v0.9.4 avec Entra ID.
- **Validation syntaxique des adresses** : contrôle du format avant interrogation des résolveurs DNS et Autodiscover.
- **Priorité des dossiers spéciaux** : respect de la classification et de l'ordre d'affichage standardisé d'Outlook (Boîte de réception, Brouillons, Éléments envoyés, Éléments supprimés, Archive, Courrier indésirable, Boîte d'envoi locale).
- **Schémas JSON des événements** : vérification de la présence des champs obligatoires pour les événements `progress`, `connection-status`, `folder-changed` et `new-mail`.

### Suite 3 : Préférences de l'application (`test_settings.cpp`)
Teste la persistance du fichier `settings.json` :
- Relecture avec valeurs par défaut garanties (fallback).
- Modification et persistance des nombres, booléens et chaînes.
- Gestion des tableaux JSON (liste des expéditeurs et domaines de confiance pour le blocage de contenu externe).
- Résilience face aux corruptions syntaxiques d'un fichier JSON mal formé.

### Suite 4 : Intégrité binaire et export ABI (`test_abi_exports.ps1`)
- Extrait dynamiquement toutes les déclarations de fonctions publiques de [`winui/core/evo-core.h`](./winui/core/evo-core.h) (`evo_core_*`).
- Interroge les exports de [`build/evo-core.dll`](./build/evo-core.dll) via `dumpbin /exports`.
- Signale immédiatement toute fonction manquante ou non exportée sous MSVC.

---

## 4. Exécution des tests

### En une commande (recommandé)

Ouvrez une invite de commandes Windows (`cmd.exe`) à la racine du projet et lancez :

```bat
tools\run-tests.bat
```

Ce script :
1. Charge l'environnement du compilateur MSVC via `tools\env.bat`.
2. Compile les exécutables de test dans `build\tests\`.
3. Lance successivement chaque suite de tests avec affichage détaillé.
4. Valide l'export des symboles de la DLL.
5. Retourne `0` si tous les tests ont réussi, ou `1` en cas d'anomalie.

### Via CMake / CTest (optionnel)

Si vous utilisez CMake pour orchestrer vos builds :

```bat
call tools\env.bat
cmake -S tests -B build\intermediate\tests -G Ninja
cmake --build build\intermediate\tests
ctest --test-dir build\intermediate\tests --output-on-failure
```

---

## 5. Comment ajouter un nouveau test

Pour ajouter un test de non-régression lors du développement d'une nouvelle fonctionnalité :

1. Ouvrez la suite concernée dans `tests/unit/` (ou créez-en une nouvelle).
2. Utilisez les macros du framework :
   ```cpp
   TEST_CASE("Nom explicite de la fonctionnalite a valider");
   {
       int resultat = MaFonction(param1, param2);
       ASSERT_EQ_INT(42, resultat);
       ASSERT_TRUE(ConditionVerifiee());
   }
   TEST_CASE_END();
   ```
3. Exécutez `tools\run-tests.bat` pour vérifier que le test passe immédiatement.
