# Items to validate

Manual tests for version 1.3.0 (the code is delivered and the `tools\run-tests.bat` suite is at 100%, but none of the following has been tried by hand). Check each item once verified.

## 1. Immersive reader

- [ ] Mail › select a message › View tab › "Immersive reader": full screen, text only, centered.
- [ ] A−, A+, spacing and theme (light, sepia, dark) change the display.
- [ ] After a click in the text, the + - T L keys have the same effect.
- [ ] Esc (including after a click in the text) closes the window; so does the close button.
- [ ] Reopen the reader: size, spacing and theme are kept.
- [ ] HTML-only message, then message with no body: correct display.

## 2. `mailto:` links

- [ ] Settings › Mail › "Mail links (mailto:)": the status shows "not registered".
- [ ] "Register…": the status changes to "registered" and Windows "Default apps" opens; choose EvolutionWin for MAILTO.
- [ ] "Remove registration": back to "not registered", the entry disappears from Windows.
- [ ] With EvolutionWin closed, run `build\EvolutionWin.exe "mailto:a@b.fr,c@d.fr?cc=e@f.fr&bcc=g@h.fr&subject=Caf%C3%A9&body=L1%0D%0AL2"`: a compose window opens with To, Cc, Bcc, subject "Café" and a two-line body.
- [ ] With EvolutionWin already open, same command: new compose window in the existing instance, no second EvolutionWin in Task Manager.
- [ ] Click on a `mailto:` link from a browser or a document (EvolutionWin as default): same behavior.
- [ ] Installation option `REGISTERMAILTO=1`: see section 5.

## 3. Export

- [ ] File › "Export data…", Mail, real account, 2 folders including one with subfolders, mbox, empty target folder: one `<folder>.mbox` per folder, tree structure reproduced, progress in the status bar, final report, "Open folder" button.
- [ ] Open one of the produced `.mbox` files with Thunderbird.
- [ ] Same thing in EML with "Limit to a period": only the messages of the period are exported, one `.eml` per message.
- [ ] Cancellation: export a large IMAP folder, then cancel from the status bar: the report says "cancelled".
- [ ] Export of contacts and calendars from the same window (existing wizards).
- [ ] Drag an event (Week view, then chip in the Month view, including a recurring event) onto the Desktop: a `<title>.ics` appears, with correct title, times, time zone and recurrence when opened in Outlook or Thunderbird.
- [ ] Drag a contact (not from the GAL directory) onto the Desktop: a `<name>.vcf` appears and imports into another application.
- [ ] Non-regression: double-clicking an event still opens it, right-click keeps its menu, a click on a contact shows its card.

## 4. Import

- [ ] "Import data…" (File menu, also in the Contacts, Calendar, Tasks ribbons) › mbox or `.eml`, "Importé" folder: messages read, original date kept.
- [ ] "Mail: Thunderbird profile": detected profiles listed; folders and subfolders recreated, unread state kept.
- [ ] "Mail: folder" on a Maildir.
- [ ] Import of a multi-contact `.vcf`, then of an `.ics` into a calendar; a second import of the same file skips everything (no duplicates).
- [ ] "Outlook (.pst/.ost)": the explanatory text (conversion with `readpst`) is displayed, "Import" is greyed out. _(`.pst`/`.ost` not supported: no free library available)_
- [ ] Import into a real IMAP account (not only the local account).
- [ ] Local folder: under "On this computer", create a folder, delete it, then recreate one with the same name: must succeed (bug fixed in 1.3.0).
- [ ] Recreate a local folder deleted before the fix (for example "Importé"): must succeed.

## 5. MSI: silent installation (clean Windows 11 VM, administrator session)

The MSI is built with `tools\build-msi.ps1`.

