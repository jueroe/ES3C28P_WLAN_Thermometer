# PORTING – dieses Projekt als Vorlage für die nächsten

Diese Datei ist für die geplanten Folgeprojekte gedacht:

1. **Außensensor** – SHT35 an einem ESP32-S3 Devkit-C1, stellt Temperatur und
   Feuchte als HTTP/JSON bereit (Gegenstück zum Feld `remote` hier)
2. **Zweites Raumgerät / ES3C28** – ESP32-S3 N16R8 (8 MB PSRAM, 16 MB Flash),
   gleiches Panel (ILI9341V, 240×320), aber RGB666 und kapazitiver Touch
   (FT6336G) – siehe Abschnitt 4
3. **Wetterstation** – JC8012P4A1 mit ESP32-P4 + ESP32-C6-MINI-1U (32 MB PSRAM,
   16 MB Flash), zeigt die Werte aller Räume an

Sie beschreibt, **was übernommen wird**, **was angepasst werden muss** und
**welche Fallen schon bezahlt sind**. Das Rad muss nicht zweimal erfunden werden.

Verwandte Dateien:

| Datei | Zweck |
|---|---|
| [README.md](README.md) | das vollständige Handbuch dieses Projekts (RAM, Layout, Fehlersuche) |
| [.github/copilot-instructions.md](.github/copilot-instructions.md) | Regeln für die KI-Unterstützung – **in das neue Repo kopieren** |
| `main/board_e32r28t.h` | Beispiel für die Trennung „alle Pins in einem Header" |

---

## 1. Was übertragbar ist

| Datei | Rolle | Anpassung im neuen Projekt |
|---|---|---|
| `main/netcfg.c/.h` | WLAN, Standort, Gerätename, Außensensor-IP im NVS; `netcfg_hostname()` | keine (nur `connect.h`-Defaults setzen) |
| `main/configmode.c/.h` | Access Point, Einrichtungsseite, Speichern + Neustart | TFT-Teile entfernen, wenn es kein Display gibt |
| `main/webserver.c/.h` | `GET /api/values` als JSON, `/` als Browserseite | Wertequelle tauschen (SHT31 statt BME280) |
| `main/discovery.c/.h` | mDNS `<name>.local`, `_http._tcp` | keine |
| `main/textutil.c/.h` | HTML-/JSON-Maskierung | keine |
| `main/sensor.c/.h` | Muster: Sensor abstrahieren + **letzten Messwert cachen** (`sensor_last()`) | Treiber ersetzen, Cache-Muster behalten |
| `main/gfx.c/.h`, `main/ttf.c/.h`, `main/fonts/`, `main/tjpgd/` | Zeichnen, TrueType, JPEG | nur bei Geräten mit Display |
| `main/layout.h` | alle Koordinaten/Maße an einer Stelle | neue Werte, Aufbau übernehmen |
| `main/clock.c/.h` | NTP + Zeitzone, Modulname bewusst **nicht** `time` | keine |
| `main/sun.c/.h`, `main/wallpaper.c/.h`, `main/statusled.c/.h` | Sonnenzeiten, Bildauswahl, Ambilight | nur bei Bedarf |

Nicht übertragbar (board- und projektgebunden): `board_e32r28t.h`, `partitions.csv`,
`sdkconfig.defaults`, die Wallpaper in `data/`.

---

## 2. Ziel A: SHT31-Außensensor (ESP32-S3 Devkit-C1)

Der einfachste Fall – kein Display, keine Bilder, nur Sensor + Netzwerk.

1. Neues ESP-IDF-Projekt, `netcfg`, `configmode` (ohne TFT), `webserver`,
   `discovery`, `textutil` übernehmen
2. `sensor.c` gegen SHT31 tauschen (I²C, Adresse 0x44 oder 0x45, Kommando 0x2400
   „measure, high repeatability", danach ~15 ms warten und 6 Bytes lesen;
   Umrechnung `-45 + 175 * raw / 65535` bzw. `100 * raw / 65535`)
