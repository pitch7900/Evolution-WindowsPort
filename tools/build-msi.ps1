<#
  Construit l'installeur MSI d'EvolutionWin (phase 5). Lancé par
  build-msi.bat, qui prépare l'environnement (env.bat : PREFIX, BUILD_DIR,
  dumpbin, msbuild...).

  1. Arborescence d'installation dans build\intermediate\msi-stage, au
     format d'un préfixe (EDS, Camel, GLib et libical calculent leurs
     chemins depuis l'emplacement de leurs DLL / de l'exécutable) :
       bin\      EvolutionWin.exe + Windows App SDK autonome, evo-core.dll,
                 DLL du préfixe réellement importées, runtime Visual C++
       libexec\  services EDS (démarrés par evo-core)
       lib\      fournisseurs Camel, backends et modules EDS, modules GIO
       share\    schémas GSettings, fuseaux libical, traductions
  2. DLL du préfixe : parcours des imports PE (dumpbin /dependents) à partir
     des exécutables et des modules ; seules celles réellement importées
     sont copiées.
  3. Signature Authenticode optionnelle (SIGN_CERT).
  4. MSI construit par WiX v5 dans dist\.
#>
param(
	[string] $Version = $env:EVO_MSI_VERSION,
	[switch] $SkipAppBuild
)

$ErrorActionPreference = 'Stop'

# version par défaut : fichier VERSION à la racine du dépôt (cf. CHANGELOG.md)
if (-not $Version) {
	$Version = (Get-Content (Join-Path $env:EVO_WORKSPACE 'VERSION') -TotalCount 1).Trim()
}

