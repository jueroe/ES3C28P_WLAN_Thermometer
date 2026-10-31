# Hinweise für die KI-Unterstützung in diesem Projekt

Diese Datei wird von Copilot Chat automatisch gelesen, wenn sie unter
`.github/copilot-instructions.md` im Arbeitsverzeichnis liegt. Sie hält fest, was
in diesem Projekt bereits teuer gelernt wurde – bitte einhalten, statt es neu
auszuprobieren. Für neue Projekte: Datei kopieren und den Abschnitt
„Projekt-spezifisch" anpassen.

## Umgebung

- ESP-IDF **v6.1**, Ziel `esp32s3`; Wrapper im Projekt: `.\idf build`,
  `.\idf -p COM12 flash`, `.\idf -p COM12 monitor`
- Tools unter `C:\Espressif\tools`, IDF unter `C:\esp\v6.1\esp-idf`
- PowerShell mit ExecutionPolicy `Restricted` → immer der Wrapper `.\idf`,
  nicht `idf.py` direkt
- Die Type-C-Buchse hängt am nativen USB des ESP32-S3 (USB Serial/JTAG), die
  Konsole läuft deshalb über `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`; UART0
  (IO43/44) gibt es nur am 1,25-mm-Stecker
- Der Monitor blockiert den seriellen Port. Vor dem Flashen die Monitor-Session
  beenden, sonst `could not open port … PermissionError`
- `Select-String`-Filter liefert Exitcode 1 – für „fertig" ist die Zeile
  `Project build complete` maßgeblich, nicht der Exitcode

## Build-System

- `MINIMAL_BUILD`: jedes benutzte Component muss in `REQUIRES`
  (`main/CMakeLists.txt`) **und** in `main/idf_component.yml` stehen
- **Änderungen an `sdkconfig.defaults` wirken nur, wenn keine `sdkconfig`
  existiert.** Bei bestehendem Projekt die Option direkt in `sdkconfig` ändern,
  sonst bleibt sie aus (Beispiel: `CONFIG_ESP_WIFI_SOFTAP_SUPPORT`)
- Kein `cJSON` in IDF 6.1 – JSON mit `snprintf` bauen oder Felder per `strstr`
  extrahieren
- Warnungen sind Fehler (`-Werror`): `snprintf` mit möglicher Kürzung vermeiden,
  dafür `strlcpy` verwenden

## Eigene Header

- **Nie** Namen von System-/Component-Headern verwenden: kein `main/time.h`
  (verdeckt `<time.h>`), kein `main/mdns.h` (verdeckt die mDNS-Komponente).
  Deshalb heißen die Module hier `clock` und `discovery`
- Header ohne ESP-IDF-Abhängigkeit halten, wenn sie von datenorientierten
  Headern gebraucht werden (Muster: `gfx_color.h`)

## Speicher (ESP32-S3 mit 512 KB SRAM + 8 MB PSRAM)

- Der volle Framebuffer 240×320×2 = 150 KB passt hier in den internen RAM und
  wird mit `MALLOC_CAP_DMA` **vor** dem Start des WiFi-Tasks angelegt
  (`LAYOUT_BAND_HEIGHT = BOARD_SCREEN_HEIGHT`); Bandrendering bleibt als Option
  (`160`), dann muss `LAYOUT_BAND_HEIGHT` `BOARD_SCREEN_HEIGHT` teilen
- Canvas nicht in PSRAM legen: SPI-DMA daraus ist langsamer und war schon
  Gegenstand von Errata. PSRAM nur für Bild-Caches verwenden
- Heap nach der Initialisierung und nach jedem neuen Server/Task **loggen** –
  Messwerte statt Schätzungen (`esp_heap_caps_get_free_size` /
  `_get_largest_free_block`)
- `stb_truetype` scheitert **still**, wenn kein zusammenhängender Block frei ist:
  Chunk-Größen in `stb_truetype.h` bleiben klein (128/64/32), Allokationsfehler
  werden protokolliert

