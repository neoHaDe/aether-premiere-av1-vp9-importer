# Aether 1.3.4

The interface speaks English now, and you can pick the language.

## English, with a switch

Everything a person reads was Russian only: the panel inside Premiere, the
`Aether.exe` window, and the diagnostic report — the one people are asked to
attach to an issue. A report nobody outside can read is not much of a report.

All three are now bilingual. The language is chosen for you from the host
locale — a Russian Premiere gets a Russian panel, everything else gets
English — and the switch in the panel header overrides that. The choice is
kept in `settings.ini` as `lang = auto | ru | en`, so the window and the panel
always agree without talking to each other.

The diagnostic engine takes `--lang en|ru`, which is how the panel asks for a
report in the language you are looking at rather than the one last saved.

**One rule made this safe, and it is written down in the source.** `Tr` returns
a whole string and never a format string. Localisation of the diagnostics was
attempted once before through `Format(Tr(...))` and reverted along with a crash
that was never explained: two languages must then agree on the number and order
of `%` substitutions, and when they drift `printf` reads the wrong stack. All
nine `Format`/`swprintf` calls that had a Russian format string were rewritten
to compose their numbers with a stream. There is no `%` anywhere in the string
table, and there is no way to add one by accident.

## The panel shows its version again

It always said "version unknown". The panel asked PowerShell for the version
with

```
powershell -NoProfile -Command "(Get-Item -LiteralPath $args[0])..." <path>
```

but `$args` is only populated with `-File`; with `-Command` everything after it
is joined into the command. The script always received an empty path, `Get-Item`
failed, and the catch wrote "version unknown" — every single time, on every
machine.

The path is now placed into the script inside single quotes with any inner
quote doubled, which is PowerShell's own escape and does not interpolate.

## Checks

Fourteen files through the core, ninety-six deliberately damaged ones, five host
profiles in the fake host. Both languages exercised end to end: the setting, the
`--lang` override, and the report itself.

Verified in Premiere Pro 26.0 on Windows 11.

## Install

Download `AetherSetup-1.3.4.exe`, close Premiere Pro, After Effects and Media
Encoder, run it. Administrator rights are required. Installs over any earlier
version; no need to uninstall first.
