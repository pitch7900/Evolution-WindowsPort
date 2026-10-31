# Base ISPDB de Thunderbird

Fichiers de configuration des fournisseurs de messagerie (format `clientConfig`
1.1 : serveurs IMAP / SMTP, ports, chiffrement, authentification, OAuth2),
copiés sans modification depuis le répertoire `ispdb/` du dépôt
[thunderbird/autoconfig](https://github.com/thunderbird/autoconfig),
commit `f50a4feb7dd0e0ed76a7ce440d591ac592dcc313`.

Ces fichiers sont distribués sous la **Mozilla Public License 2.0** (voir
[LICENSE](LICENSE)). evo-core les embarque comme ressources (GResource) pour
la détection hors ligne des fournisseurs connus lors de l'ajout d'un compte ;
la même base est interrogée en ligne (`https://autoconfig.thunderbird.net/v1.1/<domaine>`)
pour les domaines absents de cette copie.

Mise à jour (à chaque version) : remplacer les fichiers `*.xml` par ceux du
dépôt et noter le commit ci-dessus.