$Prefix    = $env:PREFIX
$Workspace = $env:EVO_WORKSPACE
$Tools     = $env:MSVC_SCRIPTS.TrimEnd('\')
$Stage     = Join-Path $env:BUILD_INTERMEDIATE 'msi-stage'
$Dist      = Join-Path $Workspace 'dist'
$PrefixBin = Join-Path $Prefix 'bin'

function Copy-Into([string] $Source, [string] $DestDir) {
	New-Item -ItemType Directory -Force $DestDir | Out-Null
	Copy-Item -Force $Source $DestDir
}

Write-Host "== Arborescence d'installation : $Stage"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
$StageBin = Join-Path $Stage 'bin'
New-Item -ItemType Directory -Force $StageBin | Out-Null

# --- EvolutionWin (Windows App SDK autonome) --------------------------------
if (-not $SkipAppBuild) {
	$App = Join-Path $env:WINUI_SRC 'app'
	$Packages = $env:WINUI_PACKAGES
	if (-not (Test-Path (Join-Path $Packages 'Microsoft.WindowsAppSDK.1.7.260224002'))) {
		& (Join-Path $Tools 'tools\nuget.exe') restore (Join-Path $App 'packages.config') -PackagesDirectory $Packages -NonInteractive
		if ($LASTEXITCODE) { throw 'nuget restore a échoué' }
	}
	& msbuild (Join-Path $App 'EvolutionWin.vcxproj') -nologo -m -v:minimal `
		-p:Configuration=Release -p:Platform=x64 "-p:GtkPrefix=$Prefix" `
		"-p:PackagesDir=$Packages\" "-p:EvoCoreLibDir=$($env:BUILD_INTERMEDIATE)\evo-core\" `
		"-p:SDKReferenceDirectoryRoot=${env:ProgramFiles(x86)}\Microsoft SDKs" `
		"-p:OutDir=$StageBin\" "-p:IntDir=$($env:BUILD_INTERMEDIATE)\winui-app-obj\"
	if ($LASTEXITCODE) { throw 'La compilation d''EvolutionWin a échoué' }
} else {
	# reprend la sortie de build-winui-app.bat, sans la version GTK
	Get-ChildItem $env:BUILD_DIR | Where-Object {
		$_.Name -notmatch '^(intermediate|libexec)$' -and
		$_.Name -notmatch '^(evolution\.exe|evolution-.*\.dll|webkit2gtk-.*\.dll|evo-core-test\.exe)$'
	} | Copy-Item -Recurse -Force -Destination $StageBin
}

# symboles et bibliothèques d'import : inutiles à l'exécution
Get-ChildItem $StageBin -File | Where-Object { $_.Extension -in '.pdb', '.lib', '.exp', '.ilk' } | Remove-Item -Force
if (-not (Test-Path (Join-Path $StageBin 'EvolutionWin.exe'))) { throw 'EvolutionWin.exe absent' }

# evo-core : compilé dans build\intermediate\evo-core (build-winui-core.bat)
Copy-Into (Join-Path $env:BUILD_INTERMEDIATE 'evo-core\evo-core.dll') $StageBin

# --- Services et modules d'EDS ---------------------------------------------
# Exclus car ils entraînent GTK (EvolutionWin n'en a pas besoin) :
# evolution-user-prompter et module-trust-prompt (dialogues GTK d'EDS ;
# EvolutionWin demande mots de passe et certificats lui-même) et le
# fournisseur RSS (greffon d'Evolution, ignoré par evo-core).
$Libexec = Join-Path $Stage 'libexec'
foreach ($exe in 'evolution-source-registry', 'evolution-addressbook-factory',
		'evolution-addressbook-factory-subprocess', 'evolution-calendar-factory',
		'evolution-calendar-factory-subprocess',
		'camel-index-control-1.2', 'camel-gpg-photo-saver') {
	Copy-Into (Join-Path $Prefix "libexec\$exe.exe") $Libexec
}

$ExcludedModules = 'libcamelrss.dll', 'libcamelrss.urls', 'module-trust-prompt.dll'
foreach ($dir in 'addressbook-backends', 'calendar-backends', 'camel-providers', 'registry-modules') {
	$src = Join-Path $Prefix "lib\evolution-data-server\$dir"
	$dst = Join-Path $Stage "lib\evolution-data-server\$dir"
	Get-ChildItem $src -File | Where-Object {
		$_.Extension -in '.dll', '.urls' -and $_.Name -notin $ExcludedModules
	} | ForEach-Object { Copy-Into $_.FullName $dst }
}

# modules GIO : TLS (IMAPS/SMTPS) et proxy système
$GioDst = Join-Path $Stage 'lib\gio\modules'
foreach ($f in 'gioopenssl.dll', 'giognomeproxy.dll', 'giomodule.cache') {
	Copy-Into (Join-Path $Prefix "lib\gio\modules\$f") $GioDst
}

# --- Données ----------------------------------------------------------------
Copy-Into (Join-Path $Prefix 'share\glib-2.0\schemas\gschemas.compiled') (Join-Path $Stage 'share\glib-2.0\schemas')
Copy-Item -Recurse -Force (Join-Path $Prefix 'share\libical') (Join-Path $Stage 'share\libical')

$Domains = 'evolution-data-server.mo', 'evolution-ews.mo', 'glib20.mo', 'glib-networking.mo', 'libsoup-3.0.mo'
Get-ChildItem (Join-Path $Prefix 'share\locale') -Directory | ForEach-Object {
	$lang = $_.Name
	foreach ($mo in $Domains) {
		$src = Join-Path $_.FullName "LC_MESSAGES\$mo"
		if (Test-Path $src) { Copy-Into $src (Join-Path $Stage "share\locale\$lang\LC_MESSAGES") }
	}
}

# --- DLL du préfixe réellement importées (parcours des imports PE) ---------
# chargés dynamiquement, absents des tables d'import : NSS (softokn3,
# freebl3, nssckbi), bus de session et lancement de processus de GLib
$Dynamic = 'softokn3.dll', 'freebl3.dll', 'nssckbi.dll', 'gdbus.exe',
	'gspawn-win64-helper.exe', 'gspawn-win64-helper-console.exe'
foreach ($f in $Dynamic) { Copy-Into (Join-Path $PrefixBin $f) $StageBin }

function Get-PeImports([string] $Path) {
	$names = @()
	$inList = $false
	foreach ($line in (& dumpbin /nologo /dependents $Path)) {
		if ($line -match 'following (delay load )?dependencies') { $inList = $true; continue }
		if ($inList) {
			$t = $line.Trim()
			if ($t -match '\.dll$') { $names += $t }
			elseif ($t -eq '' -and $names.Count) { $inList = $false }
		}
	}
	return $names
}

$queue = [System.Collections.Generic.Queue[string]]::new()
Get-ChildItem $Stage -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' } | ForEach-Object { $queue.Enqueue($_.FullName) }
$seen = @{}
$copied = @()
while ($queue.Count) {
	$file = $queue.Dequeue()
	if ($seen.ContainsKey($file)) { continue }
	$seen[$file] = $true
	foreach ($dll in (Get-PeImports $file)) {
		$inStage = Join-Path $StageBin $dll
		if (Test-Path $inStage) { continue }
		$fromPrefix = Join-Path $PrefixBin $dll
		if (Test-Path $fromPrefix) {
			Copy-Item -Force $fromPrefix $StageBin
			$copied += $dll
			$queue.Enqueue($inStage)
		}
	}
}
Write-Host ("== DLL du préfixe copiées ({0}) : {1}" -f $copied.Count, (($copied | Sort-Object) -join ' '))

# --- Runtime Visual C++ (copie locale, aucun prérequis système) ------------
$Crt = Get-ChildItem (Join-Path $env:VS_INSTALL 'VC\Redist\MSVC') -Directory |
	Where-Object { Test-Path (Join-Path $_.FullName 'x64\Microsoft.VC143.CRT') } |
	Sort-Object { [version] $_.Name } | Select-Object -Last 1
if (-not $Crt) { throw 'Runtime Visual C++ (Microsoft.VC143.CRT) introuvable' }
Get-ChildItem (Join-Path $Crt.FullName 'x64\Microsoft.VC143.CRT') -Filter *.dll | Copy-Item -Force -Destination $StageBin

# --- Signature optionnelle -------------------------------------------------
function Invoke-Sign([string[]] $Files) {
	if (-not $env:SIGN_CERT) { return }
	$ts = if ($env:SIGN_TIMESTAMP_URL) { $env:SIGN_TIMESTAMP_URL } else { 'http://timestamp.digicert.com' }
	$signArgs = @('sign', '/fd', 'SHA256', '/f', $env:SIGN_CERT, '/tr', $ts, '/td', 'SHA256')
	if ($env:SIGN_CERT_PASSWORD) { $signArgs += @('/p', $env:SIGN_CERT_PASSWORD) }
	& signtool @signArgs @Files
	if ($LASTEXITCODE) { throw 'signtool a échoué' }
}
Invoke-Sign @((Join-Path $StageBin 'EvolutionWin.exe'), (Join-Path $StageBin 'evo-core.dll'))

$size = (Get-ChildItem $Stage -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("== Arborescence : {0:N0} fichiers, {1:N1} Mo" -f (Get-ChildItem $Stage -Recurse -File).Count, $size)

# --- MSI (WiX v5) ----------------------------------------------------------
$Wix = Get-Command wix -ErrorAction SilentlyContinue
$WixExe = if ($Wix) { $Wix.Source } else { Join-Path $env:USERPROFILE '.dotnet\tools\wix.exe' }
if (-not (Test-Path $WixExe)) { throw 'WiX introuvable : dotnet tool install --global wix --version 5.0.2' }

New-Item -ItemType Directory -Force $Dist | Out-Null
$Msi = Join-Path $Dist "Evolution-$Version-x64.msi"
& $WixExe build (Join-Path $Tools 'msi\Evolution.wxs') -arch x64 -ext WixToolset.Util.wixext `
	-d "StageDir=$Stage" -d "Version=$Version" `
	-d "IconFile=$(Join-Path $env:WINUI_SRC 'app\Assets\EvolutionWin.ico')" `
	-intermediatefolder (Join-Path $env:BUILD_INTERMEDIATE 'msi-obj') -o $Msi
if ($LASTEXITCODE) { throw 'wix build a échoué' }
Remove-Item -Force -ErrorAction SilentlyContinue ([System.IO.Path]::ChangeExtension($Msi, '.wixpdb'))

# Validation ICE. Ignorées : ICE03 (les .mui du Windows App SDK portent des
# langues récentes, 1152/1153/1169, inconnues des tables ICE ; sans effet à
# l'installation), ICE61 (réinstallation à version égale voulue), ICE43 et
# ICE57 (raccourci du menu Démarrer d'un paquet par machine, ALLUSERS=1 :
# ces règles le supposent par utilisateur).
& $WixExe msi validate -sice ICE03 -sice ICE61 -sice ICE43 -sice ICE57 $Msi
if ($LASTEXITCODE) { throw 'Validation ICE du MSI en échec' }

Invoke-Sign @($Msi)
Write-Host ("== MSI : {0} ({1:N1} Mo)" -f $Msi, ((Get-Item $Msi).Length / 1MB))
