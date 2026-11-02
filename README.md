# VT-Designer

Grafischer Editor für ISOBUS-Objektpools (Virtual Terminal nach ISO 11783-6).

Objektpools werden per Drag & Drop gestaltet und exportiert als:

- binärer Objektpool (`.iop`)
- C/C++-Header mit Objekt-IDs
- C-Array zum Einkompilieren in die Steuergeräte-Firmware

Vorhandene `.iop`-Dateien können importiert werden. Unterstützt werden zunächst VT-Version 3 bis 5.

## Voraussetzungen

- Qt 6.8 LTS (Kit „MSVC 64-bit“)
- CMake ≥ 3.25
- Visual Studio 2022/2026 (MSVC, C++20)

## Hinweis

Die Normdokumente (ISO 11783) sind lizenziert und nicht Teil dieses Repositorys.
