<#
.SYNOPSIS
    Checks the prerequisites of the native MSVC build of Evolution and,
    optionally, downloads/installs what is missing.

.DESCRIPTION
    Without parameters, only reports the state of each prerequisite.
    With -Install, installs missing tools (winget, pip) and fetches missing
    sources (git, HTTP). Builds themselves are not started; the report lists
    which build-*.bat script to run next.

    Expected layout (see env.bat):
        <workspace>\evolution               sources d'Evolution (GTK)
        <workspace>\evolution-data-server   sources d'EDS
        <workspace>\winui                   client natif WinUI 3 (evo-core + EvolutionWin)
        <workspace>\tools                   scripts et outils de portage MSVC
        <workspace>\deps                    sources tierces (NSS, NSPR, libical, WebView2)
        <workspace>\gtk-build               pile GTK / préfixe d'installation commun
        <workspace>\build                   exécutables finaux et build\intermediate

.PARAMETER Install
    Install missing tools and download missing sources.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\check-prereqs.ps1
    powershell -ExecutionPolicy Bypass -File tools\check-prereqs.ps1 -Install
#>
[CmdletBinding()]
param(
    [switch] $Install
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Layout, same defaults as env.bat
# ---------------------------------------------------------------------------

$MsvcDir   = $PSScriptRoot
$Workspace = if ($env:EVO_WORKSPACE) { $env:EVO_WORKSPACE } else { (Resolve-Path (Join-Path $MsvcDir '..')).Path }
$EvoSrc    = if ($env:EVO_SRC) { $env:EVO_SRC } else { Join-Path $Workspace 'evolution' }
$EdsSrc    = if ($env:EDS_SRC) { $env:EDS_SRC } else { Join-Path $Workspace 'evolution-data-server' }
$Deps      = if ($env:DEPS) { $env:DEPS } else { Join-Path $Workspace 'deps' }
$GtkBuild  = if ($env:GTK_BUILD) { $env:GTK_BUILD } elseif (Test-Path (Join-Path $Workspace 'gtk-build\gtk\x64\release')) { Join-Path $Workspace 'gtk-build' } else { 'C:\gtk-build' }
$Msys2Root = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { 'C:\msys64' }
$Prefix    = Join-Path $GtkBuild 'gtk\x64\release'

# Pinned third-party sources
$EdsGit        = 'https://gitlab.gnome.org/GNOME/evolution-data-server.git'
$EdsBranch     = 'windows-native'
$NssGit        = 'https://github.com/nss-dev/nss.git'
$NsprVersion   = '4.38'
$NsprUrl       = "https://ftp.mozilla.org/pub/nspr/releases/v$NsprVersion/src/nspr-$NsprVersion.tar.gz"
$LibicalGit    = 'https://github.com/libical/libical.git'
$LibicalTag    = 'v3.0.20'
$WebView2Url   = 'https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2'
$PipPackages   = @('gvsbuild', 'meson', 'ninja', 'gyp-next')

$script:Results = New-Object System.Collections.Generic.List[object]

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

function Test-Command([string] $Name) {
    return [bool] (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Update-SessionPath {
    # Pick up PATH changes made by installers in this session
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
}

function Invoke-Native([string] $Exe, [string[]] $Arguments) {
    Write-Host "    > $Exe $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe exited with code $LASTEXITCODE"
    }
}

function Install-WithWinget([string] $Id, [string[]] $Extra = @()) {
    if (-not (Test-Command 'winget')) {
        throw 'winget is not available; install the package manually'
    }
    Invoke-Native 'winget' (@('install', '--id', $Id, '-e', '--silent',
        '--accept-source-agreements', '--accept-package-agreements') + $Extra)
    Update-SessionPath
}

function Get-PythonUserScripts {
    return (& python -c "import sysconfig; print(sysconfig.get_path('scripts', 'nt_user'))").Trim()
}

function Get-VsInstallPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        return $null
    }
    $path = & $vswhere -version '[17.0,18.0)' -products * -latest `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($path) { return $path.Trim() }
    return $null
}

function Save-Download([string] $Url, [string] $Destination) {
    Write-Host "    > download $Url" -ForegroundColor DarkGray
    Invoke-WebRequest -Uri $Url -OutFile $Destination -UseBasicParsing
}

# Runs one check; when it fails and -Install is given, runs the installer and
# checks again. $Hint is shown when the item stays missing.
function Test-Prerequisite {
    param(
        [string] $Name,
        [scriptblock] $Check,
        [scriptblock] $Installer,
        [string] $Hint,
        [switch] $Optional
    )

    $detail = $null
    $ok = $false
    try { $detail = & $Check; $ok = [bool] $detail } catch { $ok = $false }

    $status = if ($ok) { 'OK' } elseif ($Optional) { 'WARN' } else { 'MISSING' }

    if (-not $ok -and $Install -and $Installer) {
        Write-Host "[..] $Name : installing" -ForegroundColor Cyan
        try {
            & $Installer
            $detail = & $Check
            $ok = [bool] $detail
            $status = if ($ok) { 'INSTALLED' } else { 'FAILED' }
        } catch {
            $status = 'FAILED'
            $detail = $_.Exception.Message
        }
    }

    $color = switch ($status) {
        'OK' { 'Green' } 'INSTALLED' { 'Green' } 'WARN' { 'Yellow' } default { 'Red' }
    }
    $text = if ($ok -and $detail -is [string]) { $detail } elseif (-not $ok) { $Hint } else { '' }
    Write-Host ("[{0,-9}] {1}" -f $status, $Name) -ForegroundColor $color -NoNewline
    if ($text) { Write-Host " - $text" } else { Write-Host '' }

    $script:Results.Add([pscustomobject]@{ Name = $Name; Status = $status; Optional = [bool] $Optional })
}

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------

Write-Host ''
Write-Host "Evolution MSVC port - prerequisites check$(if ($Install) { ' (install mode)' })"
Write-Host "  workspace : $Workspace"
Write-Host "  prefix    : $Prefix"
Write-Host ''
Write-Host '== Tools'

Test-Prerequisite -Name 'Visual Studio 2022 with C++ tools' -Check {
    Get-VsInstallPath
} -Installer {
    # Build Tools edition with the C++ workload; needs elevation (UAC prompt)
    Install-WithWinget 'Microsoft.VisualStudio.2022.BuildTools' @('--override',
        '--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
} -Hint 'install Visual Studio 2022 (any edition) with the "Desktop development with C++" workload'

Test-Prerequisite -Name 'Windows 10/11 SDK' -Check {
    $inc = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
    $sdk = Get-ChildItem $inc -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName 'um\windows.h') } |
        Sort-Object Name | Select-Object -Last 1
    if ($sdk) { $sdk.Name }
} -Hint 'installed with the Visual Studio C++ workload ("Windows 11 SDK" component)'

Test-Prerequisite -Name 'Git' -Check {
    if (Test-Command 'git') { (& git --version).Trim() }
} -Installer { Install-WithWinget 'Git.Git' } -Hint 'https://git-scm.com/download/win'

Test-Prerequisite -Name 'CMake >= 3.15' -Check {
    if (Test-Command 'cmake') {
        $v = [version] ((& cmake --version | Select-Object -First 1) -replace '[^0-9.]', '')
        if ($v -ge [version] '3.15') { "cmake $v" }
    }
} -Installer { Install-WithWinget 'Kitware.CMake' } -Hint 'https://cmake.org/download/'

Test-Prerequisite -Name 'Python >= 3.9' -Check {
    if (Test-Command 'python') {
        $v = (& python -c "import sys; print('%d.%d.%d' % sys.version_info[:3])").Trim()
        if ([version] $v -ge [version] '3.9') { "python $v" }
    }
} -Installer { Install-WithWinget 'Python.Python.3.13' } -Hint 'https://www.python.org/downloads/windows/'

Test-Prerequisite -Name "Python packages ($($PipPackages -join ', '))" -Check {
    $scripts = Get-PythonUserScripts
    $missing = @()
    foreach ($p in $PipPackages) {
        & python -m pip show $p *> $null
        if ($LASTEXITCODE -ne 0) { $missing += $p }
    }
    if ($missing.Count -eq 0) { "user scripts in $scripts" }
} -Installer {
    Invoke-Native 'python' (@('-m', 'pip', 'install', '--user') + $PipPackages)
} -Hint "python -m pip install --user $($PipPackages -join ' ')"

Test-Prerequisite -Name 'MSYS2 (build tools for gvsbuild/NSPR only)' -Check {
    if (Test-Path (Join-Path $Msys2Root 'usr\bin\bash.exe')) { $Msys2Root }
} -Installer { Install-WithWinget 'MSYS2.MSYS2' } -Hint "install MSYS2 in $Msys2Root or set MSYS2_ROOT"

Test-Prerequisite -Name 'MSYS2 make and patch' -Check {
    $bin = Join-Path $Msys2Root 'usr\bin'
    if ((Test-Path "$bin\make.exe") -and (Test-Path "$bin\patch.exe")) { 'present' }
} -Installer {
    Invoke-Native (Join-Path $Msys2Root 'usr\bin\bash.exe') @('-lc', 'pacman -S --needed --noconfirm make patch')
} -Hint 'run "pacman -S make patch" in an MSYS2 shell'

Test-Prerequisite -Name 'MSYS2 intltool and perl (Evolution translations)' -Check {
    $bin = Join-Path $Msys2Root 'usr\bin'
    if ((Test-Path "$bin\perl.exe") -and (Test-Path "$bin\intltool-merge")) { 'present' }
} -Installer {
    Invoke-Native (Join-Path $Msys2Root 'usr\bin\bash.exe') @('-lc', 'pacman -S --needed --noconfirm intltool')
} -Hint 'run "pacman -S intltool" in an MSYS2 shell'

Test-Prerequisite -Name 'Edge WebView2 runtime' -Check {
    $keys = @(
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}',
        'HKCU:\Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'
    )
    foreach ($k in $keys) {
        $pv = (Get-ItemProperty $k -ErrorAction SilentlyContinue).pv
        if ($pv -and $pv -ne '0.0.0.0') { return "version $pv" }
    }
} -Installer { Install-WithWinget 'Microsoft.EdgeWebView2Runtime' } -Hint 'shipped with Windows 11; otherwise install the Evergreen runtime'

# MSI installer only (tools\build-msi.bat)
Test-Prerequisite -Name 'WiX Toolset v5 (MSI installer)' -Optional -Check {
    $wix = Join-Path $env:USERPROFILE '.dotnet\tools\wix.exe'
    if (-not (Test-Path $wix) -and (Test-Command 'wix')) { $wix = (Get-Command wix).Source }
    if (Test-Path $wix) {
        $v = (& $wix --version) -replace '\+.*', ''
        if ([version] $v -ge [version] '5.0') { "wix $v" }
    }
} -Installer {
    if (-not (Test-Command 'dotnet') -or -not (& dotnet --list-sdks)) {
        Install-WithWinget 'Microsoft.DotNet.SDK.9'
        Update-SessionPath
    }
    Invoke-Native 'dotnet' @('tool', 'install', '--global', 'wix', '--version', '5.0.2')
} -Hint 'dotnet tool install --global wix --version 5.0.2 (needs the .NET SDK)'

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

Write-Host ''
Write-Host '== Sources'

Test-Prerequisite -Name 'evolution-data-server sources' -Check {
    if (Test-Path (Join-Path $EdsSrc 'CMakeLists.txt')) { $EdsSrc }
} -Installer {
    Invoke-Native 'git' @('clone', $EdsGit, $EdsSrc)
    Invoke-Native 'git' @('-C', $EdsSrc, 'switch', '-c', $EdsBranch)
    # Windows port changes to EDS are kept as a patch in this repository
    Invoke-Native 'git' @('-C', $EdsSrc, 'apply', '--ignore-whitespace',
        (Join-Path $MsvcDir 'patches\evolution-data-server.patch'))
} -Hint "git clone $EdsGit `"$EdsSrc`", then apply tools\patches\evolution-data-server.patch"

Test-Prerequisite -Name 'NSS sources' -Check {
    if (Test-Path (Join-Path $Deps 'nss\build.sh')) { Join-Path $Deps 'nss' }
} -Installer {
    New-Item -ItemType Directory -Force $Deps | Out-Null
    Invoke-Native 'git' @('clone', '--depth', '1', '-c', 'core.autocrlf=false', $NssGit, (Join-Path $Deps 'nss'))
} -Hint "git clone $NssGit `"$Deps\nss`""

Test-Prerequisite -Name "NSPR $NsprVersion sources" -Check {
    if (Test-Path (Join-Path $Deps 'nspr\configure')) { Join-Path $Deps 'nspr' }
} -Installer {
    New-Item -ItemType Directory -Force $Deps | Out-Null
    $tmp = Join-Path ([IO.Path]::GetTempPath()) "nspr-$([guid]::NewGuid())"
    New-Item -ItemType Directory $tmp | Out-Null
    try {
        $archive = Join-Path $tmp 'nspr.tar.gz'
        Save-Download $NsprUrl $archive
        Invoke-Native 'tar' @('-xzf', $archive, '-C', $tmp)
        Move-Item (Join-Path $tmp "nspr-$NsprVersion\nspr") (Join-Path $Deps 'nspr')
    } finally {
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    }
} -Hint "download $NsprUrl and extract its nspr folder to `"$Deps\nspr`""

Test-Prerequisite -Name "libical $LibicalTag sources" -Check {
    if (Test-Path (Join-Path $Deps 'libical\CMakeLists.txt')) { Join-Path $Deps 'libical' }
} -Installer {
    New-Item -ItemType Directory -Force $Deps | Out-Null
    # LF line endings are required: the libical-glib generator breaks on CRLF templates
    Invoke-Native 'git' @('clone', '--depth', '1', '--branch', $LibicalTag, '-c', 'core.autocrlf=false',
        $LibicalGit, (Join-Path $Deps 'libical'))
} -Hint "git clone -c core.autocrlf=false --branch $LibicalTag $LibicalGit `"$Deps\libical`""

Test-Prerequisite -Name 'libical sources use LF line endings' -Check {
    $tpl = Join-Path $Deps 'libical\src\libical-glib\tools\header-structure-boilerplate-template'
    if ((Test-Path $tpl) -and -not ([IO.File]::ReadAllText($tpl).Contains("`r"))) { 'yes' }
} -Installer {
    $dir = Join-Path $Deps 'libical'
    Invoke-Native 'git' @('-C', $dir, 'config', 'core.autocrlf', 'false')
    Invoke-Native 'git' @('-C', $dir, 'rm', '-q', '--cached', '-r', '.')
    Invoke-Native 'git' @('-C', $dir, 'reset', '-q', '--hard')
} -Hint 'git config core.autocrlf false, then git rm --cached -r . and git reset --hard in deps\libical'