## Netzwerk

- `nvs_flash_init()` genau einmal, früh, über `netcfg_init()`
- **Port 80 darf nur einen Server haben**: vor dem Start des zweiten
  `httpd_stop()` des ersten abwarten (`webserver_stop()`), sonst antwortet der
  falsche Server
- Access Point braucht `CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y`; Scan im AP-Betrieb
  nur mit `WIFI_MODE_APSTA`
- DHCP-Hostname max. 32 Zeichen, nur `a–z`, `0–9`, `-`
- HTTPS zu Fremdservern: `esp_crt_bundle_attach` +
  `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y`,
  `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384`
- mDNS über die Komponente `espressif/mdns`; Hostname aus `netcfg_hostname()`

## Zahlen und Text

- `CONFIG_NEWLIB_NANO_FORMAT` kann `%f` unbrauchbar machen → Dezimalzahlen aus
  Ganzzahlen bauen (`/10`, `%10`), wie in `infotile.c` und `webserver.c`
- JSON immer mit Punkt, die Anzeige mit Komma
- Text aus Formularen maskieren: `html_escape()` / `json_escape()` aus
  `textutil.h`

## Arbeitsweise

- **Bei größeren Aufgaben die voraussichtliche Dauer ansagen**, bevor die erste
  Aktion läuft (z. B. „das sind Umbauten an 5 Dateien, bauen + flashen +
  verifizieren: etwa 10–15 Minuten"). Während der Arbeit kurze Zwischenstände
  geben („Modul A fertig, jetzt B"), damit niemand rätselt, ob es noch läuft.
  Ein Abbruch mitten in der Arbeit ist teuer: halb umgestellte Module lassen
  sich nicht sinnvoll prüfen.
- Testschalter (`APP_FORCE_CONFIG`, `APP_TEST_CONFIG_AFTER_CONNECT_S`) stehen im
  Repository auf **0**; wenn sie für einen Test auf 1 gesetzt werden, müssen sie
  danach zurück – ein vergessener Schalter hat schon zweimal eine
  Fehlersuche ausgelöst
- Nach jeder Änderung: bauen, flashen, im Monitor prüfen, Ergebnis berichten
- Board-spezifische Werte (Pins, Spiegelung, Kalibrierung) gehören in
  `main/board_<board>.h`, nie in die Logik
- Alles, was ein Mensch einstellen will, gehört in `layout.h` / `connect.h` /
  `netcfg`, nicht als Konstante in eine `.c`

## Projekt-spezifisch (hier: ES3C28P)

- Display ILI9341V 240×320 **Hochformat**, IPS → **Inversion an**
  (`BOARD_LCD_INVERT_COLORS=1`), `BOARD_LCD_BGR=1`, Spiegelung nur X
- Display-SPI: SCLK=IO12, MOSI=IO11, MISO=IO13, CS=IO10, DC=IO46, RST=EN,
  Backlight=IO45 (LEDC-PWM, high = an)
- Touch **FT6336G** an I²C (Adresse 0x38), RST=IO18, INT=IO17; liefert Pixel,
  keine Rohwerte → keine Kalibrierung, keine Z-Schwelle
- I²C-Bus geteilt: **SDA=IO16, SCL=IO15** (Touch + IIC-Stecker)
- UNIT ENV III: SHT30 (0x44) für Temperatur/Feuchte, QMP6988 (0x70) für Druck
- RGB-LED: einzelner **WS2812 an IO42** (GRB, RMT über `espressif/led_strip`),
  nicht mehr drei PWM-Kanäle
- Partitionen: 4 MB App, 4 MB littlefs (`storage`); die restlichen 8 MB von
  16 MB bleiben ungenutzt (kein OTA)

## Wo die Details stehen

- [README.md](../README.md) – vollständiges Handbuch inkl. Fehlersuche
- [PORTING.md](../PORTING.md) – was für die Folgeprojekte übernommen wird
