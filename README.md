# VT-Designer

Grafischer Editor für ISOBUS-Objektpools (Virtual Terminal nach ISO 11783-6).

Objektpools werden per Drag & Drop gestaltet und exportiert als:

- binärer Objektpool (`.iop`)
- C/C++-Header mit Objekt-IDs
- C-Array zum Einkompilieren in die Steuergeräte-Firmware

Vorhandene `.iop`-Dateien können importiert werden. Unterstützt werden VT-Version 3 bis 5.
Die Oberfläche gibt es auf Englisch und Deutsch.

## Stand

| Phase | Inhalt | Status |
|---|---|---|
| 1 | Kernbibliothek `vtcore`: Objektschema (alle VT3–VT5-Objekte), `.iop` lesen/schreiben, Validator, Exporter, Projektdatei, `vtpool-cli` | fertig |
| 2 | Editor: Zeichenfläche, Objektbaum, Eigenschaften, Toolbox, Problemliste, Bildimport, Undo/Redo, DE/EN | fertig |
| 3 | Makro-Editor, Window Mask/Key Group, mehrsprachige Pools, Ausrichten/Verteilen | offen |

## Voraussetzungen

- Qt 6.12 mit dem Kit „MinGW 64-bit“ (inkl. der mitgelieferten MinGW-, CMake- und Ninja-Werkzeuge)
- CMake ≥ 3.25

Die Pfade im Preset `windows-mingw` zeigen auf die Standardinstallation unter `C:\Qt`. Liegt Qt woanders,
die Pfade in einer eigenen `CMakeUserPresets.json` überschreiben. Abhängigkeiten (nlohmann/json, GoogleTest)
lädt CMake beim Konfigurieren automatisch.

Der Preset `windows-msvc` (Visual Studio 2026) baut nur Kernbibliothek, CLI und Tests, weil das installierte
Qt-Kit MinGW ist.

## Bauen, testen, starten

```powershell
cmake --preset windows-mingw
cmake --build --preset debug
ctest --preset debug
```

Der Build-Ordner liegt bewusst außerhalb von OneDrive unter `%USERPROFILE%\build\vt-designer\`.
Das Programm liegt danach in `...\windows-mingw\Debug\VT-Designer.exe`. Damit es auch außerhalb der
Entwicklungsumgebung startet, kopiert `cmake --build --preset debug --target deploy` die Qt-Laufzeit daneben.

## Bedienung des Editors

- **Objektleiste** (über der Zeichenfläche): ein Symbol je Objekttyp, nach Kategorien gruppiert und farblich
  unterschieden; Name und Beschreibung erscheinen als Tooltip. Symbol auf eine Maske ziehen – während des Ziehens
  zeigt die Zeichenfläche das Objekt halbtransparent in Originalgröße an der Ablagestelle und markiert das
  Ziel-Elternobjekt (rot umrandet, wenn es dort nicht erlaubt ist). Ein Klick fügt das Objekt in das ausgewählte
  Objekt ein. Benötigte Hilfsobjekte (Schrift- und Linienattribute, Designator …) werden automatisch angelegt oder
  wiederverwendet. Objekte, die die eingestellte VT-Version nicht kennt, sind ausgegraut.
- **Zeichenfläche**: klicken = auswählen, ziehen = verschieben, Anfasser = Größe ändern, Pfeiltasten = 1 px
  (mit Umschalt um eine Rasterweite), Alt+Klick = Elternobjekt auswählen, Doppelklick auf Container, Button
  oder Key = darin weiterarbeiten, Strg+Mausrad = Zoom.
- **Pool** (links unten): Masken-Hierarchie und alle Objekte nach Typ; Kontextmenü zum Öffnen und Löschen.
- **Eigenschaften** (rechts): alle Attribute mit passenden Editoren (Farbpalette, Auswahllisten, Bitfelder,
  Objektreferenzen inkl. „Neu …“), Texte, Listen, Makro-Zuordnungen, Polygonpunkte, Bildimport sowie
  „Verwendet von“.
- **Probleme** (unten): laufende Prüfung gegen ISO 11783-6; Doppelklick springt zum Objekt.
- **Pool → Pool-Einstellungen**: VT-Version, Vorschau-Terminal (Data-Mask- und Soft-Key-Größe), Exportoptionen,
  Sprache.
- **Datei → Pool exportieren** (Strg+E): schreibt `.iop`, ID-Header, `_data.h` und `_data.c`.

Kommandozeile des Editors: `VT-Designer.exe datei.vtp` öffnet ein Projekt;
`VT-Designer.exe --render dmMain --output maske.png --scale 2 datei.vtp` rendert eine Maske ohne Fenster;
`VT-Designer.exe --icons --output symbole.png` speichert eine Übersicht aller Objektsymbole.

## Kommandozeilenwerkzeug

```text
vtpool-cli info <datei>                         Objekte eines Projekts oder einer .iop auflisten
vtpool-cli validate <datei>                     Pool gegen ISO 11783-6 prüfen
vtpool-cli import <pool.iop> <projekt.vtp>      Binären Pool in ein Projekt umwandeln
vtpool-cli build <projekt.vtp> [ausgabeordner]  .iop, ID-Header und C-Array erzeugen
  --vt-version <3|4|5>                          VT-Version für .iop-Eingaben (Standard 4)
```

Beispiel: `vtpool-cli build examples/sprayer.vtp out` erzeugt `sprayer.iop`, `sprayer_ids.h`,
`sprayer_data.h` und `sprayer_data.c`.

## Projektdatei (`.vtp`)

JSON, damit Änderungen in Git gut lesbar sind. Jedes Objekt hat `id`, `type`, optional `name`
(wird zum Symbol im C-Header), `attributes` sowie je nach Typ `children`, `references`, `macros`,
`points`, `text`/`wideText`/`data`, `languages`, `labels` und `codePlanes`. Nicht angegebene
Attribute erhalten Standardwerte. Dazu kommen die Abschnitte `export` und `designer` (Vorschau-Terminal).
Ein vollständiges Beispiel liegt in [examples/sprayer.vtp](examples/sprayer.vtp).

## Aufbau

```text
src/core/     vtcore – reines C++20 ohne Qt
  Schema        Objekttypen und Record-Formate nach Annex B
  VtObject      Objekt mit Attributen und Listen (Kinder, Makros, Daten, …)
  ObjectPool    Pool mit eindeutigen IDs
  PoolEditing   Referenzen finden/entfernen, Teilbäume duplizieren
  IopCodec      binäres Poolformat lesen/schreiben
  Hierarchy     erlaubte Eltern/Kind-Beziehungen (Table A.2)
  Validator     Prüfung auf Normkonformität
  Palette, PictureCodec   Standardfarbpalette, Bitmap-Packen, RLE, Dithering
  Exporters     C-Header und C-Array
  ProjectFile   JSON-Projektdatei
src/render/   vtrender – zeichnet VT-Objekte mit QPainter (Fonts, Zeichensätze, Formen, Grafiken)
src/app/      VT-Designer – Qt-Widgets-Editor
src/cli/      vtpool-cli
translations/ deutsche Übersetzung (Qt Linguist)
tests/        GoogleTest (Kern und Editor-Logik)
examples/     Beispielprojekte
```

Neue Texte in der Oberfläche: `cmake --build <build> --target VTDesigner_lupdate` aktualisiert
`translations/vtdesigner_de.ts`; die Übersetzung dann mit Qt Linguist ergänzen.

## Hinweis

Die Normdokumente (ISO 11783) sind lizenziert und nicht Teil dieses Repositorys.
