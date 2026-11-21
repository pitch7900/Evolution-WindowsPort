<#
.SYNOPSIS
    Test UI automatisé NON DESTRUCTEUR (lecture seule) d'EvolutionWin, via Microsoft UI Automation.

.DESCRIPTION
    À lancer MANUELLEMENT par l'utilisateur, application EvolutionWin déjà démarrée (comptes réels connectés).
    Garanties :
      - aucun mouvement de souris, aucune frappe clavier : uniquement les patterns logiques UIA
        (SelectionItem pour la navigation) ;
      - liste blanche stricte : seuls les éléments de navigation Courrier / Calendrier / Contacts / Tâches et
        les catégories de Paramètres sont activés ; tout autre élément est refusé ;
      - liste noire de sécurité : aucun élément dont le nom évoque Envoyer, Supprimer, Marquer, Vider,
        Enregistrer, Répondre, Nouveau... n'est jamais activé (le script s'arrête en cas de doute) ;
      - aucun message n'est sélectionné (évite le marquage « lu »), rien n'est créé, envoyé ou modifié.
    Le script reste sur le Courrier à la fin.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tests\ui\test_ui_smoke.ps1
#>
param([string]$ProcessName = 'EvolutionWin')

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$ErrorActionPreference = 'Stop'
$script:failures = 0
$script:checks = 0

$Allowed = @('Courrier', 'Calendrier', 'Contacts', 'Tâches', 'Paramètres')
$Forbidden = 'Envoyer|Supprimer|Marquer|Vider|Enregistrer|Répondre|Transférer|Nouveau|Nouvelle|Archiver|Déplacer|Ajouter|Créer|Quitter|Déconnect|Effacer|Delete|Send|Reply|Save|New|Remove'

function Check([bool]$Condition, [string]$Label) {
    $script:checks++
    if ($Condition) { Write-Host "  [OK]   $Label" -ForegroundColor Green }
    else { Write-Host "  [FAIL] $Label" -ForegroundColor Red; $script:failures++ }
}

function Find-ByName($Root, [string]$Name) {
    $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $Name)
    $Root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
}

function Select-Safely($Element, [string]$Name) {
    if ($Name -match $Forbidden) { throw "Refusé par la liste noire : '$Name'" }
    $ptn = $null
    if (-not $Element.TryGetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern, [ref]$ptn)) {
        throw "Pas de SelectionItemPattern pour '$Name' : activation refusée (aucun clic simulé)"
    }
    $ptn.Select()
    Start-Sleep -Milliseconds 700
}

$proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { Write-Host "EvolutionWin n'est pas lancé : démarrez l'application puis relancez ce script." -ForegroundColor Yellow; exit 2 }
$window = [System.Windows.Automation.AutomationElement]::FromHandle($proc.MainWindowHandle)

Write-Host "== Navigation principale (lecture seule) =="
foreach ($module in 'Courrier', 'Calendrier', 'Contacts') {
    if ($Allowed -notcontains $module) { throw "Hors liste blanche : $module" }
    $item = Find-ByName $window $module
    Check ($null -ne $item) "élément de navigation '$module' présent"
    if ($item) {
        Select-Safely $item $module
        $ptn = $item.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
        Check $ptn.Current.IsSelected "'$module' est sélectionné après navigation"
    }
}

Write-Host "== Paramètres (lecture seule, catégories) =="
$settings = Find-ByName $window 'Paramètres'
Check ($null -ne $settings) "entrée Paramètres présente"
if ($settings) {
    Select-Safely $settings 'Paramètres'
    foreach ($category in 'Comptes', 'Courrier', 'Calendrier', 'Contacts', 'Notifications', 'Apparence', 'Confidentialité', 'Avancé') {
        $entry = Find-ByName $window $category
        Check ($null -ne $entry) "catégorie de paramètres '$category' présente"
        if ($entry) {
            Select-Safely $entry $category
            Check ($null -ne (Find-ByName $window $category)) "catégorie '$category' toujours affichée après sélection"
        }
    }
}

Write-Host "== Retour au Courrier =="
$mail = Find-ByName $window 'Courrier'
if ($mail) { Select-Safely $mail 'Courrier' }
Check ($proc.Responding) "l'application répond toujours"

Write-Host ""
Write-Host "Vérifications : $script:checks, échecs : $script:failures"
exit ([int]($script:failures -gt 0))
