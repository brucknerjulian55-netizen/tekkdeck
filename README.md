# Tekkdeck

Song-Deck als VST3 für FL Studio. Lade ein Lied, Tekkdeck erkennt Tempo, Takt und Tonart und hält es fest an deinem Projekt-Tempo.

## Bauen

Wie bei Tekkforge: `build.bat` starten oder den Ordner auf GitHub hochladen, dann das Build unter Actions herunterladen. `Tekkdeck.vst3` kommt nach `C:\Program Files\Common Files\VST3`. In FL Studio lädst du es als Instrument in den Channel Rack.

## Bedienung

- **Song laden:** Datei ins Fenster ziehen oder „Song laden“ drücken. Danach läuft der Song, sobald FL Studio läuft. Bar 1 in FL ist die erste Takt-Eins des Songs.
- **Tempo-Art:** Stretch hält die Tonhöhe. Tempo + Pitch macht Song schneller und höher wie beim Nightcore-Sound und klingt oft sauberer.
- **Ton anpassen an:** Wähle den Grundton deines Tracks, der Song wird passend transponiert.
- **Cues:** Leeres Pad setzt einen Cue am aktuellen Beat, belegtes Pad springt im Takt dorthin. Rechtsklick löscht.
- **Sprung auf:** Bestimmt, ob ein Sprung auf den nächsten Beat, Takt oder alle 2 oder 4 Takte wartet.
- **Loop:** Schaltet am aktuellen Beat einen Loop in der gewählten Länge. Beim Ausschalten läuft der Song dort weiter, wo er ohne Loop gerade wäre.
- **Klang:** Bass-Regler runter macht Platz für deine eigene Kick. Filter: links Tiefpass, rechts Hochpass.
- **Takt:** „Mit FL“ folgt dem Transport. „Frei“ läuft mit Play auch bei gestopptem FL.

## MIDI (Korg-Controller oder Piano Roll)

- Note 36 bis 43: Cues 1 bis 8
- Note 44: Loop an oder aus

Das entspricht in FL Studio den Noten C3 bis G3 und G#3.
Alle Regler lassen sich in FL automatisieren.

## Wenn die Erkennung daneben liegt

- Tempo doppelt oder halb: „BPM verdoppeln“ oder „BPM halbieren“.
- Raster minimal versetzt: „Raster früher“ oder „Raster später“, jeweils 5 ms.
- Eins falsch: Zur echten Eins spielen und „Hier ist die Eins“ drücken.

## Grenzen

- Das Plugin trennt keine Stimmen oder Instrumente. Der Bass-Regler ist ein EQ.
- Sehr große Tempoänderungen hört man im Stretch-Modus. Ab etwa 30 % wird die Anzeige rot.
- Im FL-Projekt speichert Tekkdeck den Dateipfad, nicht das Lied. Verschiebst du die Datei, musst du sie neu laden.

## Updates

Ab dieser Version gibt es einen Installer. Lade einmal `Tekkdeck-Setup.exe` aus den Releases von `brucknerjulian55-netizen/tekkdeck` und starte ihn. Er legt das Plugin im VST3-Ordner ab.

Danach läuft jedes Update so: Das Plugin zeigt **Update installieren**, du klickst darauf und bestätigst. Es lädt den Installer, der FL Studio zum Schließen auffordert und das Plugin ersetzt. Projekte und Presets bleiben erhalten. Das Plugin prüft beim Öffnen nur die Versionsnummer bei GitHub und sendet keine Projektdaten.

Windows kann beim ersten Start warnen, weil der Installer nicht signiert ist. Dann auf „Weitere Informationen“ und „Trotzdem ausführen“ klicken.

So veröffentlichst du ein Update: In `CMakeLists.txt` die Versionsnummer erhöhen und die Änderungen hochladen. GitHub baut dann den Installer und legt das Release selbst an.

## Tests

    g++ -std=c++17 -O2 -ISource Tests/deck_test.cpp -o deck_test && ./deck_test

## Lizenz

JUCE 8 steht unter AGPLv3 oder einer kommerziellen Lizenz. Vor einer Weitergabe `LICENSE.md` von JUCE prüfen.