3. JSON-Schema **identisch** zu diesem Gerät halten (`temperature_c`,
   `humidity_rh`, `valid`, `age_s`), damit die Wetterstation später beide Geräte
   gleich behandeln kann. Druck einfach weglassen
4. Konfigurationsmodus ohne Anzeige: Der Modus startet dann nur über den
   fehlenden Netzzugang, nicht per Touch. Alternativ den BOOT-Taster (GPIO9) mit
   `configmode_request()` verbinden – 5 Zeilen, sehr praktisch beim Einrichten
5. Stromverbrauch: Der C3 kann WiFi-Modem-Sleep nutzen
   (`CONFIG_ESP_WIFI_SLP_IRAM_OPT`, `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)`), wenn er
   nur alle paar Minuten antwortet – aber Achtung, dann antwortet der Webserver
   träger

**Randbedingungen C3:** Single-Core (RISC-V), ~400 KB SRAM, kein PSRAM. Kein
Bandrendering nötig, kein RAM-Sparzwang wie auf dem E32R28T. Für den
Konfigurationsmodus `CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y` **direkt in der
`sdkconfig`** setzen (siehe Falle 3).

## 3. Ziel B: ES3C28 (ESP32-S3 N16R8, RGB666, kapazitiver Touch)

Gleiches Panel und gleiche Auflösung wie hier (ILI9341V, 240×320), aber ein
anderer Prozessor, ein anderer Touch-Controller und ein anderes Farbformat. Die
Anwendung selbst bleibt unverändert – betroffen sind nur die drei
Hardware-Schichten.

| Thema | E32R28T (hier) | ES3C28 | Aufwand |
|---|---|---|---|
| Prozessor | ESP32-D0WD-V3, 2 Kerne, 520 KB SRAM | ESP32-S3, 2 Kerne, 512 KB SRAM + 8 MB PSRAM | Board-Header |
| Flash | 4 MB | 16 MB | `partitions.csv` |
| Panel | ILI9341V, RGB565 | ILI9341V, **RGB666** | siehe unten |
| Touch | XPT2046, resistiv, SPI | **FT6336G**, kapazitiv, I2C | `touch.c` ersetzen |
| Anzeige-Puffer | 1 Band (77 KB), kein PSRAM | voller Frame möglich | `layout.h` |
| Pins | siehe `board_e32r28t.h` | neues `board_es3c28.h` | Board-Header |

### Was 1:1 übernommen wird

`netcfg`, `configmode`, `webserver`, `discovery`, `textutil`, `watchdog`,
`clock`, `sun`, `wallpaper`, `statusled` (Pins anpassen), `outdoor`, `infotile`,
`bigdigit`, `ttf`, `tjpgd` – und `data/backgrounds` unverändert. Die
Wallpaper-JPEGs sind formatunabhängig; nur das Ziel-Pixelformat entscheidet,
wie sie in den Puffer geschrieben werden.

### RGB666: ehrliche Einschätzung

Das ILI9341 kann über SPI sowohl 16 Bit (RGB565) als auch 18 Bit (RGB666)
annehmen. **RGB666 ist hier kein Selbstzweck:**

* Der ganze Zeichenweg ist auf 16 Bit ausgelegt – `GFX_RGB565`,
  `gfx_unpack565`, `gfx_blend_mask`, die Farbtabellen in `background.h`, und
  TJpgDec liefert RGB565. Ein Umstieg auf 18 Bit heißt: 3 Bytes pro Pixel,
  +50 % Buszeit, und jede dieser Stellen anfassen.
* Sichtbarer Gewinn auf einem 240×320-TN-Panel ist gering – feinere Verläufe
  bei Farb?bergängen, mehr nicht. Die Anti-Aliasing-Qualität der Schriften
  hängt am Alphablending, nicht an der Farbtiefe.
* **Empfehlung:** mit `bits_per_pixel = 16` starten (funktioniert sofort), und
  erst wenn Verläufe sichtbar streifen, auf 18 umstellen. Der Panel-Treiber
  (`esp_lcd_panel_ili9341`) nimmt beides.