Test-Prerequisite -Name 'WebView2 SDK' -Check {
    $sdk = Join-Path $Deps 'webview2\sdk\build\native'
    if ((Test-Path "$sdk\include\WebView2.h") -and (Test-Path "$sdk\x64\WebView2LoaderStatic.lib")) { $sdk }
} -Installer {
    $dir = Join-Path $Deps 'webview2'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $zip = Join-Path $dir 'webview2.zip'
    Save-Download $WebView2Url $zip
    Expand-Archive -Force $zip (Join-Path $dir 'sdk')
} -Hint "download $WebView2Url (NuGet package) and extract it to `"$Deps\webview2\sdk`""

# ---------------------------------------------------------------------------
# Build state (informational: these are produced by the build scripts)
# ---------------------------------------------------------------------------

Write-Host ''
Write-Host '== Build state'

function Test-PkgConfig([string[]] $Modules) {
    $dir = Join-Path $Prefix 'lib\pkgconfig'
    $missing = $Modules | Where-Object { -not (Test-Path (Join-Path $dir "$_.pc")) }
    if (-not $missing) { return 'built' }
    return $null
}

Test-Prerequisite -Optional -Name 'GTK stack (build-gtk.bat)' -Check {
    Test-PkgConfig @('glib-2.0', 'gtk+-3.0', 'libsoup-3.0', 'libxml-2.0', 'sqlite3', 'json-glib-1.0')
} -Hint 'run tools\build-gtk.bat (about 1-2 hours)'

Test-Prerequisite -Optional -Name 'Native perl from gvsbuild' -Check {
    $perl = Join-Path $GtkBuild 'tools\perl-5.20.0\x64\bin\perl.exe'
    if (Test-Path $perl) { $perl }
} -Hint 'downloaded by build-gtk.bat (needed by NSS and libical)'

Test-Prerequisite -Optional -Name 'NSPR/NSS (build-nss.bat)' -Check {
    Test-PkgConfig @('nss', 'nspr')
} -Hint 'run tools\build-nss.bat after build-gtk.bat'

Test-Prerequisite -Optional -Name 'libical (build-libical.bat)' -Check {
    Test-PkgConfig @('libical-glib')
} -Hint 'run tools\build-libical.bat after build-gtk.bat'

Test-Prerequisite -Optional -Name 'evolution-data-server (build-eds.bat)' -Check {
    Test-PkgConfig @('libedataserver-1.2', 'camel-1.2')
} -Hint 'run tools\build-eds.bat after the steps above'

Test-Prerequisite -Optional -Name 'WebKit API shim on WebView2 (build-webkit-shim.bat)' -Check {
    Test-PkgConfig @('webkit2gtk-4.1')
} -Hint 'run tools\build-webkit-shim.bat after build-eds.bat'

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

Write-Host ''
$blocking = @($script:Results | Where-Object { -not $_.Optional -and $_.Status -in 'MISSING', 'FAILED' })
if ($blocking.Count -gt 0) {
    Write-Host "$($blocking.Count) required prerequisite(s) missing." -ForegroundColor Red
    if (-not $Install) {
        Write-Host 'Run again with -Install to install/download them automatically.'
    }
    exit 1
}

Write-Host 'All required prerequisites are present.' -ForegroundColor Green
Write-Host 'Build order: build-gtk.bat, build-nss.bat, build-libical.bat, build-eds.bat, build-webkit-shim.bat, build-evolution.bat'
exit 0
