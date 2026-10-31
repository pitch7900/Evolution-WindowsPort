# dist

Binaires packagés destinés à la distribution : installeur MSI autonome
(Windows 11 x64), généré par `tools\build-msi.bat`.

Contrairement à `build/` (sorties de compilation, ignoré par Git), ce
dossier est versionné.

## Versions disponibles

| Version | Date | Installeur | SHA-256 | Notes |
| --- | --- | --- | --- | --- |
| 0.9.1 | 2026-10-31 | [Evolution-0.9.1-x64.msi](https://github.com/pitch7900/Evolution-WindowsPort/raw/main/dist/Evolution-0.9.1-x64.msi) | `98267172992e92359a419719bc5d12e0141de95644b0ca37e528451c2ffd1fef` | [Changements](../CHANGELOG.md#091--2026-10-31) |

La version 0.9.2 est en cours de développement (voir
[`CHANGELOG.md`](../CHANGELOG.md)) et n'a pas encore d'installeur publié.

## Installation

```bat
msiexec /i Evolution-0.9.1-x64.msi          :: installation interactive
msiexec /i Evolution-0.9.1-x64.msi /qn      :: installation silencieuse (par machine)
```

Vérifier l'empreinte du fichier téléchargé (PowerShell) :

```powershell
Get-FileHash .\Evolution-0.9.1-x64.msi -Algorithm SHA256
```