Falls 18 Bit doch gewünscht sind: nicht den ganzen Zeichenweg umbauen, sondern
in `gfx_flush()` beim Kopieren ins DMA-Puffer von 565 auf 666 expandieren
(3 Bytes statt 2). Damit bleiben Canvas, Blending und JPEG unverändert.

### FT6336G statt XPT2046

Der resistive Treiber hier (`esp_lcd_touch` + `atanisoft/esp_lcd_touch_xpt2046`)
fällt weg. Zwei Wege:

1. **Fertige Komponente** aus der Registry, die auf `espressif/esp_lcd_touch`
   aufsetzt (Schnittstelle `esp_lcd_touch_read_data()` / `esp_lcd_touch_get_data()`
   bleibt gleich): `cfscn/esp_lcd_touch_ft6x36` oder
   `lambage/esp_lcd_touch_ft6336u`. Beide sind Community-Komponenten –
   Qualität vorher am Log prüfen.
2. **Eigener Treiber** in `touch.c` (empfohlen, wenn die Komponente nicht passt):
   Der FT6336G hängt an I²C (Adresse 0x38), meldet die Zahl der Berührungen in
   Register 0x02 und die erste Berührung ab 0x03 (X-High/Low, Y-High/Low). Das
   sind ~100 Zeilen, und die bestehende Abstraktion (`touch_init`,
   `touch_read`, `touch_state_t`) bleibt unverändert.

Wichtig für den Rest des Programms:

* **Keine Kalibrierung, keine Z-Schwelle.** `BOARD_TOUCH_RAW_MIN/MAX`,
  `CONFIG_XPT2046_Z_THRESHOLD` und die `scale_axis()`-Umrechnung entfallen;
  die Koordinaten kommen bereits in Bildschirmpixeln. `state->strength` auf
  einen festen Wert setzen, damit die Prüfungen im Code weiter greifen.
* `swap_xy` / `mirror_x` / `mirror_y` bleiben nötig – kapazitive Panels sind
  häufig anders orientiert als das Display. Mit dem Log aus
  `touch: XPT2046 ready …` (dort dann entsprechend) prüfen.
* **Der Eck-Touch zum Öffnen der Einrichtung funktioniert weiter**
  (`LAYOUT_CONFIG_HOLD_MS` 500 ms in `layout.h`). Bei kapazitivem Touch ist
  ?halten" angenehmer als bei resistivem, weil kein Druck nötig ist.

### PSRAM und der Anzeigepuffer

Der S3 hat 512 KB internes SRAM – ein voller Frame (240×320×2 = 150 KB) passt
dort hinein, ganz ohne PSRAM. Trotzdem **nicht** blind umstellen:

* `LAYOUT_BAND_HEIGHT` auf `BOARD_SCREEN_HEIGHT` setzen und `origin_y` bleibt 0:
  dann entfällt das Bandrendering, und TJpgDec dekodiert nur noch einmal statt
  zweimal pro Frame (~30 ms gespart). Die Bandlogik bleibt als Option im Code.
* Den Canvas **im internen RAM** halten (`MALLOC_CAP_DMA`), nicht in PSRAM:
  SPI-DMA aus PSRAM funktioniert auf dem S3, ist aber langsamer und war schon
  Gegenstand von Errata. PSRAM erst für Bilder-Caches oder Vorab-Dekodierung
  nutzen.
* Mit 8 MB PSRAM sind die Wallpaper-Sorgen weg: mehrere Bilder vorab dekodieren,
  oder die littlefs-Partition von 1 MB auf 4 MB vergrößern (16 MB Flash).

### Partitionen (16 MB)

```
nvs        24 KB
otadata     8 KB      ← OTA ist bei 16 MB problemlos möglich
phy_init    4 KB
factory/ota_0  4 MB
ota_1          4 MB
storage     ~5 MB     littlefs mit allen Wallpapern
```

