<#
  Publie une version d'EvolutionWin dans les Releases GitHub. Lancé par
  publish-release.bat, après build-msi.bat.

  1. Vérifications : installeur dist\Evolution-<version>-x64.msi présent,
     section « ## [<version>] — <date> » datée dans CHANGELOG.md (pas
     « en cours »), aucune Release existante pour l'étiquette v<version>.
  2. Étiquette annotée v<version> (créée sur le commit courant si elle
     n'existe pas encore, puis poussée), après contrôle du fichier VERSION.
  3. Empreinte SHA-256 du MSI (fichier .sha256, format sha256sum).
  4. Release « Evolution <version> » : notes reprises du CHANGELOG, MSI et
     .sha256 joints ; préversion si demandé ou si la version a un suffixe
     (0.9.4-beta1).

  Jeton GitHub : GH_TOKEN ou GITHUB_TOKEN, sinon celui du gestionnaire
  d'identifiants de Git (git credential fill), avec le droit « repo ».
#>
param(
	[string] $Version,
	[string] $Repository = 'pitch7900/Evolution-WindowsPort',
	[switch] $PreRelease,
	[switch] $DryRun
)

$ErrorActionPreference = 'Stop'
# accents des messages d'étiquette transmis à git (PowerShell 5.1 : ASCII par défaut)
$OutputEncoding = New-Object Text.UTF8Encoding $false

$Workspace = $env:EVO_WORKSPACE
$Dist      = Join-Path $Workspace 'dist'

# version par défaut : fichier VERSION à la racine du dépôt
$FileVersion = (Get-Content (Join-Path $Workspace 'VERSION') -TotalCount 1).Trim()
if (-not $Version) {
	$Version = $FileVersion
}
if ($Version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$') {
	throw "Version non valide : « $Version » (attendu : x.y.z ou x.y.z-suffixe)"
}
if ($Version -match '-') {
	$PreRelease = $true
}
$Tag = "v$Version"

# --- 1. vérifications ---------------------------------------------------------

$Msi = Join-Path $Dist "Evolution-$Version-x64.msi"
if (-not (Test-Path $Msi)) {
	throw "Installeur introuvable : $Msi (lancer d'abord tools\build-msi.bat)"
}

# notes de version : section du CHANGELOG jusqu'à la suivante
$Changelog = Get-Content (Join-Path $Workspace 'CHANGELOG.md') -Encoding UTF8
$Start = -1
for ($i = 0; $i -lt $Changelog.Count; $i++) {
	if ($Changelog[$i] -match "^## \[$([regex]::Escape($Version))\]") {
		$Start = $i
		break
	}
}
if ($Start -lt 0) {
	throw "CHANGELOG.md ne contient pas de section pour la version $Version"
}
if ($Changelog[$Start] -match 'en cours') {
	throw "La section $Version du CHANGELOG est encore « en cours » : la dater avant de publier"
}
$End = $Changelog.Count
for ($i = $Start + 1; $i -lt $Changelog.Count; $i++) {
	if ($Changelog[$i] -match '^## \[') {
		$End = $i
		break
	}
}
$Notes = ($Changelog[($Start + 1)..($End - 1)] -join "`n").Trim()

# jeton GitHub
$Token = if ($env:GH_TOKEN) { $env:GH_TOKEN } else { $env:GITHUB_TOKEN }
if (-not $Token) {
	$Credential = "protocol=https`nhost=github.com`n`n" | git credential fill 2>$null
	$Line = $Credential | Where-Object { $_ -like 'password=*' } | Select-Object -First 1
	if ($Line) {
		$Token = $Line.Substring('password='.Length)
	}
}
if (-not $Token) {
	throw 'Aucun jeton GitHub : définir GH_TOKEN ou se connecter à GitHub avec Git'
}

$Api = "https://api.github.com/repos/$Repository"
$Headers = @{
	Authorization          = "Bearer $Token"
	Accept                 = 'application/vnd.github+json'
	'X-GitHub-Api-Version' = '2022-11-28'
}

# la Release ne doit pas déjà exister
$Existing = $null
try {
	$Existing = Invoke-RestMethod -Uri "$Api/releases/tags/$Tag" -Headers $Headers
} catch {
	# 404 attendu ; toute autre erreur (jeton, réseau) arrête la publication
	if (-not $_.Exception.Response -or [int] $_.Exception.Response.StatusCode -ne 404) {
		throw
	}
}
if ($Existing) {
	throw "La Release $Tag existe déjà : $($Existing.html_url)"
}

# --- 2. étiquette ---------------------------------------------------------------

git -C $Workspace rev-parse -q --verify "refs/tags/$Tag" *> $null
$TagExists = $LASTEXITCODE -eq 0

if (-not $TagExists) {
	if ($FileVersion -ne $Version) {
		throw "Le fichier VERSION ($FileVersion) ne correspond pas à $Version : étiquette non créée"
	}
	$Status = git -C $Workspace status --porcelain --untracked-files=no
	if ($Status) {
		throw "Modifications non commitées : commiter avant de créer l'étiquette $Tag"
	}
}

Write-Host "Version      : $Version$(if ($PreRelease) { ' (préversion)' })"
Write-Host "Étiquette    : $Tag $(if ($TagExists) { '(existante)' } else { '(à créer sur ' + (git -C $Workspace rev-parse --short HEAD) + ')' })"
Write-Host "Installeur   : $Msi"

if ($DryRun) {
	Write-Host "`nNotes :`n$Notes"
	Write-Host "`n-DryRun : rien n'a été publié."
	return
}

if (-not $TagExists) {
	$TagMessage = "Evolution $Version`n`n$Notes"
	$TagMessage | git -C $Workspace tag -a $Tag -F -
	if ($LASTEXITCODE -ne 0) { throw "Création de l'étiquette $Tag impossible" }
}
git -C $Workspace push origin "refs/tags/$Tag"
if ($LASTEXITCODE -ne 0) { throw "Envoi de l'étiquette $Tag impossible" }

# --- 3. empreinte ---------------------------------------------------------------

$MsiName = Split-Path $Msi -Leaf
$Hash = (Get-FileHash $Msi -Algorithm SHA256).Hash.ToLowerInvariant()
$HashFile = "$Msi.sha256"
[IO.File]::WriteAllText($HashFile, "$Hash *$MsiName`n", (New-Object Text.UTF8Encoding $false))
Write-Host "SHA-256      : $Hash"

# --- 4. Release et fichiers joints --------------------------------------------

$Body = @{
	tag_name   = $Tag
	name       = "Evolution $Version"
	body       = "$Notes`n`n---`n`n**Installation** : ``msiexec /i $MsiName`` (Windows 11 x64, installation par machine).`n`nSHA-256 de ``$MsiName`` : ``$Hash``"
	draft      = $false
	prerelease = [bool] $PreRelease
} | ConvertTo-Json
$Release = Invoke-RestMethod -Method Post -Uri "$Api/releases" -Headers $Headers `
	-ContentType 'application/json; charset=utf-8' -Body ([Text.Encoding]::UTF8.GetBytes($Body))

$UploadUrl = $Release.upload_url -replace '\{.*\}$', ''
foreach ($Asset in @(@($Msi, 'application/x-msi'), @($HashFile, 'text/plain'))) {
	$Name = Split-Path $Asset[0] -Leaf
	Write-Host "Envoi        : $Name"
	Invoke-RestMethod -Method Post -Uri "$UploadUrl`?name=$([uri]::EscapeDataString($Name))" -Headers $Headers `
		-ContentType $Asset[1] -InFile $Asset[0] -TimeoutSec 1800 | Out-Null
}

Write-Host "`nRelease publiée : $($Release.html_url)"