- [ ] Installation: `msiexec /i dist\Evolution-1.3.0-x64.msi /qn /l*v i.log`: code 0, application in `C:\Program Files\Evolution`, `CloseDependencies` present in `i.log`.
- [ ] Update while the application is running (with an account, so EDS services and WebView2 are active): reinstall with `/qn`, no "files in use" box, code 0 or 3010.
- [ ] After this update, no `camel-index-control`, `gspawn-win64-helper`, `evolution-*` or EvolutionWin `msedgewebview2` survives; an Edge or Teams `msedgewebview2` was not killed.
- [ ] Multi-session: with another user (or a disconnected session) running EvolutionWin, redo the update: its instances close.
- [ ] `FORCECLOSE=0`: no closing, code 3010 or replacement at restart.
- [ ] Uninstall without purge (`msiexec /x … /qn`): `%LOCALAPPDATA%\EvolutionWin` and `%LOCALAPPDATA%\evolution` remain.
- [ ] Uninstall with `PURGEUSERDATA=1`: the 3 data folders and the credentials (`cmdkey /list | findstr Evolution-Data-Server`) disappear; read `%TEMP%\EvolutionWin-purge.log`.
- [ ] Update to a higher version with `PURGEUSERDATA=1` on the command line: the data stays intact.
- [ ] `/qn AUTOUPDATE=0`: `reg query HKLM\SOFTWARE\Policies\EvolutionWin` gives `DisableAutoUpdate = 1`, no more update checks; the key disappears on uninstall.
- [ ] `/qn REGISTERMAILTO=1`: EvolutionWin appears in Settings › Default apps › Mail / MAILTO; after choosing it, a `mailto:` link opens a compose window. Without the property, it does not appear; the entry disappears on uninstall.
- [ ] Uninstall with `/l*v u.log PURGEUSERDATA=1`: `u.log` contains "Adding PURGELOG property"; absent without purge.
- [ ] Interface: double-click on the MSI (clean VM): Welcome › folder › "Closing Evolution" (2 choices) › ready › progress › finish. No license page. Texts in French, with no broken characters.
- [ ] Interactive update with Evolution open: choice "Close automatically": the application and its services close; choice "Do not close": no closing (restart requested or files replaced at restart).
- [ ] Uninstall: reopen the installed MSI › Remove › "Evolution data" page: checkbox unchecked by default; checked, `%LOCALAPPDATA%\EvolutionWin`, `%LOCALAPPDATA%\evolution` and the credentials disappear; unchecked, they remain.
- [ ] Purge log: uninstall with `/l*v u.log PURGEUSERDATA=1`: `u.log` contains lines "EvolutionWin purge : Dossier supprime : …"; so does `%TEMP%\EvolutionWin-purge.log`. Check that the deleted folders are those of the user who is uninstalling (not those of SYSTEM). _To be tested on a throwaway account: the purge also deletes the Credential Manager credentials._
- [ ] Known limitation to confirm: Settings › Installed apps › Uninstall: no interface, no purge checkbox.
- [ ] `/qn` and `/passive`: none of the new pages is displayed; FORCECLOSE, AUTOUPDATE, REGISTERMAILTO, LAUNCHAPP work as before.

## 6. Group policies (GPO)

- [ ] Copy `deploy\policy\EvolutionWin.admx` and the `fr-FR` / `en-US` folders into `C:\Windows\PolicyDefinitions`: `gpedit.msc` shows the **EvolutionWin** category with three policies.
- [ ] `DisableAutoUpdate` enabled then `gpupdate`: no update window at startup; Settings › About: button greyed out with "Updates are disabled by your administrator."
- [ ] "Remote message content" = Never: Settings › Privacy, list greyed out on "Never"; disable the policy, the setting becomes free again.
- [ ] HKCU fallback: create `HKCU\Software\Policies\EvolutionWin\DisableAutoUpdate` (DWORD) = 1: same effect without an HKLM key; with HKLM = 0, the machine setting wins.

## 7. Remaining items to validate from previous milestones

See [ROADMAP.md](ROADMAP.md): Microsoft 365 (calendar, contacts, GAL, Teams, "On behalf of"), automatic replies, server rules, offline queue, conflicts, sync windows, full-text indexing, anti-spam, Gmail delegations.