Wenn OTA nicht gebraucht wird, lieber eine große App-Partition und 8 MB
littlefs. `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` nicht vergessen – und daran
denken, dass `sdkconfig.defaults` nur bei fehlender `sdkconfig` wirkt (Falle 3).

### Startreihenfolge beim Portieren

1. Neues Projekt, `board_es3c28.h` mit den Pins, `partitions.csv`,
   `sdkconfig.defaults` – **zuerst** `CONFIG_SPIRAM=y` und die Flash-Größe
2. Panel zum Laufen bringen (`esp_lcd` + ILI9341, `bits_per_pixel = 16`),
   Testbild aus `testscreen.c` zeigt, ob Pins und Orientierung stimmen
3. Touch: erst rohe Koordinaten loggen, dann `swap`/`mirror` einstellen, dann
   vier Ecken antippen (wie bei der ersten Inbetriebnahme hier)
4. Erst danach die Anwendungsmodule kopieren – in der Reihenfolge
   `netcfg` → `wifi` → `webserver` → `discovery`, damit das Gerät früh ohne
   Neuflashen bedienbar ist
5. `data/backgrounds` 1:1 übernehmen (die Dateinamen müssen zu
   `background.h` passen – das ist die einzige Kopplung)

## 4. Ziel C: Wetterstation (ESP32-P4 + ESP32-C6)

Hier ändert sich die Netzwerkseite grundlegend, die Anzeigeseite wird einfacher.

1. **Zuerst das Board zum Laufen bringen**: Espressif-Beispiel für den P4 bzw.
   das Board-SDK als Ausgangspunkt, Panel über `esp_lcd` (beim P4 meist
   MIPI-DSI, `esp_lcd_mipi_dsi`) – Board-Datenblatt prüfen, nicht raten
2. **Netzwerk über den C6**: Der P4 hat kein eigenes WLAN. Dafür kommen
   `esp_hosted` (Firmware auf dem C6, Anbindung SDIO/SPI) und `esp_wifi_remote`
   ( stumpfe `esp_wifi_*`-API auf dem P4) dazu. Die `esp_netif`-Aufrufe bleiben
   gleich, aber **`wifi.c` muss angepasst werden**: Modus wechseln, Scan,
   Access Point und `esp_wifi_sta_get_ap_info()` laufen über die Remote-Schicht.
   Diesen Schritt zuerst isoliert testen (verbinden, IP, Scan), bevor die
   Einrichtungsseite portiert wird
3. **Anzeige**: Bandrendering ist hier unnötig – 32 MB PSRAM erlauben einen
   vollen Framebuffer. In `gfx.c` genügt `origin_y = 0` und
   `LAYOUT_BAND_HEIGHT = BOARD_SCREEN_HEIGHT`; die Bandlogik bleibt als Option
   erhalten. Danach: doppelt gepufferte Frames, Bilder vorab dekodieren
4. **Räume anzeigen**: Die Messwerte der anderen Geräte per
   `http://<ip-oder-name>.local/api/values` holen (Muster: `sun_fetch()` in
   `main/sun.c`, nur HTTP statt HTTPS) und in einer Tabelle/Kachel darstellen.
   Geräteliste als Tabelle in einer eigenen Konfigurationsdatei oder im NVS
5. **Schriften**: Auf dem P4 lohnt es, die TTF-Rasterisierung einmal pro
   Textänderung in einen PSRAM-Puffer zu rendern und nur bei Änderung neu – bei
   800×1280 ist das der Unterschied zwischen flüssig und ruckelig

---

## 5. Die schon bezahlten Fallen

Diese Liste ist der eigentliche Wert dieses Projekts – jede Zeile hat einmal
einen Abend gekostet.

1. **`stb_truetype` fordert still zu viel Heap.** Der Zeichen-Cache reservierte
   56 KB pro Glyph; nach WiFi/TLS war kein zusammenhängender Block mehr frei und
   die Glyphen blieben **ohne Fehlermeldung** leer. Lösung: Chunk-Größe in
   `stb_truetype.h` auf 128/64/32 senken, Allokationsfehler protokollieren,
   Selbsttest „Textpixel pro Band zählen".
