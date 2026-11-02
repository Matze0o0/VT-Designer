# VT-Designer

Grafischer Editor für ISOBUS-Objektpools (Virtual Terminal nach ISO 11783-6).

Objektpools werden per Drag & Drop gestaltet und exportiert als:

- binärer Objektpool (`.iop`)
- C/C++-Header mit Objekt-IDs
- C-Array zum Einkompilieren in die Steuergeräte-Firmware

Vorhandene `.iop`-Dateien können importiert werden. Unterstützt werden zunächst VT-Version 3 bis 5.

## Stand

| Phase | Inhalt | Status |
|---|---|---|
| 1 | Kernbibliothek `vtcore`: Objektschema (alle VT3–VT5-Objekte), `.iop` lesen/schreiben, Validator, Exporter, Projektdatei, `vtpool-cli` | fertig |
| 2 | Qt-Editor: Objektbaum, Eigenschaften, Zeichenfläche, Bildimport | offen |

## Voraussetzungen

- CMake ≥ 3.25
- Visual Studio 2026 (MSVC, C++20)
- Qt 6.8 LTS (Kit „MSVC 64-bit“) – erst für den grafischen Editor (Phase 2)

Abhängigkeiten (nlohmann/json, GoogleTest) lädt CMake beim Konfigurieren automatisch.

## Bauen und testen

```powershell
cmake --preset windows-msvc
cmake --build --preset debug
ctest --preset debug
```

Der Build-Ordner liegt bewusst außerhalb von OneDrive unter `%USERPROFILE%\build\vt-designer\`.

## Kommandozeile

```text
vtpool-cli info <datei>                       Objekte eines Projekts oder einer .iop auflisten
vtpool-cli validate <datei>                   Pool gegen ISO 11783-6 prüfen
vtpool-cli import <pool.iop> <projekt.vtp>    Binären Pool in ein Projekt umwandeln
vtpool-cli build <projekt.vtp> [ausgabeordner]  .iop, ID-Header und C-Array erzeugen
  --vt-version <3|4|5>                        VT-Version für .iop-Eingaben (Standard 4)
```

Beispiel: `vtpool-cli build examples/sprayer.vtp out` erzeugt `sprayer.iop`, `sprayer_ids.h`,
`sprayer_data.h` und `sprayer_data.c`.

## Projektdatei (`.vtp`)

JSON, damit Änderungen in Git gut lesbar sind. Jedes Objekt hat `id`, `type`, optional `name`
(wird zum Symbol im C-Header), `attributes` sowie je nach Typ `children`, `references`, `macros`,
`points`, `text`/`wideText`/`data`, `languages`, `labels` und `codePlanes`. Nicht angegebene
Attribute erhalten Standardwerte. Ein vollständiges Beispiel liegt in [examples/sprayer.vtp](examples/sprayer.vtp).

## Aufbau

```text
src/core/   vtcore – reines C++20 ohne Qt
  Schema       Objekttypen und Record-Formate nach Annex B
  VtObject     Objekt mit Attributen und Listen (Kinder, Makros, Daten, …)
  ObjectPool   Pool mit eindeutigen IDs
  IopCodec     binäres Poolformat lesen/schreiben
  Hierarchy    erlaubte Eltern/Kind-Beziehungen (Table A.2)
  Validator    Prüfung auf Normkonformität
  Palette, PictureCodec   Standardfarbpalette, Bitmap-Packen, RLE
  Exporters    C-Header und C-Array
  ProjectFile  JSON-Projektdatei
src/cli/    vtpool-cli
tests/      GoogleTest
examples/   Beispielprojekte
```

## Hinweis

Die Normdokumente (ISO 11783) sind lizenziert und nicht Teil dieses Repositorys.