2. **HTTPS auf api.sunrise-sunset.org scheitert mit `-0x3000`.** Das ist
   `MBEDTLS_ERR_X509_FATAL_ERROR` wegen des cross-signierten GTS-Root-R4 →
   `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y`. Außerdem reichen
   4 KB TLS-Puffer nicht: `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384`.
3. **`sdkconfig.defaults` wirkt nur, wenn keine `sdkconfig` existiert.** Bei
   bestehendem Projekt muss die Option **direkt in der `sdkconfig`** geändert
   werden – sonst bleibt z. B. `CONFIG_ESP_WIFI_SOFTAP_SUPPORT=n` und der Linker
   meldet `undefined reference to esp_netif_create_default_wifi_ap`.
4. **Zwei HTTP-Server auf Port 80 kollidieren lautlos.** Beide binden mit
   `SO_REUSEADDR` erfolgreich, und die Anfrage landet beim falschen Server.
   Vor dem Start des zweiten Servers `httpd_stop()` des ersten abwarten.
5. **Header-Namen:** ein `main/time.h` verdeckt `<time.h>`, ein `main/mdns.h` den
   Header der mDNS-Komponente. Deshalb heißen die Module hier `clock` und
   `discovery`.
6. **`esp_wifi_scan_start()` braucht ein gestartetes Interface** – im
   Access-Point-Betrieb also Modus `WIFI_MODE_APSTA`, sonst schlägt der Scan fehl.
   Der Scan während laufendem AP wirft Clients kurz raus (Kanalwechsel).
7. **Der DHCP-Hostname braucht die richtige Länge**: maximal 32 Zeichen,
   `esp_netif_set_hostname()` gibt sonst einen Fehler zurück, der leicht
   übersehen wird.
8. **Resistiver Touch (XPT2046):** Rohwerte kalibrieren, Achsen können vertauscht
   oder gespiegelt sein (`BOARD_TOUCH_SWAP_XY`, `_MIRROR_X/Y`), und die
   Z-Druckschwelle ist boardspezifisch (`CONFIG_XPT2046_Z_THRESHOLD`).
9. **`nvs_flash_init()` genau einmal** und früh – NVS wird von WiFi, unseren
   Einstellungen und der Feldstärke-Kalibrierung gemeinsam genutzt.
10. **Konfigurationsmodus endet im Neustart.** Ein Neustart ist der einzige
    Zustand, der garantiert sauber ist, wenn der Netzwerkteil in einer eigenen
    Task läuft. Dazu der NVS-Merker „übersprungen", sonst hängt ein Gerät ohne
    Netz bei jedem Start in der Einrichtung.
11. **`%.1f` in `printf` ist riskant**, wenn `CONFIG_NEWLIB_NANO_FORMAT` aktiv
    ist. Zahlen mit Zehnteln deshalb aus Ganzzahlen bauen
    (`wert/10`, `wert%10`) – so macht es `infotile.c`.
12. **Flash-Größe prüfen, bevor OTA geplant wird**: 4 MB reichen nicht für zwei
    App-Images, deshalb hier keine OTA-Partition. Beim P4 mit 16 MB ist OTA
    dagegen problemlos.

---

13. **`bits_per_pixel = 18` braucht auch 18-Bit-Daten.** Wer im Panel-Treiber auf
    18 Bit stellt, aber 16-Bit-Pixel schickt, bekommt Farbmüll oder ein
    verschobenes Bild. Format und Puffer müssen zusammenpassen.
14. **Kapazitiver Touch liefert Koordinaten in Pixeln**, resistive Rohwerte. Wird
    die Umrechnung (`scale_axis`) übernommen, landet der Finger im Eck.
    Ebenso entfällt die Z-Schwelle – aber `strength` muss ein sinnvoller Wert
    sein, sonst verwirft der Code jede Berührung.


## 6. Checkliste für ein neues Projekt

- [ ] Board-Datenblätter/Pinbelegung **vor** dem ersten Codezeile lesen, Pins in
      `board_<name>.h` sammeln – beim ES3C28 zusätzlich `CONFIG_SPIRAM=y`
- [ ] `sdkconfig.defaults` früh festlegen (Flash-Größe, Partitionen, CPU-Takt)
- [ ] Pins in `board_<name>.h` sammeln, nichts hart im Code
- [ ] `netcfg` + `webserver` + `discovery` zuerst – damit ist das Gerät ohne
      Neuflashen bedienbar, was jede weitere Iteration beschleunigt
- [ ] JSON-Schema über alle Geräte gleich halten
- [ ] `.github/copilot-instructions.md` aus diesem Repo kopieren
- [ ] README mit „Fehlersuche"-Tabelle beginnen, sobald der erste Abend verloren
      ging – nicht am Ende

## 7. Diese Datei im Chat nutzen

Die folgenden Zitate sind **Textbausteine zum Kopieren**, keine Statusberichte:
sie zeigen, was du im neuen Chat schreiben könntest. Das neue Projekt muss
dafür natürlich schon existieren (`New-Item` bzw. die ESP-IDF-Vorlage) und
`data\backgrounds` schon kopiert sein.

Für den Außensensor:

> Lies `C:\Users\joe\Documents\ESP-IDF\Projects\E32R28T_erste_Tests\PORTING.md`
> und lege danach das Projekt für den SHT31-Außensensor an.

Für das neue Display:

> Lies `C:\Users\joe\Documents\ESP-IDF\Projects\E32R28T_erste_Tests\PORTING.md`,
> Abschnitt 3 (ES3C28). Das neue Projekt liegt in `…\ES3C28_Thermometer`,
> `data\backgrounds` ist schon kopiert.

Noch besser: `PORTING.md` und `.github/copilot-instructions.md` gleich in das neue
Repository kopieren – dann liegen sie im Arbeitsverzeichnis und werden ohne
Pfadangabe gefunden. Die zweite Datei wirkt sogar ohne jede Erwähnung, weil
VS Code sie beim Chat-Start automatisch lädt.

---

## 8. ERGEBNIS: die ES3C28P-Portierung ist umgesetzt (2026-10-31)

Das Projekt `…\ES3C28P_Thermometer` ist die konkrete Umsetzung von Abschnitt 3.
Auf dem Gerät verifiziert: Display, IPS-Inversion, FT6336G, SHT30, QMP6988,
WS2812, WLAN/NTP, littlefs mit 48 Wallpapern, voller Frame in 143 ms.

### Was gegenüber Abschnitt 3 abweicht

| Thema | Annahme in Abschnitt 3 | Tatsächlich umgesetzt |
|---|---|---|
| Panel | TN, Inversion unklar | **IPS → `BOARD_LCD_INVERT_COLORS=1`**; ohne Inversion erscheint das Bild als Negativ |
| Ausrichtung | „anderer Touch-Controller" | **Portrait = MADCTL `MX` (wie beim E32R28T)**, `swap_xy` aus; das native Portrait des Panels passt direkt zur Einbau-Rotation |
| Touch-Treiber | Community-Komponente oder eigener Treiber | **eigener Treiber in `touch.c`** (Register 0x02/0x03, Reset-Puls an IO18) – die Komponenten waren nicht nötig |
| Touch-Kalibrierung | Flags eventuell nötig | **keine Flags, keine Skalierung**: der FT6336G liefert 0…239/0…319, exakt die Bildschirmkoordinaten |
| Sensorik | „SHT31 statt BME280" | **UNIT ENV III**: SHT30 (0x44) für T/RH, QMP6988 (0x70) für Druck, beide am **selben I²C-Bus wie der Touch** (SDA=IO16, SCL=IO15) |
| Status-LED | Pins anpassen | **ein WS2812 an IO42** (GRB, RMT über `espressif/led_strip`), keine drei PWM-Kanäle |
| Anzeigepuffer | voller Frame empfohlen | **umgesetzt**: `LAYOUT_BAND_HEIGHT = BOARD_SCREEN_HEIGHT`, Canvas 150 KB in `MALLOC_CAP_DMA`, vor dem WiFi-Task angelegt |
| Konsole | UART | **USB Serial/JTAG** (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`), weil Type-C am nativen USB hängt |
| Partitionen | 4 MB App + 5 MB storage (Beispiel) | **4 MB App + 4 MB littlefs** (0x10000/0x410000); die restlichen 8 MB bleiben frei |

### Neue, teuer gelernte Fallen (zu Abschnitt 5 ergänzen)

15. **IPS-Panel braucht `esp_lcd_panel_invert_color(true)`.** Ohne das wirkt
    alles wie ein Foto-Negativ; die Board-Beschreibungen (Tactility, Bruce,
    Launcher) führen `invert-color`/`TFT_IPS=1` genau deshalb.
16. **Der FT6336G braucht einen Reset-Puls an IO18** (Low ≥ 10 ms, dann High,
    ~300 ms warten). Ohne ihn bleibt er nach dem Einschalten gelegentlich stumm.
17. **Der SHT30 ignoriert nach dem Power-Up sporadisch den ersten Befehl.**
    Ein Retry (5 × 50 ms) im Init ist billiger als „geht nach Reset".
18. **Der QMP6988 rechnet in 64 Bit.** Die Kompensation (`a1`, `b12`, `bp3` …)
    läuft mit 28…65 Bit Festkomma; mit `int32_t` kommt Druckmüll heraus. Die
    Rohdaten sind 24 Bit mit Mittelwert 8388608, das Ergebnis ist Q4 in Pascal.
19. **`led_strip` v3 hat eine neue API**: `led_strip_config_t.color_component_format`
    (`LED_STRIP_COLOR_COMPONENT_FMT_GRB`) statt `led_pixel_format`.
20. **Ein I²C-Bus-Scan beim Start spart Abende.** `i2c_master_probe()` über
    0x08…0x77 zeigt sofort, ob 0x38/0x44/0x70 antworten; der erste Fehlversuch
    „SHT30 not found" war nur ein Timing-Problem, die Adressen waren da.
21. **N16R8 = 8 MB *octal* PSRAM**: `CONFIG_SPIRAM_MODE_OCT=y` und
    `CONFIG_SPIRAM_SPEED_80M=y`; das Bootlog muss „Found 8MB PSRAM device",
    „Speed: 80MHz" und „SPI SRAM memory test OK" zeigen.
22. **`sdkconfig.defaults` wirkt auch hier nur ohne `sdkconfig`.** Beim Wechsel
    auf ein neues Board die alte `sdkconfig` und `build/` löschen, sonst bleibt
    z. B. `CONFIG_ESPTOOLPY_FLASHSIZE_2MB` stehen.

### Gemessene Werte (Startprotokoll)

```
I (2851) cyd: ILI9341 ready, 240x320 portrait, 40 MHz SPI clock, BGR element order
I (2851) gfx: canvas 240x320: 153600 bytes
I (2891) storage: littlefs mounted at /littlefs: 4096 KiB total, 3108 KiB free
I (4181) sensor: I2C device found at 0x18 / 0x38 / 0x44 / 0x70
I (4221) sensor: SHT30 ready at 0x44, first reading 23.3 C, 49.1 %rH
I (4241) sensor: QMP6988 ready at 0x70 (chip id 0x5C)
I (4291) sensor: sensor ready: 23.3 C, 49.1 %rH, 974 hPa
I (4601) touch: FT6336G ready at 0x38, firmware 0x02, chip id 0x64, 240x320
I (15851) cyd: frame: 23,3 degC, …, decode 15 ms, total 141 ms
```

Damit sind die drei Hardware-Schichten aus Abschnitt 3 vollständig ersetzt; die
Anwendungsmodule (`netcfg`, `configmode`, `webserver`, `discovery`, `clock`,
`sun`, `wallpaper`, `outdoor`, `infotile`, `bigdigit`, `ttf`, `tjpgd`) blieben
unverändert.
