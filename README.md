# ES3C28P – Raumthermometer, -hygrometer und Luftdruckmesser

ESP-IDF-Projekt auf Basis des "2.8inch ESP32-S3 Display"
(QDtech / LCDWiki **SKU ES3C28P**: ESP32-S3R8 mit 16 MB Flash und 8 MB PSRAM,
ILI9341V-IPS-Panel, kapazitiver FT6336G-Touch).

Die Anwendung ist vom E32R28T-Projekt (ESP32-D0WD-V3, XPT2046, BME280)
übernommen; Display, Touch, Backlight, WS2812 und die Sensorik des UNIT ENV III
sind auf diesem Gerät verifiziert.

## Zielbild der Anwendung

| Funktion | Vorgabe |
|---|---|
| Sensor | UNIT ENV III über I²C: SHT30 (Temperatur, relative Luftfeuchte) und QMP6988 (Luftdruck) – **läuft** |
| Uhrzeit | NTP über WLAN, Zeitzone Europe/Berlin – **läuft**, Anzeige vorerst `dd.mm. hh:mm` |
| Temperatur | große Ziffern für die ganzen °C, Komma und eine Nachkommastelle, das `°` nur ein Viertel so groß |
| Luftfeuchte | darunter, etwa halb so große Schrift, Format `xx,x %rF` |
| Luftdruck | darunter, `967 hPa` |
| Hintergrund | JPEG-Wallpapers nach Jahreszeit (Frühling/Sommer/Herbst/Winter) und Tageszeit (Morgen/Tag/Abend/Nacht), je Slot 3 Bilder zufällig, insgesamt 48 |
| Sonnenauf-/-untergang | Werte von sunrise-sunset.org für 48.4149 / 11.0997 – **läuft** |
| Schnittstelle | HTTP-Endpunkt, der Temperatur und Luftfeuchte als JSON liefert (später Abruf durch eine Wetterstation) |
| Ausrichtung | **Hochformat 240×320**: (0,0) oben links, x bis 239 nach rechts, y bis 319 nach unten; das Panel steht um 90° nach rechts gedreht im Gehäuse |

Die Wallpaper liegen am PC unter `backgrounds/<jahreszeit>/<tageszeit>/`, je drei
JPEG-Dateien pro Ordner (zusammen 729 KB). Sie kommen in die
littlefs-Partition `storage`, siehe [Flash-Aufteilung](#flash-aufteilung).

## Anzeige-Layout (Hochformat)

Die Bilder enthalten bereits zwei Glasflächen (Glassmorph-Effekt) für die
Texte; die Anwendung zeichnet nur noch die Werte hinein.

| Element | Bereich | Umsetzung |
|---|---|---|
| Temperatur | obere Kachel, frei positionierbar (aktuell x 25, Grundlinie y 160, `°`-Ecke 215/40) | `xx,x` mit 120 px hohen Ziffern, Komma und Nachkommastelle halb so groß und unten bündig, `°` separat platziert – [bigdigit.c](main/bigdigit.c), Werte in [layout.h](main/layout.h) |
| Luftfeuchte, Luftdruck, Datum/Zeit | untere Kachel x 25…214, y 205…294 | drei zentrierte Zeilen `xx,x %rF`, `xxx hPa`, `dd.mm. hh:mm` – [infotile.c](main/infotile.c), Werte in [layout.h](main/layout.h) |

Die Temperatur wird mit der eingebetteten **TrueType-Schrift Open Sans** gezeichnet
([ttf.c](main/ttf.c), Renderer `stb_truetype`, Font liegt als `EMBED_FILES` im
Flash und wird ohne RAM-Kopie gelesen). Vorteil gegenüber den Bitmap-Fonts: jede
Größe, saubere Kantenglättung, echte Schriftformen – und derselbe Renderer deckt
auch Uhrzeit, Datum und Luftfeuchte ab.

**Wichtig:** `stb_truetype` holt seine Zwischenpuffer (Glyphen-Vertices, Kurven-
und Kantenlisten, Bitmap) aus dem Heap. Upstream fordert für die Vertices eines
120-px-Glyphen einen Block von **56 KB** am Stück an (2000 Einträge à 28 Byte) –
und eine fehlgeschlagene Allokation ist dort **stumm**: das Glyph bleibt einfach
leer. Genau das passierte, sobald WiFi und TLS den Heap zerstückelt hatten (nur
noch 43 KB zusammenhängend): Das Hintergrundbild blieb, alle Texte verschwanden.
Deshalb ist in [stb_truetype.h](main/stb_truetype.h) die Chunk-Größe auf 128/64/32
reduziert (stb hängt bei Bedarf weitere Chunks an), und jede Textausgabe wird
nach dem Zeichnen geprüft – ein Band ohne Textpixel erzeugt eine Fehlerzeile im
Log statt einer leeren Anzeige.

### Ein voller Framebuffer statt Bänder

Der ESP32-S3 hat genug internen RAM für einen vollständigen Framebuffer
(240 × 320 × 2 Byte = **150 KB**, gemessen 153 600 Byte). Der Canvas wird mit
`MALLOC_CAP_DMA` **vor** dem Start des WiFi-Tasks angelegt, deshalb ist der
zusammenhängende Block sicher frei; PSRAM wird dafür bewusst nicht genutzt
(SPI-DMA aus PSRAM ist langsamer und war Gegenstand von Errata):

1. `wallpaper_decode()` liest das JPEG in den RAM (12–23 KB) und prüft die Größe
2. `wallpaper_paint()` dekodiert das Bild mit **TJpgDec** ([main/tjpgd](main/tjpgd))
   einmal in den vollen Canvas; danach zeichnen `bigdigit`/`infotile` ihre Texte
   mit Bildschirmkoordinaten darüber, dann geht das Bild per DMA an das Display.

Gemessen auf dem Gerät: **Dekodieren 16 ms, kompletter Frame 143 ms** (die
Anzeige wird nur bei geänderten Werten neu gezeichnet). Die Bandlogik bleibt als
Option erhalten: `LAYOUT_BAND_HEIGHT 160` in [main/layout.h](main/layout.h)
zeichnet wieder zwei Bänder à 77 KB, dann läuft TJpgDec zweimal pro Frame.

### RAM-Budget (Ist-Stand)

| Posten | Größe |
|---|---|
| interner SRAM insgesamt | 512 KB |
| PSRAM (OPI, 80 MHz) | 8 MB |
| Canvas 240×320 (voller Frame) | 150 KB |
| JPEG im RAM (größtes Wallpaper) | ~23 KB |
| TJpgDec-Arbeitsspeicher | 4 KB |
| **frei nach der Initialisierung** (inkl. PSRAM) | **~8,5 MB** |

Der Konfigurationsmodus (Access Point, zwei HTTP-Server, mDNS) läuft mit
demselben Canvas; die Sonnenzeiten-Abfrage belegt während des TLS-Handshakes
kurzzeitig ~25–30 KB und gibt sie danach wieder frei.

### Layout einstellen – [main/layout.h](main/layout.h)

Alle Werte der Anzeige stehen in einer Datei, geändert wird nur dort. Die
Koordinaten gelten im Hochformat: (0,0) oben links, x bis 239 nach rechts, y bis
319 nach unten.

| Makro | Bedeutung | aktuell |
|---|---|---|
| `LAYOUT_TEMP_X` | linke Kante der ersten großen Ziffer | 25 |
| `LAYOUT_TEMP_BASELINE_Y` | Grundlinie = Unterkante der großen Ziffern; Komma-Schweif und Nachkommastelle enden hier ebenfalls | 160 |
| `LAYOUT_TEMP_MAX_WIDTH` | Breite, auf die die Zeichengruppe höchstens gestaucht wird | 190 |
| `LAYOUT_TEMP_DIGIT_HEIGHT` | Höhe der beiden Ziffern vor dem Komma | 120 |
| `LAYOUT_TEMP_MIN_SQUEEZE` | stärkste horizontale Stauchung; `1.0` = nie stauchen, `0.5` = sehr schmal | 0.55 |
| `LAYOUT_TEMP_DECIMAL_SCALE` | Komma + Nachkommastelle relativ zur Ziffernhöhe | 0.5 |
| `LAYOUT_TEMP_DEGREE_X` | rechte Kante des `°` | 215 |
| `LAYOUT_TEMP_DEGREE_Y` | Oberkante des `°` | 40 |
| `LAYOUT_TEMP_DEGREE_SCALE` | `°` relativ zur Ziffernhöhe | 0.5 |

Die Positionen von Ziffernblock und Gradzeichen sind damit unabhängig: `x` und
Grundlinie gehören zum Zahlentext, `DEGREE_X/Y` bezeichnen die **obere rechte
Ecke der Tinte** des Gradzeichens. Beides lässt sich frei auf dem Bildschirm
platzieren, auch überlappend.

Für die untere Kachel gilt dasselbe Prinzip: ein Rechteck plus drei
Zeilenhöhen, der Block wird darin automatisch mittig gesetzt.

| Makro | Bedeutung | aktuell |
|---|---|---|
| `LAYOUT_INFO_X` / `LAYOUT_INFO_Y` | linke obere Ecke des Textbereichs | 25 / 205 |
| `LAYOUT_INFO_W` / `LAYOUT_INFO_H` | Breite und Höhe des Textbereichs | 190 / 90 |
| `LAYOUT_INFO_HUMIDITY_CAP` | Ziffernhöhe der Feuchtezeile `xx,x %rF` | 24 |
| `LAYOUT_INFO_PRESSURE_CAP` | Ziffernhöhe der Druckzeile `xxx hPa` | 18 |
| `LAYOUT_INFO_TIME_CAP` | Ziffernhöhe der Zeile `dd.mm. hh:mm` | 18 |
| `LAYOUT_INFO_GAP` | Abstand zwischen den Zeilen | 8 |
| `LAYOUT_INFO_MIN_SQUEEZE` | stärkste Stauchung je Zeile; greift sie nicht, wird die Zeile kleiner | 0.6 |
| `LAYOUT_OUT_TEMP_BASELINE_Y` | Grundlinie der Außentemperatur-Zeile | 180 |
| `LAYOUT_OUT_TEMP_CAP` | Versalhöhe dieser Zeile | 24 |
| `LAYOUT_OUT_TEMP_PREFIX` | Text davor (`""` = nur die Zahl) | `"außen "` |
| `LAYOUT_CONFIG_TRIGGER_X/Y/W/H` | Rechteck, das den Konfigurationsmodus öffnet (Ecke oben rechts) | 180, 0, 60, 60 |
| `LAYOUT_CONFIG_HOLD_MS` | so lange muss der Finger dort liegen | 500 |
| `LAYOUT_CONFIG_BACK_Y/H` | Zurück-Schaltfläche, volle Breite | 264, 56 |
| `LAYOUT_CONFIG_TITLE_CAP` … `_BUTTON_CAP` | Ziffernhöhen des Einrichtungsbildschirms | 22/12/14/26/13/18 |
| `LAYOUT_CONFIG_*_Y` | Grundlinien der Zeilen | siehe Datei |

Dazu kommt `LAYOUT_BAND_HEIGHT` (160): die Höhe der Bänder, in denen gezeichnet
wird (siehe oben). Sie muss `BOARD_SCREEN_HEIGHT` ohne Rest teilen und bestimmt
Speicherbedarf **und** Rechenzeit: 160 Zeilen = 77 KB Puffer und zwei
Decoder-Durchläufe, 80 Zeilen = 38 KB und vier Durchläufe.

Jede Zeile wird waagerecht um `LAYOUT_INFO_X + LAYOUT_INFO_W/2` zentriert und
steht mit ihrer Unterkante auf der jeweiligen Grundlinie. Die Summe aus
Zeilenhöhen und Zwischenräumen wird senkrecht im Bereich zentriert – wird
`LAYOUT_INFO_H` geändert, wandert der Block automatisch mit.

Ablauf zum Ausprobieren: Wert ändern → `.\idf build` → `.\idf -p COM12 app-flash`
→ im Monitor die Zeilen ansehen:

```
bigdigit: "22,2" cap 120 px, scale x/y 0.060/0.082, from x=25 on baseline y=160, 187 px wide
bigdigit: degree: 27x25 px ink, top right corner at (215,40)
infotile: area x 25..214, y 205..294, centre line at x=120, block 76 px high from y=212
infotile:   line 1 "47,2 %rF": cap 24 px, baseline y=236, 130 px wide
infotile:   line 2 "967 hPa": cap 18 px, baseline y=262, 92 px wide
infotile:   line 3 "18.10. 13:44": cap 18 px, baseline y=288, 136 px wide
```

Regeln, die der Renderer anwendet:

* Die Geometrie-Zeilen erscheinen einmal pro Frame (nur beim ersten Band), nicht
  viermal.
* Die Ziffern-Gruppe wird horizontal gestaucht, bis sie in `MAX_WIDTH` passt –
  aber höchstens bis `MIN_SQUEEZE`. Ist selbst das zu wenig, verringert der
  Renderer die Ziffernhöhe, damit die Proportionen lesbar bleiben. Für die
  Infozeilen gilt dasselbe mit `LAYOUT_INFO_W` und `LAYOUT_INFO_MIN_SQUEEZE`.
* Komma und Nachkommastelle sitzen mit ihrer Unterkante auf der Grundlinie; der
  Komma-Schweif endet ebenfalls dort.
* Das Gradzeichen wird an `DEGREE_X/Y` ausgerichtet, egal wie groß es ist.
* Feuchte und Druck kommen mit einer bzw. drei Nachkommastellen aus dem Sensor
  (`47,1 %rF` mit deutschem Komma, `967 hPa`). Datum und Uhrzeit kommen aus der
  NTP-Uhr; solange die nicht synchronisiert ist, zeigen die Zeilen die
  Rückfallwerte `APP_FALLBACK_*` aus [main/main.c](main/main.c) (`01.01. 00:00`).

## WLAN einrichten – [main/configmode.c](main/configmode.c), [main/netcfg.c](main/netcfg.c)

SSID, Passwort und Standort müssen **nicht mehr im Quelltext** stehen: das Gerät
lässt sich direkt am Bildschirm bzw. über eine kleine Webseite einrichten, und
die Werte überleben den Neustart im NVS.

### Wann der Konfigurationsmodus startet

| Auslöser | Bedingung |
|---|---|
| Netzwerk nicht erreichbar | Verbindungsversuch mit dem **stärksten Access Point** (siehe unten); scheitert er, startet der Konfigurationsmodus |
| Finger im Eck | Rechteck `LAYOUT_CONFIG_TRIGGER_*` in [layout.h](main/layout.h) = x 180…239, y 0…59, **500 ms halten** (`LAYOUT_CONFIG_HOLD_MS`), damit ein Streifen nicht umschaltet |

Der Bildschirm zeigt dann Hotspot-Name, Passwort und Adresse:

```
        WLAN einrichten
     kein Netzwerk - bitte einrichten
1. Mit dem Hotspot verbinden
          thermometer
      Passwort: thermometer
2. Im Browser öffnen
      http://192.168.4.1
<- zurück zur APP   (ganze untere Kante)
```

Am Telefon oder PC mit dem Hotspot `CONNECT_AP_SSID` verbinden, im Browser
`http://192.168.4.1` öffnen. Die Seite zeigt oben den Gerätenamen und bietet:

| Feld | Zweck |
|---|---|
| **Name dieses Geräts** | erscheint im Browser, im JSON, im Log und als Hostname im Router (siehe unten) |
| **Netzwerk** | Auswahlliste der gefundenen Netze (nach Signalstärke, **ein Eintrag pro Name** – Mesh und 2,4/5-GHz-Doppeleinträge werden zusammengefasst) |
| **Anderes Netzwerk** | Freitext für versteckte oder gerade nicht sichtbare Netze, hat Vorrang vor der Liste |
| **Passwort** | leer lassen für ein offenes Netz |
| **Breite / Länge** | Koordinaten für die Sonnenzeiten, leer = bisheriger Wert bleibt |
| **Außensensor** | IP, Name oder `host:port` eines zweiten Geräts, leer = keiner (wird alle 60 s abgefragt) |

| Schaltfläche | Ergebnis |
|---|---|
| **Speichern** | Werte in NVS, Antwortseite, **Neustart** nach 1,5 s → Anmeldung im neuen Netz |
| **Abbrechen** | nichts wird geändert, Seite kann geschlossen werden |
| **Netzwerke neu suchen** | neuer Scan |
| **`<- zurück zur APP`** (am TFT) | NVS-Merker „Konfiguration übersprungen“, **Neustart** → Anzeige läuft ohne Netz weiter |

Beide Wege enden in einem Neustart. Das ist Absicht: der Netzwerkteil läuft in
einer eigenen Task, und ein Neustart ist der einzige Zustand, der garantiert
sauber ist. Der Merker „übersprungen“ verhindert, dass ein Gerät ohne WLAN bei
jedem Start wieder im Konfigurationsmodus landet – der Eck-Touch funktioniert
weiterhin. Ein neues Speichern löscht den Merker.

### Werte und Speicherung

`connect.h` liefert nur noch die **Vorgaben für den ersten Start**. Sobald etwas
im NVS steht, gilt das:

| NVS-Schlüssel (Namespace `net`) | Inhalt |
|---|---|
| `ssid`, `pass` | WLAN-Zugang |
| `lat`, `lon` | Koordinaten als Text (Dezimalgrad) |
| `name` | Gerätename, Vorgabe `CONNECT_DEVICE_NAME` = `Raumthermometer` |
| `remote` | IP, Name oder `host:port` des Außensensors (ohne `http://`, ohne `/`), leer = keiner |
| `off` | Temperatur-Offset in Zehntelgrad (Ganzzahl, `-15` = −1,5 °C) |
| `skip` | 1 = Konfigurationsmodus beim Start nicht automatisch öffnen |

### Verbindungsaufbau: der stärkste Access Point gewinnt

Mesh-Repeater und Dual-Band-Router melden denselben Namen mehrfach – der Treiber
sucht sich aber nicht zuverlässig den stärksten aus. Deshalb scannt
[wifi.c](main/wifi.c) vor dem Verbinden alle BSSIDs und bindet die Verbindung an
den stärksten Eintrag:

```
I (8066) cyd: scan: "FRITZ!Box 7590 j.oe" 2c:91:ab:5e:e5:6a channel 1, -45 dBm
I (8066) cyd: scan: "FRITZ!Box 7590 j.oe" 3c:37:12:56:48:bb channel 6, -62 dBm
I (8096) cyd: "FRITZ!Box 7590 j.oe" strongest at -45 dBm
I (8096) cyd: using the strongest access point of "FRITZ!Box 7590 j.oe"
```

* `wifi_scan_all()` liefert **jede** BSSID einzeln (die Auswahlliste der
  Einrichtungsseite fasst gleiche Namen weiterhin zusammen).
* `wifi_connect_bssid()` setzt `sta.bssid_set` und verbindet gezielt dorthin.
* **Ausfall:** Antwortet dieser Access Point nicht mehr, löst der
  Disconnect-Handler die Bindung (`choosing freely again`) und lässt den Treiber
  neu wählen – statt aufzugeben.
* **Kein Abbruch wegen schwachem Signal mehr:** `CONNECT_MIN_RSSI` (-85 dBm) ist
  nur noch ein Hinweis im Log (`is weak … - trying anyway`). Vorher wurde der
  Verbindungsversuch ganz übersprungen, was eine Unit mit knappem Empfang
  unnötig in den Konfigurationsmodus schickte.
* Nach dem Verbinden steht im JSON, **welcher** Access Point es geworden ist
  (`ssid`, `bssid`, `channel`).

### Der Gerätename

Mit mehreren Einheiten im Haus bekommt jede einen eigenen Namen. Er steht

* im Browser (Seitenüberschrift und `<title>`),
* im JSON als Feld `name`,
* im seriellen Log (`device name "..."`),
* **als DHCP-Hostname im Router** – daraus lässt sich das Gerät ohne
  IP-Adresse erreichen.

Dafür wird der Name auf Hostnamen-Zeichen reduziert: Kleinschreibung, Umlaute
ausgeschrieben, alles andere wird zu `-`.

| Name im Formular | Hostname | erreichbar als |
|---|---|---|
| `Raumthermometer` | `raumthermometer` | `http://raumthermometer.local/` |
| `Thermometer Küche` | `thermometer-kueche` | `http://thermometer-kueche.local/` |

Damit sind **zwei** Wege möglich, das Gerät ohne IP-Adresse zu erreichen:

1. **`<hostname>.local`** – über mDNS, funktioniert in jedem Netz, ohne Zutun des
   Routers (siehe [mDNS](#mdns-erreichbarkeit-ohne-router-dns-discoveryc))
2. **`<hostname>.<domain-des-routers>`** – über das lokale DNS des Routers, z. B.
   `thermometer-kueche.fritz.box`. Die Domain hängt vom Router ab (`fritz.box`,
   `lan`, `speedport.ip`, bei manchen gar keine) und steht am PC in
   `ipconfig /all` als „Verbindungsspezifisches DNS-Suffix“.

Nach dem Ändern des Namens einmal neu starten, dann ist er neu angemeldet.

```c
netcfg_t cfg;
if (netcfg_load(&cfg)) { … }   /* false = noch nichts gespeichert, connect.h gilt */
```

Den Modus erzwingen (zum Testen von Hotspot und Seite):

```c
#define APP_FORCE_CONFIG 1   /* main/main.c, danach zurück auf 0 */
```

**Achtung:** solange der Schalter auf 1 steht, versucht das Gerät *nie* eine
Verbindung – es landet nach jedem Start wieder in der Einrichtung. Der Build
warnt dann (`#warning "APP_FORCE_CONFIG is 1 ..."`), und im Log steht eine
`E`-Zeile mit `!! APP_FORCE_CONFIG is 1 !!`.

### Die Seite selbst

Der HTTP-Server (`esp_http_server`) läuft auf Port 80 mit vier Sockets und
8 KB Task-Stack; die Seite wird pro Anfrage in einem 6-KB-Heap-Puffer
zusammengesetzt und danach wieder freigegeben. **Port 80 ist auch der
Messwert-Endpunkt** – deshalb stoppt der Konfigurationsmodus ihn zuerst
(`webserver_stop()`, Logzeile `readings endpoint stopped`). Sonst würde
`http://192.168.4.1` weiterhin die Messwertseite zeigen, weil beide Server auf
demselben Port lauschen.

### DNS-Antworter und die Anmelde-Prüfungen der Handys

Ein Handy prüft nach dem Verbinden, ob das Netz Internet hat: erst eine
DNS-Abfrage (Android nach `connectivitycheck.gstatic.com`), dann ein HTTP-Abruf
(`/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`). Ohne Antworten markiert
es das WLAN als „kein Internet“, zeigt ein Ausrufezeichen und schickt den
Datenverkehr über Mobilfunk – die Einrichtungsseite ist dann nicht mehr
erreichbar.

Deshalb läuft im Konfigurationsmodus ein **minimaler DNS-Server**
([configmode.c](main/configmode.c), `dns_task`): Er beantwortet **jeden** Namen
mit der Adresse des Access Points, und der Access Point wird den Clients als DNS
Server angeboten (`esp_netif_set_dns_info`). Die Prüfabrufe beantwortet der
Webserver wie ein funktionierendes Internet:

| Pfad | Antwort | für |
|---|---|---|
| `/generate_204`, `/gen_204` | `204 No Content` | Android, Chrome |
| `/hotspot-detect.html`, `/library/test/success.html` | `Success`-Seite | Apple |
| `/connecttest.txt`, `/ncsi.txt` | `Microsoft Connect Test` / `Microsoft NCSI` | Windows |
| alles andere | `302` auf `/` | Tippfehler, beliebige Adressen |

Damit bleibt das Handy im Unit-Netz, und der Browser landet zuverlässig auf dem
Formular (`httpd_uri_match_wildcard` ist dafür aktiviert).

### Hintergrund-Scan und Neustart nach 15 Minuten

* Der Netzwerk-Scan läuft in einer eigenen Task alle
  `CONFIGMODE_SCAN_INTERVAL_MS` (20 s). Er wechselt kurz den Kanal und würde die
  Seite sonst sekundenlang blockieren; so antwortet sie sofort.
  „Netzwerke neu suchen“ setzt nur ein Flag, der Scan startet dann gleich neu.
* War **15 Minuten** kein Client verbunden und wurde nichts gespeichert
  (`CONFIGMODE_IDLE_RESTART_MS`), startet die Unit neu und versucht wieder das
  gespeicherte Netz. Sonst bliebe eine bereits montierte Unit für immer im
  Access Point, wenn beim Einschalten kurz kein Empfang war. Client-Verbinden
  und jeder Seitenaufruf setzen die Uhr zurück.
* Der Modus beendet auch die Netzwerk-Task (`configuration mode active, the
  network task stops`) – NTP- und HTTPS-Versuche ohne Verbindung wären nur
  Lograuschen.

## WLAN, Uhrzeit und Sonnenzeiten – [main/connect.h](main/connect.h), [main/wifi.c](main/wifi.c), [main/clock.c](main/clock.c), [main/sun.c](main/sun.c)

Alles, was zur Installation gehört, steht in **`main/connect.h`**: SSID, Passwort,
Hostname, NTP-Server, Zeitzone und die Koordinaten für sunrise-sunset.org – als
Vorgaben, änderbar über die Konfigurationsseite (siehe oben).

```c
#define CONNECT_WIFI_SSID       "FRITZ!Box 7590 j.oe"
#define CONNECT_WIFI_PASSWORD   "…"
#define CONNECT_SNTP_SERVER     "de.pool.ntp.org"
#define CONNECT_TZ              "CET-1CEST,M3.5.0,M10.5.0/3"   /* Europe/Berlin */
#define CONNECT_LATITUDE_TEXT   "48.41490736914154"
#define CONNECT_LONGITUDE_TEXT  "11.099721982616911"
```

`connect.h` enthält das WLAN-Passwort und steht deshalb in [.gitignore](.gitignore).
Im Repository liegt nur die Vorlage [main/connect.example.h](main/connect.example.h):

```powershell
Copy-Item main\connect.example.h main\connect.h   # einmalig, dann Werte eintragen
```

Ablauf beim Start (der Netzwerkteil läuft in einer eigenen Task, die Anzeige
startet also sofort):

1. `clock_init()` setzt die Zeitzone (`setenv("TZ", …)` + `tzset()`), damit
   `localtime_r()` Ortszeit liefert – inklusive Sommerzeit
2. `wifi_init()` bringt den Treiber im Station-Modus hoch, `wifi_scan_rssi()`
   prüft, ob das Netz überhaupt in Reichweite ist, dann `wifi_connect()` und
   warten auf die IP-Adresse. Nach einem Verbindungsabbruch wird neu verbunden;
   war das Gerät schon einmal verbunden, endlos (Router-Neustart braucht keinen
   Geräte-Neustart)
3. `clock_start_sntp()` + `clock_wait_for_sync()`: SNTP-Polling, ohne die Anzeige
   zu blockieren
4. `sun_fetch()` holt Sonnenauf-/-untergang, danach alle
   `CONNECT_SUN_REFRESH_S` (6 h) erneut
5. Die Uhrzeit erscheint in der unteren Kachel, sobald sie da ist – der
   Neuzeichen-Takt (alle 10 s) merkt die Änderung selbst. Bis dahin stehen die
   Rückfallwerte `APP_FALLBACK_*` aus [main.c](main/main.c) (`01.01. 00:00`)

### Woher die Sonnenzeiten kommen

Der Dateninput sind **zwei Koordinaten**: die im NVS gespeicherten, sonst die
Vorgaben in `connect.h`. Daraus baut `netcfg_sun_url()` die Adresse:

```c
#define CONNECT_LATITUDE_TEXT   "48.41490736914154"   /* Breite, Nord positiv, Süd negativ */
#define CONNECT_LONGITUDE_TEXT  "11.099721982616911"  /* Länge, Ost positiv, West negativ */

netcfg_sun_url(url, sizeof(url));
/* -> https://api.sunrise-sunset.org/json?lat=…&lng=…&formatted=0 */
```

Für einen anderen Aufstellort also nur die beiden Werte ändern – am einfachsten
auf der Konfigurationsseite (dort stehen sie im Formular), sonst als Vorgabe in
`connect.h`. Dezimalgrad mit Punkt, z. B. `"53.5511"` / `"9.9937"` für Hamburg;
URL und Abfrage folgen automatisch. Werte mit zu vielen Nachkommastellen sind
unkritisch, die API rundet selbst.

Der Ablauf in [sun.c](main/sun.c):

1. HTTPS-GET auf die von `netcfg_sun_url()` gebaute Adresse
   (`esp_http_client`, Zertifikatsprüfung gegen das CA-Bundle im Flash)
2. Antwort ist JSON, daraus werden `status` und die beiden Felder `sunrise` und
   `sunset` gelesen, z. B. `"sunrise":"2026-10-18T05:38:23+00:00"` (UTC)
3. Umrechnung UTC → Ortszeit über `localtime_r()`, also inklusive Sommerzeit
4. Ablage mit `clock_set_sun_times()`; lesbar über `clock_sunrise()` /
   `clock_sunset()` (jeweils `hour`/`minute`/`valid`)

Weil die Antwort nur aus einer flachen Liste fester Felder besteht, extrahiert
`sun.c` die zwei Werte direkt aus dem Text, statt einen JSON-Parser
einzubinden (kein zusätzliches Modul, ~1 KB Flash). Kommt künftig mehr aus der
API, wäre ein richtiger Parser der nächste Schritt.

Bis zur ersten erfolgreichen Abfrage stehen in [clock.c](main/clock.c) die
Platzhalter 07:39 / 18:19 mit `valid = false`; nach dem Abruf sind sie gültig und
`clock_log_now()` protokolliert sie ohne den Zusatz „(placeholder)".

### HTTPS und Zertifikate

`api.sunrise-sunset.org` hängt bei Google Trust Services; das Zertifikat wird von
`WE1` ausgestellt, die Kette endet bei `GTS Root R4` – allerdings in der
**cross-signierten** Variante (ausgestellt von GlobalSign Root CA, die nicht im
CA-Bundle von ESP-IDF liegt). Deshalb steht in
[sdkconfig.defaults](sdkconfig.defaults):

```
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y
```

Ohne diese Option bricht der Handshake mit `esp-x509-crt-bundle: No matching
trusted root certificate found` ab (`mbedtls_ssl_handshake returned -0x3000`).
Ebenso wichtig: `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` bleibt bei 16 KB, denn die
Zertifikatskette kommt als ein TLS-Record und passt nicht in 4 KB.

Die TLS-Sitzung braucht während der Abfrage etwa 25–30 KB Heap; danach ist der
Speicher wieder frei (die Anzeige läuft während der Abfrage weiter, weil sie in
der Thermometer-Task läuft).

## Messwerte über HTTP abrufen ? [main/webserver.c](main/webserver.c)

Sobald das Gerät im Heimnetz angemeldet ist, läuft ein kleiner HTTP-Server auf
Port 80. Er greift auf denselben Messwertspeicher zu, den die Anzeige f?llt
([main/sensor.c](main/sensor.c) h?lt den letzten Satz mit Zeitstempel), also
kostet eine Abfrage weder I2C-Zeit noch einen Bild-Neuaufbau.

| Adresse | Inhalt |
|---|---|
| `GET /api/values` | JSON, gedacht für andere Geräte und Skripte |
| `GET /api` | dasselbe (kurze Form) |
| `GET /` | kleine Seite f?r den Browser, aktualisiert sich alle 10 s |
| `http://…:8080/` | **zweiter Server, nur die Seite** – kein JSON (siehe unten) |

```json
{"name":"Thermometer Küche","valid":true,"temperature_c":22.6,"humidity_rh":39.0,
 "pressure_hpa":963.8,"offset_c":-1.5,"age_s":7,"time":"24.10. 23:12",
 "ip":"192.168.123.192","rssi_dbm":-52,"uptime_s":23,"ssid":"FRITZ!Box 7590 j.oe",
 "bssid":"2c:91:ab:5e:e5:6a","channel":1,"remote":""}
```

| Feld | Bedeutung |
|---|---|
| `name` | Gerätename aus der Einrichtung (Vorgabe `Raumthermometer`) |
| `valid` | `false` = noch kein Messwert (Sensor fehlt oder Boot gerade erst) ? dann sind die drei Zahlen `null` |
| `temperature_c`, `humidity_rh`, `pressure_hpa` | Zahlen mit **Punkt** als Dezimaltrennzeichen (JSON) und einer Nachkommastelle |
| `age_s` | Alter des Messwerts in Sekunden (die Anzeige liest alle 10 s) |
| `time` | Ortszeit `dd.mm. hh:mm`; vor der NTP-Antwort der R?ckfallwert `01.01. 00:00` |
| `ip`, `rssi_dbm`, `uptime_s` | Adresse, Funkfeldst?rke, Laufzeit seit dem Start |
| `remote` | eingestellte Adresse des Außensensors, `""` = keine |
| `outdoor_c` | Temperatur, die dieses Gerät vom Außensensor geholt hat, `null` solange keine |
| `offset_c` | eingestellter Temperatur-Offset (siehe [Temperatur kalibrieren](#temperatur-kalibrieren-offset)) |
| `ssid`, `bssid`, `channel` | Access Point, mit dem die Unit verbunden ist – im Mesh teilen sich mehrere die SSID, die BSSID sagt welcher |

Die IP-Adresse steht im Log (`webserver: readings at http://?/api/values`) und
auf der Seite selbst; im Router heißt das Gerät `raumthermometer`
(`CONNECT_HOSTNAME`).

### Zwei Server: Port 80 für alles, Port 8080 nur für die Seite

`WEBSERVER_PUBLIC_PORT` in [main/webserver.h](main/webserver.h) (Standard 8080)
startet einen zweiten HTTP-Server, der **ausschließlich `/` kennt**. Wer diesen
Port im Router nach außen weiterleitet, gibt damit nur die Anzeigeseite frei:
`/api/values` existiert dort überhaupt nicht und antwortet mit 404 – ein
versehentlich mitveröffentlichter JSON-Endpunkt ist so ausgeschlossen. Die Seite
auf 8080 lässt außerdem den Hinweis auf den JSON-Pfad weg. Port 80 bleibt der
vollständige Server im lokalen Netz.

Beide Server werden gemeinsam gestartet (`webserver_start()`) und gemeinsam
gestoppt (`webserver_stop()`), bevor der Konfigurationsmodus Port 80 übernimmt.
Auf 0 gesetzt bleibt der zweite Server aus und spart ~8 KB Heap.

### Am PC testen

Am einfachsten über den Namen (siehe [Der Gerätename](#der-gerätename)) – dann
muss man die IP nicht kennen. Mit `.local` geht es in jedem Netz:

```
http://thermometer-kueche.local/
http://thermometer-kueche.local/api/values
```

Im Heimnetz der FRITZ!Box funktioniert genauso der dort angemeldete Name:

```
http://thermometer-kueche.fritz.box/api/values
```

**Browser:** die Adresse oben ohne Pfad – zeigt die Werte groß, die
JSON-Adresse steht darunter als Hinweis.

**PowerShell** (in diesem Windows ist `curl` ein Alias auf `Invoke-WebRequest`,
deshalb `curl.exe`):

```powershell
curl.exe -s http://thermometer-kueche.local/api/values
curl.exe -s -i http://thermometer-kueche.fritz.box/api/values   # mit HTTP-Kopfzeilen
Invoke-RestMethod http://thermometer-kueche.fritz.box/api/values # direkt als Objekt
```

Statt des Namens geht auch die IP aus dem Log (`ip` im JSON, `webserver: readings
at http://…`).

**Eingabeaufforderung (cmd.exe):**

```bat
curl http://thermometer-kueche.fritz.box/api/values
```

**Nur ein Feld herausziehen**, z. B. f?r ein Skript:

```powershell
(Invoke-RestMethod http://thermometer-kueche.fritz.box/api/values).temperature_c
curl.exe -s http://thermometer-kueche.fritz.box/api/values | python -c "import json,sys; print(json.load(sys.stdin)['pressure_hpa'])"
```

**Dauerhaft mitschreiben** (eine Zeile pro 10 s, bis Strg+C):

```powershell
while ($true) { curl.exe -s http://thermometer-kueche.fritz.box/api/values; Write-Output ''; Start-Sleep 10 }
```

Kleine Stolperfallen: `http://` nicht vergessen, das Gerät antwortet nur im
selben Netz (kein Portforwarding). Der Name im Router bleibt stabil, auch wenn sich
die IP per DHCP ändert – deshalb in Skripten lieber den Namen verwenden. Klappt die
Namensauflösung nicht (anderer Router, Gästenetz), im Router nachsehen, unter
welcher IP und welchem Namen `thermometer-kueche` steht.

## Temperatur kalibrieren (Offset) - [main/netcfg.c](main/netcfg.c), [main/sensor.c](main/sensor.c)

Der SHT30 sitzt im Gehäuse und misst deshalb etwas zu warm. Statt im Code zu
rechnen, lässt sich ein Offset auf der Einrichtungsseite
(`http://192.168.4.1`, Feld **Temperatur-Offset in °C**) eintragen:

| Punkt | Wert |
|---|---|
| Eingabe | Dezimalkomma **oder** Punkt, `-0,5` und `-0.5` sind gleichwertig |
| Bereich | −5,0 bis +5,0 °C, in 0,1-Schritten; leer oder `0` = keine Korrektur |
| Ablage | NVS-Schlüssel `off` (Namespace `net`) als Ganzzahl in Zehntelgrad |
| Wirkung | `sensor_read()` addiert den Wert auf **jeden** Messwert |

Weil die Korrektur in [main/sensor.c](main/sensor.c) direkt nach dem Auslesen
passiert, zeigen TFT-Anzeige, JSON-Endpunkt und der Messwertspeicher denselben
Wert – es gibt keine Stelle, die den Offset vergessen könnte. Das JSON führt den
Offset zusätzlich als `offset_c`, damit ein Skript Roh- und Anzeigewert
auseinanderhalten kann.

Beim Speichern wird der Wert auf ganze Zehntel gerundet (kaufmännisch), damit
`0,04` nicht zu `0,0` wird. Der Log bestätigt die Übernahme:

```
I (507) netcfg: stored "Thermometer Küche": WLAN "FRITZ!Box 7590 j.oe", location …, offset -1.5 C, outdoor sensor -
I (5406) cyd: device name "Thermometer Küche", offset -1.5 C, outdoor sensor not set
I (5716) bigdigit: "22,5" cap 120 px …
```

**Wichtig beim Ändern:** Das Passwortfeld ist beim Öffnen der Seite immer leer.
Wer nur den Offset ändert, lässt es leer – bei unveränderter SSID bleibt dann das
gespeicherte Passwort erhalten (`web: password empty on an unchanged network,
keeping the stored one`). Wird dagegen eine **andere** SSID gewählt, gilt ein
leeres Feld als offenes Netz.

## Außentemperatur vom Partnergerät - [main/outdoor.c](main/outdoor.c)

Ein zweites Thermometer im Netz (der SHT31-Sensor, siehe
[PORTING.md](PORTING.md)) liefert seine Werte unter `GET /api/values`. Diese
Unit holt dort die Temperatur ab und zeigt sie unter der Raumtemperatur an.

```
I (12897) outdoor: aussenthermometer.local: 5,8 C
I (17907) outdoor: line "außen 5,8°": cap 24 px, baseline y=180, 164 px wide
```

| Punkt | Wert |
|---|---|
| Adresse | NVS-Schlüssel `remote`, Feld **Außensensor** der Einrichtungsseite |
| erlaubt | IP (`192.168.123.191`), Name (`aussenthermometer.local`) oder `host:port` (`nasjoe65.ddns.net:8083`) |
| **nicht** erlaubt | `http://` davor, `https://` davor, `/` oder ein Pfad dahinter – die Firmware baut selbst `http://<remote>/api/values` |
| Abfrage | `http://<remote>/api/values`, alle `OUTDOOR_REFRESH_S` (60 s) |
| Timeout | `OUTDOOR_TIMEOUT_MS`, 5 s - ein stummes Partnergerät blockiert nichts |
| Feld | `temperature_c`, nur wenn dort `"valid":true` steht |
| Ablage | Cache mit Zeitstempel, gelesen von der Anzeige-Task (kein Warten auf Netz) |

### Zugriff über DDNS / Portweiterleitung

Der JSON-Endpunkt `/api/values` läuft **auf Port 80**. Der öffentliche
Seiten-Port 8080 (`WEBSERVER_PUBLIC_PORT`) kennt **absichtlich nur die
HTML-Seite** – dort antwortet `/api/values` mit `404 Nothing matches the given
URI`. Eine Weiterleitung von außen muss deshalb auf **internen Port 80** zeigen,
nicht auf 8080.

Am PC prüfen (Vorher/Nachher der Weiterleitung):

```powershell
curl.exe -i http://nasjoe65.ddns.net:8083/api/values
```

* **200 + JSON** (`{"valid":true,"temperature_c":…}`): Port zeigt auf 80 →
  im Feld `nasjoe65.ddns.net:8083` eintragen (ohne `http://`, ohne `/`).
* **404 „Nothing matches the given URI"**: Port zeigt auf 8080 → Weiterleitung
  auf internen Port 80 ändern oder einen anderen Außenport dafür verwenden.

Empfohlen, wenn beide Geräte im selben Heimnetz hängen: die **lokale Adresse**
des Außengeräts eintragen (z. B. `192.168.123.191` oder
`aussenthermometer.local`). Das ist schneller, funktioniert ohne Internet und
setzt den JSON-Endpunkt nicht öffentlich frei. Damit die IP stabil bleibt, im
Router eine DHCP-Reservierung für das Außengerät anlegen.

Auf diesem Gerät ist genau das bereits eingerichtet und verifiziert:
`remote = "aussenthermometer.local"`, das JSON von `192.168.123.123` meldet
`"outdoor_c":8.5`. Voraussetzung dafür ist
`CONFIG_LWIP_DNS_SUPPORT_MDNS_QUERIES=y` in der `sdkconfig` – nur dann löst
`getaddrinfo()` im ESP32 einen `.local`-Namen über mDNS auf. Die Option steht
deshalb auch in [sdkconfig.defaults](sdkconfig.defaults).

Weil der Wert nur im Cache liegt, zeichnet die Anzeige unabhängig vom Netz weiter;
die Netzwerk-Task aktualisiert im Hintergrund. Erst wenn `remote` leer ist,
passiert gar nichts - dann bleibt die Zeile einfach weg.

### Aussehen und Position

Alle Werte stehen in [main/layout.h](main/layout.h):

| Makro | Bedeutung | aktuell |
|---|---|---|
| `LAYOUT_OUT_TEMP_BASELINE_Y` | Grundlinie der Zeile | 180 |
| `LAYOUT_OUT_TEMP_CAP` | Versalhöhe - so groß wie die Feuchtigkeitszeile | 24 |
| `LAYOUT_OUT_TEMP_PREFIX` | Text vor der Zahl, `""` für die nackte Zahl | `"außen "` |
| `LAYOUT_OUT_TEMP_MAX_WIDTH` | breiteste erlaubte Zeile | 190 |
| `LAYOUT_OUT_TEMP_MIN_SQUEEZE` | stärkste Stauchung | 0.6 |

Die Zeile ist horizontal um `BOARD_SCREEN_WIDTH / 2` (x=120) zentriert. Mit
Grundlinie 180 und Versalhöhe 24 reicht ihr Tintenbereich von y=156 bis y=180.
Mit dem vollen Framebuffer (`LAYOUT_BAND_HEIGHT = BOARD_SCREEN_HEIGHT`) spielt
das keine Rolle; nur wenn wieder in Bändern gezeichnet wird (160 Zeilen), liegt
der obere Teil im ersten und der Rest im zweiten Band – unkritisch, weil jedes
Band das ganze Bild malt und nur die Zeilen außerhalb abschneidet. Nur die
Grundlinie genau auf eine Bandgrenze (160/320) legen sollte man nicht.

Das JSON dieser Unit führt den geholten Wert als `outdoor_c` mit (oder `null`,
solange keiner da ist) - praktisch für eine Wetterstation, die nur die
Raumgeräte abfragt.

## mDNS: Erreichbarkeit ohne Router-DNS – [main/discovery.c](main/discovery.c)

Die DHCP-Adresse und der Hostname helfen nur, wenn der Router lokale Namen
auflöst und der Client dessen Domain benutzt. Damit das Gerät **in jedem Netz**
unter seinem Namen erreichbar ist, läuft ein mDNS-Responder
(Komponente `espressif/mdns`):

```
http://thermometer-kueche.local/
http://thermometer-kueche.local/api/values
```

Das funktioniert ohne Router-Konfiguration, auch im Gästenetz und hinter einem
Repeater mit anderem Namen. Zusätzlich wird der HTTP-Dienst als `_http._tcp`
angemeldet – damit taucht die Unit in Netzwerk-Scannern und in der
Netzwerkansicht von Windows/macOS auf.

| Punkt | Wert |
|---|---|
| Hostname | aus dem Gerätenamen, `netcfg_hostname()` (identisch zum DHCP-Namen) |
| Instanzname | der Gerätename selbst, darf Leerzeichen und Umlaute enthalten |
| Dienst | `_http._tcp` auf Port 80 |
| Speicher | gemessen 5,5 KB Heap (115396 → 109936 Bytes), größter Block − 6 KB |
| Start | nach `webserver_start()` in der Netzwerk-Task |
| Ende | `discovery_stop()` beim Eintritt in den Konfigurationsmodus |

```c
discovery_start();   /* main/main.c, nach erfolgreicher Anmeldung */
discovery_stop();    /* main/configmode.c, vor dem Access Point */
```

Der Modulname ist `discovery` und nicht `mdns`, weil ein `main/mdns.h` den Header
der mDNS-Komponente verdecken würde – dieselbe Falle wie bei `main/time.h`
(siehe [clock.h](main/clock.h)).

**Wer `.local` nicht auflösen kann:** Windows 10/11 und macOS können es von
Haus aus, unter Android erst ab Version 12 (davor hilft z. B. die App
„Bonjour Browser“ oder die IP-Adresse). In solchen Fällen bleibt der Weg über
den Router-Hostnamen oder direkt die IP aus dem JSON-Feld `ip`.

## Hardware

| Baustein | Details |
|---|---|
| Modul | ESP32-S3R8, 2 Kerne, 240 MHz, 512 KB SRAM, **8 MB OPI-PSRAM**, **16 MB QSPI-Flash** |
| Display | 2.8" **IPS**-TFT, 240×320, **ILI9341V**, 4-Draht-SPI, Backlight an GPIO45 |
| Touch | **FT6336G** (kapazitiv) an I²C, Adresse 0x38, Reset GPIO18, INT GPIO17 |
| Sensor | **UNIT ENV III**: SHT30 (0x44) für Temperatur/Feuchte, QMP6988 (0x70) für Druck – am selben I²C-Bus wie der Touch |
| Sonstiges | WS2812 an GPIO42, BOOT-Taster an GPIO0, microSD über SDIO, ES8311-Audio (ungenutzt) |

Alle verwendeten Pins stehen gesammelt in
[board_es3c28p.h](main/board_es3c28p.h); Quelle sind die Pin-Tabelle der
LCDWiki-Produktseite (https://www.lcdwiki.com/2.8inch_ESP32-S3_Display) und die
Gerätebeschreibungen des Tactility- und Bruce-Projekts.

| Funktion | GPIO |
|---|---|
| LCD SCLK / MOSI / MISO | 12 / 11 / 13 |
| LCD CS / DC | 10 / 46 |
| LCD Reset | an EN des ESP32 (deshalb `reset_gpio_num = -1`, Software-Reset) |
| LCD Backlight | 45 (High = ein, LEDC-PWM) |
| Touch SDA / SCL / RST / INT | 16 / 15 / 18 / 17 |
| I²C SDA / SCL (ENV III) | **16 / 15** (derselbe Bus wie der Touch) |
| WS2812 | 42 (GRB, RMT) |
| BOOT-Taste | 0 |
| microSD (nicht genutzt, SDIO) | CLK 38, CMD 40, D0…D3 39/41/48/47 |
| Audio ES8311 (nicht genutzt) | Enable 1 (Low = ein), MCLK 4, BCLK 5, DOUT 8, LRCK 7, DIN 6 |
| Batteriespannung (nicht genutzt) | 9 (200k/200k-Teiler) |

Das Panel ist ein **IPS-Typ** und braucht `BOARD_LCD_INVERT_COLORS=1`; ohne
Inversion erscheint das Bild wie ein Negativ. Der Touch-Controller liefert
bereits Pixelkoordinaten, deshalb gibt es weder Kalibrierwerte noch eine
Z-Schwelle – nur die Flags `BOARD_TOUCH_SWAP_XY`/`MIRROR_*` für den Fall, dass
eine andere Panelcharge anders eingebaut ist.

### Stecker am Board

| Stecker | Belegung | Nutzung hier |
|---|---|---|
| IIC (4-polig, 1,25 mm) | SDA = IO16, SCL = IO15, 3V3, GND | UNIT ENV III und Touch teilen sich diesen Bus |
| UART (4-polig) | TXD0 = IO44, RXD0 = IO43, 3V3, GND | nicht genutzt (Konsole läuft über USB) |
| Expand Pin (4-polig) | GPIO2 / 3 / 14 / 21 | frei |
| USB Type-C | nativer USB des ESP32-S3 (IO19/20) | Flash + Konsole (USB Serial/JTAG) |

Der microSD-Slot hängt an SDIO (nicht SPI); die Pins überschneiden sich nicht mit
Display, Touch oder Sensor.

## Flash-Aufteilung

16 MB Flash, custom Partitionstabelle in [partitions.csv](partitions.csv)
(`CONFIG_PARTITION_TABLE_CUSTOM=y`, `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`):

| Partition | Typ | Offset | Größe | Inhalt |
|---|---|---|---|---|
| (bootloader) | – | 0x0 | 32 KB | vom Bootloader belegt |
| (Partitionstabelle) | – | 0x8000 | 4 KB | diese Tabelle |
| `nvs` | data/nvs | 0x9000 | 24 KB | Einstellungen, WLAN-Kalibrierung |
| `phy_init` | data/phy | 0xF000 | 4 KB | RF-Kalibrierdaten |
| `factory` | app/factory | 0x10000 | **4 MB** | Anwendung (aktuell 1,4 MB) |
| `storage` | data/spiffs | 0x410000 | **4 MB** | littlefs: 48 Wallpaper |
| (frei) | – | 0x810000 | ~8 MB | ungenutzt |

Bewusste Entscheidungen:

* **4 MB App**: die Anwendung belegt 1,4 MB (inklusive eingebetteter
  Open-Sans-Schrift), also 67 % frei.
* **4 MB littlefs**: die 48 Wallpaper belegen 729 KB; der Rest ist Platz für
  weitere Bilder oder spätere Logs.
* **kein OTA-Slot**: Updates laufen über USB; die restlichen 8 MB bleiben frei.
* Der Subtyp der littlefs-Partition ist `spiffs` (generischer Datentyp) – das
  erwartet die Komponente `joltwallet/littlefs`. Die Partition wird beim ersten
  Mount automatisch formatiert (`format_if_mount_failed`).

Prüfen lässt sich die Tabelle ohne Hardware:

```powershell
.\idf partition-table        # erzeugt build/partition_table/partition-table.bin und zeigt sie an
```

Nach einer Änderung der Tabelle einmal `.\idf flash` ausführen – die Tabelle
wird dabei an 0x8000 mitgeschrieben. Ein `erase-flash` ist nicht nötig, solange
keine Partition ihre Lage ändert; die neue `storage`-Partition liegt in einem
Bereich, der vorher unbenutzt war.

## Was das Programm macht

1. Backlight aus, WS2812 initialisieren, danach **Selbsttest der LED**: rot,
   grün, blau, weiß nacheinander für je 400 ms (steht so im Log); anschließend
   übernimmt die LED-Task (siehe [RGB-LED als Ambilight](#rgb-led-als-ambilight-statusledh))
2. ILI9341 über SPI2 hochfahren (40 MHz) in den **Hochformat-Modus 240×320**,
   IPS-Inversion an, BGR-Reihenfolge
3. Canvas 240×320 anlegen (150 KB, DMA-fähig, vor dem WiFi-Task)
4. littlefs-Partition `storage` einhängen (beim ersten Mal formatieren), Belegung
   und Dateianzahl loggen
5. I²C-Bus anlegen, Adressen scannen und im Log ausgeben; SHT30 (0x44) und
   QMP6988 (0x70) initialisieren; der Init kehrt erst zurück, wenn eine
   vollständige Messung vorliegt
6. FT6336G initialisieren (Reset-Puls, Adresse 0x38)
7. Zeitzone setzen, **Netzwerk-Task starten** (WLAN + NTP, läuft im Hintergrund)
8. Backlight auf 100 %, LED grün
9. **Thermometer-Schleife**: Wallpaper lesen und prüfen, das Bild einmal in den
   Canvas dekodieren, Temperatur als `xx,x°` in die obere Kachel zeichnen,
   darunter in der unteren Kachel Feuchte, Luftdruck und Datum/Uhrzeit, Bild
   senden. Alle 10 s eine neue Messung; neu gezeichnet wird nur, wenn sich
   ein angezeigter Wert ändert (Zehntelgrad, Zehntel-Prozent Feuchte, ganze hPa,
   Minute).

Das alte Hardware-Testbild liegt getrennt in
[testscreen.c](main/testscreen.c)/[testscreen.h](main/testscreen.h) (Muster,
Statuszeile, Touch-Punkte, BOOT-Neudruck). Einschalten mit
`APP_SHOW_TESTSCREEN 1` in [main.c](main/main.c); zum Entfernen beide Dateien
und den einen Aufruf löschen.

### Bildschirmaufbau des Testbilds (Hochformat 240×320)

| Bereich | Inhalt |
|---|---|
| y 0–25 | Titelbalken: Boardname, ESP-IDF-Version |
| y 32–151 | Farbreihen: Weiß, Gelb, Cyan, Grün, Magenta, Rot, Blau, Schwarz |
| y 158–187 | Verläufe: Schwarz→Weiß, Schwarz→Rot, Schwarz→Blau |
| y 194–257 | Geometrie: Rahmen, Diagonalen, Kreis, Text „GEOMETRY 1PX" |
| y 264–319 | Statuszeile und laufender Balken |

## Wallpaper-Ablage

Die JPEGs liegen im Projektordner [data/](data). Genau dieser Ordner wird beim
Bauen in ein littlefs-Image gepackt und mit `flash` in die Partition `storage`
geschrieben. Kopiere deine Verzeichnisstruktur also so hinein:

```
ES3C28P_Thermometer\
└─ data\                        <- Inhalt = Wurzel des Dateisystems
   └─ backgrounds\
      ├─ spring\  ├─ morning\   *.jpg
      │           ├─ day\       …
      │           ├─ evening\   …
      │           └─ night\     …
      ├─ summer\  … wie spring
      ├─ autumn\  … wie spring
      └─ winter\  … wie spring
```

Damit ergeben sich auf dem Gerät diese Pfade (Präfix `STORAGE_BASE_PATH` aus
[storage.h](main/storage.h)):

```
/littlefs/backgrounds/spring/morning/wild-flowers-desert.jpg
/littlefs/backgrounds/summer/day/ocean-sunset.jpg
…
```

Also einfach den kompletten Ordner `backgrounds` nach `data\` kopieren:

```powershell
Copy-Item -Recurse "C:\...\backgrounds" "C:\Users\joe\Documents\ESP-IDF\Projects\ES3C28P_Thermometer\data\"
```

Dann bauen und flashen – das Image wird automatisch neu erzeugt und ab 0x410000
geschrieben:

```powershell
.\idf -p COM12 flash monitor
```

Erwartete Ausgabe (Ist-Stand mit 48 Dateien):

```
I (2891) storage: littlefs mounted at /littlefs: 4096 KiB total, 3108 KiB free
I (2931) storage: /littlefs/backgrounds/autumn/day: 3 files, 53200 bytes
I (2951) storage: /littlefs/backgrounds/autumn/evening: 3 files, 40490 bytes
…   (16 Ordnerzeilen, je 3 Dateien)
I (4181) storage: 48 files in total (48 JPEG), 728964 bytes
```

Hinweise:

* Der Image-Build läuft über `littlefs_create_partition_image(storage ../data
  FLASH_IN_PROJECT)` in [main/CMakeLists.txt](main/CMakeLists.txt); dabei wird
  einmalig ein kleines Python-Venv (`build/littlefs_py_venv`) mit
  `littlefs-python` angelegt.
* Das Image ist fest 4 MB groß (`--fs-size=0x400000`, Blockgröße 4096).
* **Platzbedarf (gemessen):** 729 KB Dateien; von den 4 MB sind danach
  **3108 KiB frei**. Ein viertes Bild pro Slot passt also problemlos.
* Dateinamen dürfen bis 127 Zeichen lang sein: `CONFIG_LITTLEFS_OBJ_NAME_LEN=128`
  in [sdkconfig.defaults](sdkconfig.defaults) (Vorgabe der Komponente ist 64).
  Der Image-Build übernimmt den Wert automatisch als `--name-max`.
* Jedes `flash` schreibt die komplette Partition neu, auch wenn sich nur die App
  geändert hat. Wer das nicht will, flasht die App gezielt mit
  `.\idf app-flash` (Tabelle und Image bleiben dann unangetastet).

### Welches Bild wann erscheint

Die Regeln stehen als `#define`-Block oben in [main/wallpaper.h](main/wallpaper.h)
und werden von `wallpaper_daytime_for()` / `wallpaper_season_from_month()`
angewendet:

| Wechsel | Auslöser |
|---|---|
| → `morning` | Sonnenaufgang (von sunrise-sunset.org, `clock_sunrise()`) |
| `morning` → `day` | `WALLPAPER_MORNING_END_HOUR` = **9:00**, im Winter `WALLPAPER_MORNING_END_HOUR_WINTER` = **10:00** |
| → `evening` | Sonnenuntergang (`clock_sunset()`) |
| → `night` | `WALLPAPER_NIGHT_START_HOUR` = **23:00** (und nach Mitternacht bis Sonnenaufgang) |

Die Jahreszeit folgt den meteorologischen Blöcken: Dezember–Februar Winter,
März–Mai Frühling, Juni–August Sommer, September–November Herbst.

Beim Wechsel des Slots (Jahreszeit oder Tageszeit) wird **eines der drei Bilder
zufällig** gewählt (`wallpaper_random_index()`); innerhalb eines Slots bleibt es
stehen. Geprüft wird im 10-s-Takt der Messschleife, ein Wechsel erfolgt also
innerhalb von 10 s nach dem auslösenden Zeitpunkt.

Sind die Sonnenzeiten noch nicht bekannt (kein WLAN, erste Sekunden nach dem
Start), gelten die Ersatzwerte `WALLPAPER_SUNRISE_FALLBACK_*` (07:00) und
`WALLPAPER_SUNSET_FALLBACK_*` (19:00). Solange die NTP-Uhr fehlt, rechnet die
Auswahl mit dem Rückfall-Datum (`01.01. 00:00` → Winter/Nacht); sobald die Zeit
da ist, springt der Hintergrund auf den richtigen Slot (im Log als
`cyd: background …` sichtbar).

## RGB-LED als Ambilight – [main/statusled.h](main/statusled.h)

Das Gerät hängt im Acrylrahmen etwa 15 mm vor einer weißen Wand; die RGB-LED
leuchtet diese Wand von hinten aus. Auf dem ES3C28P ist es **ein einzelner
adressierbarer WS2812 an GPIO42** (GRB, angesteuert über RMT mit der Komponente
`espressif/led_strip`), nicht mehr drei PWM-Kanäle; Helligkeit und Farbverläufe
entstehen durch Skalieren der Pixelfarbe.

| Modus | Verhalten |
|---|---|
| Nacht (`DAYTIME_NIGHT`) | **Regenbogen-Fade** als Ambilight: rot → gelb → grün → cyan → blau → magenta → rot, ein Umlauf pro `STATUSLED_AMBIENT_PERIOD_MS` (60 s), Aktualisierung alle 50 ms (1200 Stufen pro Umlauf) |
| alle anderen Tageszeiten | weiß mit `STATUSLED_IDLE_BRIGHTNESS` (30 % der Kanalwerte) |

```c
#define STATUSLED_AMBIENT_PERIOD_MS   60000   /* ein Farbumlauf im Nachtmodus */
#define STATUSLED_AMBIENT_BRIGHTNESS  100     /* Prozent */
#define STATUSLED_IDLE_BRIGHTNESS      30     /* Prozent, weiß */
```

Der Modus folgt der Tageszeit aus `wallpaper_update()` in [main/main.c](main/main.c);
im Log steht pro Wechsel `status LED: rainbow fade (night)` bzw.
`status LED: white, dimmed`. `statusled_set()` (Selbsttest, BOOT-Taste im
Testbild) schaltet die Automatik ab, bis wieder `statusled_ambilight()` aufgerufen
wird.

## Voraussetzungen

* ESP-IDF **v6.1** – auf diesem Rechner installiert unter `C:\esp\v6.1\esp-idf`
  (Espressif Installation Manager, Tools in `C:\Espressif\tools`)
* VS Code mit der ESP-IDF-Erweiterung (`espressif.esp-idf-extension`), Setup
  `v6.1` ist in `.vscode/settings.json` schon ausgewählt
* USB-Datenkabel (Type-C) – das Board erscheint als **USB-Serial/JTAG** (nativer
  USB des ESP32-S3). Auf diesem Rechner ist es **COM12**; bei einem zweiten
  Board ist der Port ein anderer (siehe Abschnitt
  [Fabrikneues Board](#fabrikneues-board-partitionieren-daten-und-app-flashen))
* 2,4-GHz-WLAN in Reichweite; SSID und Passwort in [main/connect.h](main/connect.h)
  (aus [connect.example.h](main/connect.example.h) kopieren)

## Bauen, Flashen, Monitor

### In VS Code

1. Ordner `ES3C28P_Thermometer` öffnen (falls nicht schon offen)
2. Unten in der Statusleiste: Ziel `esp32s3`, seriellen Port auswählen (hier COM12)
3. Bausteinsymbol = Build, Blitzsymbol = Flash, Stecker/Seriell-Symbol = Monitor
   (oder „Build, Flash and Monitor" über die Kommandopalette)

### IntelliSense in VS Code

Die Include-Pfade kommen aus `.vscode/c_cpp_properties.json`. Wichtig sind zwei
Felder:

* `compilerPath` – der Xtensa-Compiler, aus dessen eingebautem Suchpfad die
  Systemheader (`stdio.h`, `string.h`, …) kommen. Ohne diesen Eintrag meldet VS
  Code für jede Systemheader-Zeile „cannot open source file ...".
* `compileCommands` – `${workspaceFolder}/build/compile_commands.json`. Daraus
  zieht IntelliSense die rund 95 `-I`-Pfade und alle `-D`-Schalter des echten
  ESP-IDF-Builds, also auch `sdkconfig.h` mit den `CONFIG_*`-Werten. Die Datei
  entsteht beim Build automatisch; **vor dem ersten Build gibt es sie noch
  nicht**.

Wenn die Meldungen nach einem Toolchain-Update wieder auftauchen, zeigt
`compilerPath` auf einen nicht mehr existierenden Pfad – dann in
`.vscode/c_cpp_properties.json` den aktuellen Pfad eintragen (Ordner
`C:\Espressif\tools\xtensa-esp-elf\`) und in VS Code
„C/C++: Reset IntelliSense Database" ausführen.

**Header, die in keinem `.c` eingebunden sind** (`background.h` ist so ein Fall),
stehen nicht in der Compile-Datenbank. IntelliSense prüft sie dann nur mit dem
Projekt-Suchpfad `${workspaceFolder}/**`, nicht mit den ESP-IDF-Pfaden. Sobald
so ein Header einen anderen Header aus dem Projekt einbindet, der seinerseits
ESP-IDF-Header braucht (früher `gfx.h` → `esp_lcd_panel_ops.h`), meldet VS Code
einen Include-Fehler, obwohl der Compiler die Datei akzeptiert. Lösung: die
Abhängigkeit klein halten – reine Daten-/Farbtabellen binden nur
[gfx_color.h](main/gfx_color.h) ein, das außer `<stdint.h>` nichts braucht.

Alternative: die ESP-IDF-Erweiterung hat in `.vscode/settings.json` schon
Einstellungen für `clangd` hinterlegt (inklusive `--compile-commands-dir`). Wer
statt des C/C++-Parsers lieber clangd nutzt, installiert die Extension
`llvm-vs-code-extensions.vscode-clangd` und setzt
`"C_Cpp.intelliSenseEngine": "disabled"`, damit sich beide nicht in die Quere
kommen.

### Auf der Kommandozeile

`idf.py` liegt bei einer EIM-Installation **nicht** im PATH; es funktioniert nur
in einer Shell, in der das Aktivierungsskript geladen wurde. Dafür gibt es drei
Wege:

**1. Wrapper im Projekt (empfohlen, funktioniert in jedem Fenster)**

```powershell
cd C:\Users\joe\Documents\ESP-IDF\Projects\ES3C28P_Thermometer
.\idf build
.\idf -p COM12 flash monitor     # COM12 = Port dieses Geräts
# Monitor beenden: Strg + ]
```

[idf.cmd](idf.cmd) setzt `IDF_PATH`, `IDF_TOOLS_PATH`, `IDF_PYTHON_ENV_PATH`
und den Toolchain-Pfad für diesen Aufruf und reicht alle Argumente an `idf.py`
weiter. Eine `.cmd`-Datei ist von der PowerShell-ExecutionPolicy nicht
betroffen, deshalb funktioniert das auch bei `Restricted` (Standard auf diesem
Rechner). Nach einem Wechsel der ESP-IDF-Version über den EIM müssen die Pfade
oben in der Datei angepasst werden.

**2. ESP-IDF-Umgebung in der aktuellen Sitzung aktivieren**

```powershell
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned   # einmalig
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
idf.py build
```

Ohne die Policy-Änderung bricht das Laden des Profils mit
„Die Ausführung von Skripts ist auf diesem System deaktiviert" ab. Danach ist
`idf.py` als Alias verfügbar – allerdings nur in diesem Fenster.

**3. Nur für einen Aufruf, ohne Policy-Änderung**

```powershell
powershell -ExecutionPolicy Bypass -NoExit -Command ". 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'"
```

Öffnet ein neues Fenster mit vollständiger ESP-IDF-Umgebung.

Der erste Build lädt die verwalteten Komponenten automatisch aus der ESP
Component Registry nach `managed_components/`:

* `espressif/esp_lcd_ili9341` 2.1.0 (ILI9341-Treiber für `esp_lcd`)
* `espressif/led_strip` 3.x (WS2812-Status-LED über RMT)
* `joltwallet/littlefs` 1.23.0 (Dateisystem der Wallpaper-Partition)
* `espressif/mdns` 1.14.x (`<hostname>.local`, nicht mehr Teil von IDF 6)
* `espressif/cmake_utilities` (wird von den Komponenten mitgebracht)

Zum Testen ohne Hardware genügt `.\idf build` – es ist bereits erfolgreich
durchgelaufen (App-Binärdatei 1,37 MB inklusive 147 KB Schrift, 4-MB-App-
Partition, 16 MB Flash, 8 MB OPI-PSRAM, CPU 240 MHz).

### Fabrikneues Board: partitionieren, Daten und App flashen

Diese Reihenfolge gilt für ein **weiteres, werksneues ES3C28P** (oder nach einem
kompletten Neuaufbau). Sie löscht alles, was ab Werk auf dem Flash liegt, legt
die Partitionstabelle aus [partitions.csv](partitions.csv) an und schreibt
Bootloader, App und das 4-MB-littlefs-Image mit den 48 Wallpapern.

Alle Befehle im Projektordner ausführen; `COM12` ist der Port **dieses**
Geräts und muss bei einem anderen Board angepasst werden.

Kurzfassung (die Einzelschritte darunter erklären dasselbe):

```powershell
cd C:\Users\joe\Documents\ESP-IDF\Projects\ES3C28P_Thermometer
.\idf build
.\idf -p COM12 erase-flash
.\idf -p COM12 flash
.\idf -p COM12 monitor
```

**1. Port ermitteln** (Board per USB-C anschließen):

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

Alternativ im Geräte-Manager unter „Anschlüsse (COM & LPT)" nachsehen; das Board
meldet sich als **USB-Serial/JTAG** (nativer USB des ESP32-S3, kein CH340).
Erscheint kein Port: BOOT-Taste halten, RESET/EN kurz drücken, RESET loslassen,
dann BOOT loslassen (Download-Modus).

**2. Einmal bauen** (erzeugt `build\storage.bin` und die App-Binärdatei):

```powershell
.\idf build
```

Erwartete letzte Zeile: `Project build complete.`
Kontrolle: `build\storage.bin` ist 4 MB groß, `build\ES3C28P_Thermometer.bin`
etwa 1,4 MB.

**3. Flash komplett löschen** (empfohlen bei fabrikneuer Ware; entfernt die
Werksfirmware und alte NVS-/Partitionstabellen-Reste):

```powershell
.\idf -p COM12 erase-flash
```

**4. Partitionstabelle + Daten + App in einem Schritt schreiben:**

```powershell
.\idf -p COM12 flash
```

Dabei werden genau diese Images an die folgenden Offsets geschrieben
(Reihenfolge und Werte stehen auch in `build\flash_args`):

| Offset | Image | Inhalt |
|---|---|---|
| `0x000000` | `build\bootloader\bootloader.bin` | 2nd-Stage-Bootloader |
| `0x008000` | `build\partition_table\partition-table.bin` | Partitionstabelle |
| `0x010000` | `build\ES3C28P_Thermometer.bin` | App (4-MB-Partition `factory`) |
| `0x410000` | `build\storage.bin` | littlefs mit allen Wallpapern (4 MB) |

Das Schreiben des 4-MB-Images dauert je nach Port einige Sekunden bis eine
Minute. Am Ende muss `Hash of data verified.` und `Done` stehen.

**5. Start prüfen:**

```powershell
.\idf -p COM12 monitor
```

Im Log müssen mindestens diese Zeilen erscheinen (Zeitstempel können abweichen):

```
I (26) boot: 2 factory          factory app      00 00 00010000 00400000
I (27) boot: 3 storage          Unknown data     01 82 00410000 00400000
I (2851) cyd: ILI9341 ready, 240x320 portrait, 40 MHz SPI clock, BGR element order
I (2851) gfx: canvas 240x320: 153600 bytes
I (2891) storage: littlefs mounted at /littlefs: 4096 KiB total, 3108 KiB free
I (4181) storage: 48 files in total (48 JPEG), 728964 bytes
I (4191) sensor: I2C device found at 0x44
I (4201) sensor: I2C device found at 0x70
I (4221) sensor: SHT30 ready at 0x44, first reading 23.3 C, 49.1 %rH
I (4241) sensor: QMP6988 ready at 0x70 (chip id 0x5C)
I (4601) touch: FT6336G ready at 0x38, firmware 0x02, chip id 0x64
I (4811) cyd: frame: 23,3 degC, …, total 150 ms
```

Fehlt `storage: 48 files`, ist das littlefs-Image nicht geschrieben worden –
Schritt 4 wiederholen. Fehlen die Sensorzeilen, Bus-Scan im Log ansehen
(erwartet: 0x18, 0x38, 0x44, 0x70).

**6. Erster Start / Netzwerk:** Nach `erase-flash` ist das NVS leer. Die Unit
startet mit den Werten aus [main/connect.h](main/connect.h). Passt das WLAN
nicht, nach dem Boot die **obere rechte Ecke** des Displays 0,5 s halten und
über den Hotspot `thermometer` / `http://192.168.4.1` einrichten.

#### Spätere Änderungen ohne erneutes Vollflashen

| Ziel | Befehl |
|---|---|
| Nur die App (nach Code-Änderung) | `.\idf -p COM12 app-flash` |
| Nur die Wallpaper (nach Änderung in `data\`) | `.\idf -p COM12 storage-flash` |
| Nur die Partitionstabelle | `.\idf -p COM12 partition-table-flash` |
| Nur der Bootloader | `.\idf -p COM12 bootloader-flash` |
| Alles (App + Daten + Tabelle + Bootloader) | `.\idf -p COM12 flash` |
| Komplett löschen | `.\idf -p COM12 erase-flash` |

#### Fallback: direkt mit esptool schreiben

Falls `idf.py` den Flash-Vorgang nicht startet (z. B. weil der Wrapper fehlt),
schreibt derselbe Befehl die vier Images direkt. Der Pfad zum Python ist der aus
[idf.cmd](idf.cmd):

```powershell
C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe -m esptool `
  --chip esp32s3 -p COM12 -b 460800 --before default-reset --after hard-reset `
  write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB `
  0x0 build\bootloader\bootloader.bin `
  0x8000 build\partition_table\partition-table.bin `
  0x10000 build\ES3C28P_Thermometer.bin `
  0x410000 build\storage.bin
```

Die Backticks sind die Zeilenfortsetzung von PowerShell; alternativ alles in
eine Zeile schreiben. Die Optionen `--flash-mode dio --flash-freq 80m
--flash-size 16MB` entsprechen dem Build; sie müssen zum Modul passen
(N16R8: 16 MB Flash, 8 MB OPI-PSRAM).

## Watchdog - [main/watchdog.c](main/watchdog.c)

Ein hängender Task startet die Unit neu, statt sie tot stehen zu lassen. Der
Watchdog läuft mit **60 s**, ein Timeout erzeugt einen Panic (Backtrace im Log)
und einen Neustart; der nächste Start protokolliert dann den Grund.

| Punkt | Wert |
|---|---|
| Timeout | 60 s (`WATCHDOG_TIMEOUT_MS`) |
| Überwacht | nur Tasks, die sich mit `watchdog_subscribe()` anmelden |
| Idle-Tasks | **nicht** überwacht (`idle_core_mask = 0`), damit lange Flash-Operationen nicht auslösen |
| Angemeldet | Anzeige-Task, Scan-Task und die Schleife des Konfigurationsmodus |

```c
watchdog_init();        /* app_main, direkt nach netcfg_init() */
watchdog_subscribe();   /* im Task, der überwacht werden soll */
watchdog_feed();        /* mindestens alle 60 s */
```

Die Anzeige-Task ruft `watchdog_feed()` in ihrem 100-ms-Takt, der Hintergrund-Scan
in 500-ms-Schritten. Damit fällt auf, wenn Zeichnen, JPEG-Dekodierung oder der
Webserver hängen – früher blieb das Bild einfach stehen.

## Projektstruktur

| Datei | Zweck |
|---|---|
| [main/main.c](main/main.c) | Ablauf: Backlight, LCD-Init, littlefs, Sensor, Netzwerk-Task, Thermometer-Schleife mit Slot-Wechsel (`APP_SHOW_TESTSCREEN` schaltet das Testbild ein) |
| [main/board_es3c28p.h](main/board_es3c28p.h) | Alle Pins, Auflösung, Ausrichtung, SPI-Takt, Farb-/Inversionsflags und Touch-Orientierung |
| [main/connect.h](main/connect.h) | **Konfiguration der Installation**: WLAN, NTP, Zeitzone, Koordinaten (nicht im Repository, Vorlage: [connect.example.h](main/connect.example.h)) |
| [main/wifi.c](main/wifi.c), [main/wifi.h](main/wifi.h) | WLAN-Station: NVS, `esp_netif`, Event-Handler, Verbindungsaufbau mit Wiederholung |
| [main/clock.c](main/clock.c), [main/clock.h](main/clock.h) | Zeitzone, SNTP, Ortszeit, Ablage der Sonnenzeiten |
| [main/sun.c](main/sun.c), [main/sun.h](main/sun.h) | Sonnenauf-/-untergang von sunrise-sunset.org holen und umrechnen (HTTPS) |
| [main/wallpaper.c](main/wallpaper.c), [main/wallpaper.h](main/wallpaper.h) | JPEG lesen, bandweise mit TJpgDec in voller Auflösung malen, Slot-Auswahl (Jahreszeit/Tageszeit + Zufallsbild) |
| [main/tjpgd](main/tjpgd) | JPEG-Decoder TJpgDec R0.03 mit eigener Konfiguration (`tjpgdcnf.h`) |
| [main/bigdigit.c](main/bigdigit.c), [main/bigdigit.h](main/bigdigit.h) | Große Temperaturanzeige `xx,x°` mit dem TrueType-Renderer |
| [main/infotile.c](main/infotile.c), [main/infotile.h](main/infotile.h) | Untere Kachel: Feuchte, Luftdruck, Datum/Zeit als drei zentrierte Zeilen |
| [main/layout.h](main/layout.h) | Alle Layout-Werte der Anzeige (beide Kacheln, Ziffernhöhen, Komma-/Gradgröße, Bandhöhe) |
| [main/ttf.c](main/ttf.c), [main/ttf.h](main/ttf.h) | TrueType-Text: `stb_truetype`, Anti-Aliasing, Stauchung |
| [main/fonts/OpenSans-Regular.ttf](main/fonts) | Schrift (Apache 2.0), per `EMBED_FILES` im App-Image |
| [main/stb_truetype.h](main/stb_truetype.h) | TrueType-Rasterizer (public domain), mit einer lokalen Anpassung: kleinere Heap-Chunks (siehe oben) |
| [main/testscreen.c](main/testscreen.c), [main/testscreen.h](main/testscreen.h) | Hardware-Testbild, Statuszeile, Touch-Punkte, BOOT-Neudruck (separat entfernbar) |
| [main/statusled.c](main/statusled.c), [main/statusled.h](main/statusled.h) | RGB-LED über LEDC-PWM: Selbsttest, Regenbogen-Ambilight bei Nacht, sonst gedimmtes Weiß |
| [main/configmode.c](main/configmode.c), [main/configmode.h](main/configmode.h) | WLAN-Konfiguration: Access Point, HTTP-Server mit Einrichtungsseite, TFT-Hinweis, Zurück-Schaltfläche |
| [main/netcfg.c](main/netcfg.c), [main/netcfg.h](main/netcfg.h) | WLAN-Daten und Koordinaten im NVS: laden, speichern, `netcfg_sun_url()` |
| [main/webserver.c](main/webserver.c), [main/webserver.h](main/webserver.h) | Dauerhafter HTTP-Server im Heimnetz: `GET /api/values` (JSON), `GET /` (Browserseite) |
| [main/discovery.c](main/discovery.c), [main/discovery.h](main/discovery.h) | mDNS: `<hostname>.local` und `_http._tcp`, in jedem Netz ohne Router-DNS |
| [main/watchdog.c](main/watchdog.c), [main/watchdog.h](main/watchdog.h) | Task-Watchdog: 60 s, Panic und Neustart, nur für angemeldete Tasks |
| [main/textutil.c](main/textutil.c), [main/textutil.h](main/textutil.h) | Maskierung für HTML und JSON (Konfigurationsseite, Messwert-Endpunkt) |
| [main/outdoor.c](main/outdoor.c), [main/outdoor.h](main/outdoor.h) | Außentemperatur vom Partnergerät holen (HTTP, alle 60 s) und unter der Raumtemperatur anzeigen |
| [main/gfx.c](main/gfx.c), [main/gfx.h](main/gfx.h) | Mini-Grafikbibliothek: Canvas (auch als Band), Rechtecke, Linien, Kreis, Text, Flush |
| [main/gfx_color.h](main/gfx_color.h) | RGB565-Farbmakros, ohne ESP-IDF-Abhängigkeit (auch für Datentabellen nutzbar) |
| [main/font5x7.c](main/font5x7.c), [main/font5x7.h](main/font5x7.h) | 5×7-Pixel-Font (A–Z, 0–9, Satzzeichen), generiert |
| [main/touch.c](main/touch.c), [main/touch.h](main/touch.h) | FT6336G: Reset-Puls, I²C-Register 0x02/0x03, Pixel direkt als Bildschirmkoordinate |
| [main/board_i2c.c](main/board_i2c.c), [main/board_i2c.h](main/board_i2c.h) | Gemeinsamer I²C-Bus für Touch und ENV III (SDA = IO16, SCL = IO15) |
| [main/storage.c](main/storage.c), [main/storage.h](main/storage.h) | littlefs: Mount der `storage`-Partition, Belegung, Dateizählung |
| [main/sensor.c](main/sensor.c), [main/sensor.h](main/sensor.h) | UNIT ENV III: SHT30 (Temperatur/Feuchte), QMP6988 (Druck), Bus-Scan und Adress-Fallbacks |
| [main/background.h](main/background.h) | Tabelle der 48 Wallpaper-Dateinamen und Textfarben je Jahreszeit/Tageszeit |
| [data/](data) | Inhalt der littlefs-Partition (Wallpaper), wird beim Bauen zum Image |
| [main/idf_component.yml](main/idf_component.yml) | Abhängigkeiten aus der Component Registry (ILI9341, led_strip, littlefs, mDNS) |
| [tools/gen_font5x7.py](tools/gen_font5x7.py) | Erzeugt den Font neu (Glyphen als ASCII-Art, mit Probedruck) |
| [sdkconfig.defaults](sdkconfig.defaults) | 16 MB Flash, 8 MB OPI-PSRAM, 240 MHz CPU, Ziel esp32s3, eigene Partitionstabelle, littlefs-Namenslänge 128, USB-Serial/JTAG-Konsole, WiFi-Puffer (RAM), SoftAP für den Konfigurationsmodus, TLS-Optionen für HTTPS |
| [partitions.csv](partitions.csv) | Partitionstabelle: nvs, phy_init, 4 MB App, 4 MB littlefs |
| [idf.cmd](idf.cmd) | Wrapper: `.\idf build` / `.\idf -p COM12 flash monitor` ohne ESP-IDF-Profil |

## Erwartete Ausgabe auf dem seriellen Monitor

```
I (711) cyd: --- ES3C28P room thermometer, ESP-IDF v6.1 ---
I (711) cyd: heap: 8693723 bytes free, largest free block 8257536 bytes
I (731) statusled: RGB LED: red
…   (grün, blau, weiß, aus – je 400 ms)
I (2731) cyd: SPI1 for ILI9341: SCLK=12 MOSI=11 MISO=13 CS=10 DC=46 RST=EN
I (2731) ili9341: LCD panel create success, version: 2.1.0
I (2851) cyd: ILI9341 ready, 240x320 portrait, 40 MHz SPI clock, BGR element order
I (2851) gfx: canvas 240x320: 153600 bytes
I (2891) storage: littlefs mounted at /littlefs: 4096 KiB total, 3108 KiB free
…   (16 Ordnerzeilen, je 3 Dateien)
I (4181) storage: 48 files in total (48 JPEG), 728964 bytes
I (4181) board_i2c: I2C0 ready: SDA=16 SCL=15 (shared by touch and ENV III)
I (4181) sensor: I2C device found at 0x18
I (4191) sensor: I2C device found at 0x38
I (4191) sensor: I2C device found at 0x44
I (4201) sensor: I2C device found at 0x70
I (4221) sensor: SHT30 ready at 0x44, first reading 23.3 C, 49.1 %rH
I (4241) sensor: QMP6988 ready at 0x70 (chip id 0x5C)
I (4291) sensor: sensor ready: 23.3 C, 49.1 %rH, 974 hPa
I (4601) touch: FT6336G ready at 0x38, firmware 0x02, chip id 0x64, 240x320, swap=0 mirror=0/0
I (4511) ttf: 147528 byte font loaded, ascent 2189 / descent -600 / gap 0 (font units)
I (4511) clock: time zone Europe/Berlin (CET-1CEST,M3.5.0,M10.5.0/3)
I (4511) cyd: 8502771 free heap bytes after initialisation
I (4511) cyd: background 2/1/1 at 31.10. 11:03: /littlefs/backgrounds/autumn/day/yellow-air-balloon.jpg, text colour 0x0000
I (4581) wallpaper: …/yellow-air-balloon.jpg: 240x320, 12077 bytes JPEG, YCbCr
I (4651) bigdigit: "00,0" cap 100 px, scale x/y 0.060/0.068, from x=25 on baseline y=130, 187 px wide
I (4651) bigdigit: degree: 21x21 px ink, top right corner at (215,40)
I (4661) infotile:   line 1 "0,0 %rF": cap 24 px, baseline y=236, 111 px wide
I (4661) cyd: band 0..319: 5411 text pixels
I (4701) cyd: frame: 0,0 degC, text from (25,130), 187 px wide, degree at (215,40), decode 68 ms, total 185 ms
I (8161) wifi: connected to "FRITZ!Box 7590 j.oe", IP 192.168.123.123
I (8171) clock: NTP server de.pool.ntp.org, querying ...
I (13071) clock: NTP answer received, 31.10.2026 11:03:22 (Europe/Berlin)
I (14951) sun: sunrise 06:59, sunset 16:58 (Europe/Berlin)
I (15711) cyd: 31.10.2026 11:05:13 ENV III: 23.32 C, 49.1 %rH, 974.1 hPa, IP 192.168.123.123
I (15851) cyd: frame: 23,3 degC, text from (25,130), 187 px wide, degree at (215,40), decode 15 ms, total 141 ms
```

Der erste Frame steht nach etwa 5 s (ohne auf das Netzwerk zu warten), die
Uhrzeit erscheint mit dem nächsten 10-s-Refresh nach dem NTP-Antwort, die
Sonnenzeiten nach der HTTPS-Abfrage. Beispiel für einen Start ohne WLAN:
`W (21520) clock: no NTP answer …`, danach läuft die Anzeige mit `01.01. 00:00`
weiter.

## Fehlersuche

| Symptom | Ursache / Abhilfe |
|---|---|
| Kein Log auf dem Monitor | Falscher COM-Port, USB-Kabel ohne Datenleitungen, BOOT gedrückt halten und EN/RESET tippen |
| „cannot open source file stdio.h" (IntelliSense) | `.vscode/c_cpp_properties.json` prüfen: `compilerPath` muss auf die vorhandene `xtensa-esp32-elf-gcc.exe` zeigen und `compileCommands` auf die gebaute `build/compile_commands.json` (erst nach einem Build vorhanden). Danach „C/C++: Reset IntelliSense Database" |
| Include-Fehler in einem Header, obwohl der Build läuft | Der Header gehört zu keinem `.c` und wird von IntelliSense nur mit dem Projekt-Suchpfad geprüft. Keine ESP-IDF-Header darin einbinden – reine Tabellen nutzen [gfx_color.h](main/gfx_color.h) statt [gfx.h](main/gfx.h), Details im Abschnitt [IntelliSense in VS Code](#intellisense-in-vs-code) |
| `idf.py` wird nicht erkannt (`CommandNotFoundException`) | `idf.py` liegt bei EIM-Installationen nicht im PATH. Entweder `.\idf build` (Wrapper im Projekt) oder die ESP-IDF-Umgebung laden, siehe [Auf der Kommandozeile](#auf-der-kommandozeile) |
| „Die Ausführung von Skripts ist auf diesem System deaktiviert" | PowerShell-ExecutionPolicy ist `Restricted` (Windows-Standard). Entweder den Wrapper `.\idf` nutzen oder einmalig `Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned` ausführen |
| `partition 'storage' not found` / littlefs meldet `ESP_ERR_NOT_FOUND` | Partitionstabelle im Flash ist noch die alte: `.\idf -p COM12 flash` (schreibt sie an 0x8000 mit). Prüfen mit `.\idf partition-table` |
| `storage: 0 files in total` obwohl Wallpaper vorhanden | Dateien liegen nicht unter `data/` (z. B. eine Ebene zu tief, etwa `data/backgrounds/backgrounds/…`). Prüfen mit `python -m littlefs list build\storage.bin --block-size 4096` im Ordner `build\littlefs_py_venv\Scripts\` |
| littlefs-Image zu groß für die Partition | Weniger/kleinere JPEGs, oder `storage` in [partitions.csv](partitions.csv) vergrößern (App-Partition entsprechend verkleinern) und Tabelle neu flashen |
| Dateiname wird nicht angelegt / `LFS_ERR_NAMETOOLONG` | Name länger als `CONFIG_LITTLEFS_OBJ_NAME_LEN - 1` (aktuell 127 Zeichen), oder das Image wurde vor der Änderung gebaut: `.\idf fullclean` und neu bauen |
| Sensor meldet `no SHT30 found at 0x44/0x45` | SDA/SCL vertauscht oder nicht kontaktiert (SDA = IO16, SCL = IO15), 3V3/GND fehlen, oder das ENV-III-Kabel sitzt nicht. Der Bus-Scan im Log zeigt, welche Adressen antworten (erwartet: 0x18 Audio-Codec, 0x38 Touch, 0x44 SHT30, 0x70 QMP6988) |
| QMP6988 meldet `chip id 0x00`/`0xFF` | Der Druckteil antwortet nicht: Kabel/Stecker prüfen; Adresse 0x56 (SDO high) wird automatisch probiert. Die Temperatur/Feuchte läuft trotzdem weiter |
| SHT30 liefert unsinnige Werte | Sensor kurz vorher angefasst – Selbstaufheizung; 1–2 Minuten warten. Druck wird in hPa ausgegeben (965 hPa entspricht ca. 400 m Höhe) |
| `Could not open COM12, the port is busy` beim Flashen | Ein noch laufender Monitor hält den Port: im Monitor `Strg+]`, sonst das Fenster schließen, dann erneut flashen |
| `undefined reference to 'app_main'` | `main.c` fehlt in der `SRCS`-Liste von [main/CMakeLists.txt](main/CMakeLists.txt) – passiert, wenn die ESP-IDF-Erweiterung die Liste synchronisiert, während die Datei kurzzeitig gelöscht war. Datei wieder eintragen |
| Wallpaper erscheint, aber die Farben stimmen nicht (grün/lila) | Byte-Reihenfolge des JPEG-Ergebnisses: `flags.swap_color_bytes` in [wallpaper.c](main/wallpaper.c) umschalten |
| Bild steht auf dem Kopf | `BOARD_LCD_MIRROR_X` **und** `BOARD_LCD_MIRROR_Y` in [board_es3c28p.h](main/board_es3c28p.h) umschalten (180°-Drehung) – danach auch `BOARD_TOUCH_MIRROR_X/Y` umschalten, sonst passt der Touch nicht mehr |
| Schrift wirkt zu schmal/gestaucht | `LAYOUT_TEMP_MIN_SQUEEZE` in [layout.h](main/layout.h) erhöhen (1.0 = nie stauchen, Schrift wird stattdessen kleiner) |
| Schrift/° zu groß oder zu klein | `LAYOUT_TEMP_DIGIT_HEIGHT` bzw. `LAYOUT_TEMP_DEGREE_SCALE` in [layout.h](main/layout.h) – Kontrolle über die `bigdigit:`-Zeilen im Log |
| Text sitzt an der falschen Stelle | `LAYOUT_TEMP_X` / `LAYOUT_TEMP_BASELINE_Y` bzw. `LAYOUT_TEMP_DEGREE_X/Y` in [layout.h](main/layout.h) |
| Untere Zeilen zu groß/klein oder falsch verteilt | `LAYOUT_INFO_*_CAP`, `LAYOUT_INFO_GAP` und `LAYOUT_INFO_MIN_SQUEEZE` in [layout.h](main/layout.h) – Kontrolle über die `infotile:`-Zeilen im Log (Blockhöhe und Grundlinien je Zeile) |
| Infozeilen fehlen komplett | `infotile.c` muss in der `SRCS`-Liste von [main/CMakeLists.txt](main/CMakeLists.txt) stehen, sonst fehlt der Aufruf im Image |
| `band … has no text pixels` im Log | Die Textausgabe ist leer geblieben: dafür fehlte `stb_truetype` ein zusammenhängender Puffer. Prüfen, ob `LAYOUT_BAND_HEIGHT`/`LAYOUT_TEMP_DIGIT_HEIGHT` kleiner gestellt wurden bzw. ob der Heap zu knapp ist (Heap-Zeile im Log). Normalerweise darf diese Meldung nicht auftreten |
| Kein Text, Log zeigt `font could not be loaded` | `fonts/OpenSans-Regular.ttf` fehlt oder ist beschädigt; Datei prüfen und neu bauen (`EMBED_FILES` in [main/CMakeLists.txt](main/CMakeLists.txt)) |
| Texte verschwinden nach einigen Sekunden | War ein Fehler in `stb_truetype` (56-KB-Block nach Heap-Fragmentierung durch WiFi/TLS) – behoben durch die kleinere Chunk-Größe in [stb_truetype.h](main/stb_truetype.h). Tritt es wieder auf, steht jetzt `band … has no text pixels` im Log |
| `ttf: no memory for … bytes (free …, largest …)` | Der Renderer konnte einen Zwischenpuffer nicht anlegen (siehe oben) – die Anzeige bleibt in diesem Frame ohne Text |
| Touch trifft um 90° versetzt | Touch-Flags passend zur Anzeige: `BOARD_TOUCH_SWAP_XY` / `BOARD_TOUCH_MIRROR_X/Y` – siehe [Touch-Kalibrierung](#touch-kalibrierung) |
| Bildschirm bleibt dunkel, Log endet mit `cyd: LCD bring-up failed` | Fehlerzeile darüber lesen (SPI/Registrierung). Backlight liegt auf GPIO45 (`BOARD_LCD_PIN_BACKLIGHT`) – High = ein |
| Bild ist da, aber wie ein Negativ | `BOARD_LCD_INVERT_COLORS` muss beim IPS-Panel `1` sein |
| Rot und Blau vertauscht | `BOARD_LCD_BGR` umschalten (aktuell `1`) |
| Bild seitenverkehrt | `BOARD_LCD_MIRROR_X` umschalten (bzw. `BOARD_LCD_MIRROR_Y`) |
| Bild um 90° verdreht | `BOARD_LCD_LANDSCAPE` prüfen (muss `0` sein) und die Touch-Flags entsprechend anpassen |
| Streifen, Flackern, Farbrauschen | `BOARD_LCD_PIXEL_CLOCK_HZ` auf `20 * 1000 * 1000` reduzieren |
| `not enough memory for the 240x320 band canvas` | Der volle Frame braucht 150 KB im internen RAM. Prüfen, ob der Canvas wirklich vor dem WiFi-Task angelegt wird; sonst `LAYOUT_BAND_HEIGHT` in [layout.h](main/layout.h) auf `160` setzen (77 KB, zwei Decoder-Durchläufe) |
| `cannot reach "…" (reason 201 …)` im Log | Access Point nicht gefunden: SSID prüfen (Groß-/Kleinschreibung, 2,4 GHz – der ESP32 kann kein 5-GHz-Band), Gerät weiter vom Router entfernt. Alternativ im Eck-Touch den Konfigurationsmodus öffnen |
| `reason 15` beim Verbinden | Passwort falsch. Am einfachsten neu eingeben: Eck-Touch (x 180…239, y 0…59, 500 ms halten) oder `APP_FORCE_CONFIG 1` in [main.c](main/main.c) |
| Gerät hängt in der Einrichtungsschleife | Es kommt nach dem Speichern immer wieder zum Setup-Bildschirm: SSID/Passwort im Formular falsch, oder ein Testbuild mit `APP_FORCE_CONFIG 1` ist geflasht (dann wird das Netz nie versucht) – Logzeile `WLAN "…" from NVS` und `found with … dBm` prüfen |
| Gleiche SSID mehrfach in der Liste | Sollte nicht mehr auftreten (Mesh und 2,4/5 GHz werden zusammengefasst, der stärkere Eintrag bleibt). Sonst im Log `wifi: scan: … networks, … shown` ansehen |
| Hotspot `thermometer` erscheint nicht | Der Konfigurationsmodus läuft nicht (kein Netzproblem, `skip` gesetzt). Eck-Touch halten oder `APP_FORCE_CONFIG 1` setzen. `CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y` muss in der `sdkconfig` stehen – nach Änderungen an [sdkconfig.defaults](sdkconfig.defaults) die `sdkconfig` löschen und neu bauen |
| Seite `192.168.4.1` lädt nicht | Mit dem Hotspot verbinden (nicht mit dem Heimnetz), Passwort `CONNECT_AP_PASSWORD`. Am TFT steht die tatsächliche Adresse; `configmode: web server at http://…` im Log prüfen |
| Speichern quittiert mit 400 | Meldung auf der Seite lesen: Passwort 1–7 Zeichen (WPA2 braucht mindestens 8), SSID leer/zu lang, oder Koordinaten außerhalb −90…90 bzw. −180…180 |
| Nach „Speichern“ passiert nichts | Das Gerät startet nach 1,5 s neu (`configmode: restarting with the new settings`); die Seite verliert dabei die Verbindung. Am TFT erscheint wieder die Anzeige |
| `http://?/api/values` antwortet nicht | Der Endpunkt läuft erst nach erfolgreicher Anmeldung im Heimnetz (`webserver: readings at ?` im Log). IP prüfen, Port 80 nicht von einer Firewall blockiert, Gerät und PC im selben Netz |
| Handy zeigt „kein Internet“ und öffnet die Seite nicht | Der DNS-Antworter oder die Prüfantworten fehlen. Im Log müssen `DNS: every name answers with …` und `connectivity check … -> 204` stehen; `dns_task` braucht einen freien UDP-Port 53 |
| Einrichtung springt nach 15 Minuten zurück | Kein Client verbunden und nichts gespeichert (`CONFIGMODE_IDLE_RESTART_MS`). Das ist Absicht – Handy verbinden oder speichern |
| „Temperatur-Offset ungültig“ trotz gültiger Zahl | Komma **und** Punkt werden akzeptiert; erlaubt sind −5,0 bis +5,0 in 0,1-Schritten. Prüfen, ob ein Leerzeichen mitkopiert wurde |
| Offset wirkt nicht | Der Wert wird beim **Speichern** übernommen, die Unit startet neu. Im Log `offset -1.5 C` in der Zeile `device name …` prüfen |
| Nach dem Ändern nur des Offsets verbindet die Unit nicht mehr | War ein Fehler: ein leeres Passwortfeld hat das gespeicherte Passwort gelöscht. Jetzt bleibt es bei unveränderter SSID erhalten (`web: password empty on an unchanged network`) |
| `reset: task watchdog` im Log | Eine angemeldete Task hat 60 s nicht gefeedet – Backtrace im Log darüber zeigt, wo sie hing. Nicht die Ursache wegdrücken, sondern die Stelle suchen |
| Port 8080 antwortet nicht | `WEBSERVER_PUBLIC_PORT` steht auf 0, oder der Port ist im lokalen Netz von einer Firewall gesperrt. `page for outside at http://…:8080/` im Log prüfen |
| Unit verbindet sich mit dem schwächeren Repeater | Sollte nicht mehr passieren: der stärkste BSSID wird gewählt. Im Log zeigt `scan:` alle Kandidaten, `using the strongest` den gewählten; `bssid` im JSON nennt den tatsächlich benutzten |
| Keine Außentemperatur auf der Anzeige | Feld **Außensensor** ist leer (Log: `outdoor sensor not set`). Adresse eintragen - IP, `<name>.local` oder `host:port` (ohne `http://`, ohne `/`) - und speichern |
| `no outdoor temperature from …` im Log | Das Partnergerät antwortet nicht: Adresse prüfen (`curl.exe -i http://<adresse>/api/values` am PC). Bei DDNS muss der Außenport auf **intern 80** zeigen; Port 8080 liefert nur die HTML-Seite und dort `404`. Die Zeile bleibt dann weg, die Anzeige läuft weiter |
| Außentemperatur springt auf `--` bzw. verschwindet | Das Partnergerät meldet `"valid":false` (eigener Sensorfehler) - der Wert wird bewusst nicht übernommen |
| `<hostname>.local` wird nicht aufgelöst | mDNS fehlt beim Client: unter Windows beide Dienste prüfen (`Dnscache`/`fdPHost`), unter Android erst ab Version 12 vorhanden, manche Firmennetze blockieren Multicast. Im Log steht `discovery: announced as http://….local/`; sonst Router-Hostname oder IP nutzen |
| `192.168.4.1` zeigt die Messwertseite statt der Einrichtung | War ein Fehler: Messwert- und Einrichtungs-Server lauschten beide auf Port 80. Behoben durch `webserver_stop()` beim Eintritt in den Konfigurationsmodus – im Log muss `readings endpoint stopped` vor `access point` stehen |
| `Nothing matches the given URI` | Pfad falsch: genau `/api/values`, `/api` oder `/`. Der Server im Konfigurationsmodus kennt diese Pfade nicht |
| JSON zeigt `"valid":false` und `null` | Noch kein Messwert im Speicher: Sensor fehlt (`no SHT30 found at 0x44/0x45` im Log) oder die Abfrage kam vor dem ersten Frame |
| `connect.h: No such file or directory` | [connect.h](main/connect.h) fehlt, weil sie nicht im Repository liegt: `Copy-Item main\connect.example.h main\connect.h` und Werte eintragen |
| Uhrzeit bleibt `01.01. 00:00` | Platzhalter aus [main.c](main/main.c): kein WLAN, kein NTP-Antwort innerhalb `CONNECT_SNTP_WAIT_MS`, oder Port 123/UDP von der Firewall blockiert. Testweise anderen Server in `CONNECT_SNTP_SERVER` eintragen (`pool.ntp.org`) |
| `sun: request failed: ESP_ERR_HTTP_CONNECT` mit `No matching trusted root certificate found` | `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y` fehlt (siehe [HTTPS und Zertifikate](#https-und-zertifikate)) |
| `sun: request failed: ESP_ERR_HTTP_CONNECT` ohne Zertifikatsmeldung | Kein Internetzugang/DNS, `CONNECT_SUN_TIMEOUT_MS` zu klein, oder Port 443 blockiert. Für Details `esp_log_level_set("esp-tls", ESP_LOG_DEBUG)` in [sun.c](main/sun.c) setzen und den Monitor ansehen |
| `sun: API did not answer with status OK` | Der Dienst meldet z. B. `INVALID_REQUEST`: Koordinaten in [connect.h](main/connect.h) prüfen (Punkt als Dezimaltrenner, Bereich −90…90 bzw. −180…180) |
| Sonnenzeiten bleiben bei `(placeholder)` | Die Abfrage läuft alle `CONNECT_SUN_REFRESH_S` (6 h); nach einem Fehlversuch dauert es also bis zu 6 h. Für einen Test den Wert kurz auf 60 setzen |
| `mbedtls_ssl_handshake returned -0x3000` (X509) | Siehe [HTTPS und Zertifikate](#https-und-zertifikate): 16-KB-TLS-Puffer und Cross-Signed-Option |
| Uhrzeit um 1–2 h falsch | Zeitzone: `CONNECT_TZ` in [connect.h](main/connect.h) muss `CET-1CEST,M3.5.0,M10.5.0/3` sein (posix-Regel: `CET` = UTC+1, `CEST` = UTC+2) |
| Wallpaper wirkt unscharf | Sollte nicht mehr vorkommen: seit dem Wechsel auf TJpgDec wird in voller Auflösung dekodiert. Bleibt es unscharf, stammt die geladene Datei aus einem älteren Image – `.\idf -p COM12 flash` schreibt auch die littlefs-Partition neu |
| `wallpaper: … : broken data` / `not a baseline JPEG` | Das JPEG ist beschädigt oder progressiv kodiert; TJpgDec versteht nur Baseline-JPEG. Datei neu speichern (z. B. mit „Progressive" aus) |
| `wallpaper: … work area too small` | `WALLPAPER_WORK_SIZE` in [wallpaper.c](main/wallpaper.c) erhöhen (4 KB reichen für `JD_FASTDECODE 1`) |
| Touch reagiert nicht | `BOARD_TOUCH_ENABLE 1` prüfen und die `touch:`-Zeile im Log ansehen. Der FT6336G braucht den Reset-Puls an GPIO18; der Bus-Scan muss `I2C device found at 0x38` zeigen |
| Punkt erscheint nicht unter dem Finger | Orientierung prüfen, siehe Abschnitt [Touch-Orientierung](#touch-orientierung) |
| RGB-LED macht im Betrieb nichts | Modus prüfen: `status LED: white, dimmed` heißt 30 % Helligkeit – das ist bei Tageslicht am Rückwand-Abstrahl kaum zu sehen. Zum Testen `STATUSLED_IDLE_BRIGHTNESS` in [statusled.h](main/statusled.h) auf 100 setzen |
| RGB-LED zeigt keine Farben / nur eine Farbe | Der Selbsttest direkt nach dem Start (rot, grün, blau, weiß) zeigt, ob der WS2812 arbeitet. Fehlt eine Farbe, stimmt `BOARD_LED_WS2812_PIN` (GPIO42) nicht oder die GRB-Reihenfolge passt nicht (`LED_STRIP_COLOR_COMPONENT_FMT_GRB`) |

### Touch-Orientierung

Der FT6336G liefert bereits **Pixelkoordinaten** (0…239 / 0…319), es gibt also
keine Rohwerte, keine Kalibrierung und keine Z-Schwelle. In der Hochformat-
Ausrichtung dieses Projekts passen die Achsen 1:1 zum Bildschirm:
`BOARD_TOUCH_SWAP_XY 0`, `BOARD_TOUCH_MIRROR_X 0`, `BOARD_TOUCH_MIRROR_Y 0`.

Zum Prüfen das Testbild einschalten (`APP_SHOW_TESTSCREEN 1` in
[main.c](main/main.c), bauen, flashen) und die Ecken antippen. Im Log steht dann
`testscreen: TOUCH: RAW xxxx/yyyy -> xxx/yyy`; der magenta Punkt muss unter dem
Finger sitzen. Wenn nicht:

| Beobachtung | Änderung in [board_es3c28p.h](main/board_es3c28p.h) |
|---|---|
| X und Y vertauscht | `BOARD_TOUCH_SWAP_XY 1` |
| horizontal gespiegelt | `BOARD_TOUCH_MIRROR_X 1` |
| vertikal gespiegelt | `BOARD_TOUCH_MIRROR_Y 1` |
| Punkt um 90° versetzt | `BOARD_TOUCH_SWAP_XY 1` plus das passende `MIRROR_*` |

Die `strength` in `touch_state_t` ist fest 1000 (ein kapazitives Panel liefert
keinen Druck); sie erfüllt nur die „gedrückt"-Prüfungen im restlichen Code.

Änderungen an `main/main.c`, `main/gfx.c` usw. brauchen nur einen neuen Build;
nach Änderungen an `main/idf_component.yml` lädt der Component Manager die
Abhängigkeiten selbst nach. `sdkconfig.defaults` wirkt nur bei neu erzeugter
`sdkconfig` (Ziel neu setzen oder `sdkconfig` löschen).

## Nächste Schritte

1. ~~Partitionierung~~ – erledigt, siehe [Flash-Aufteilung](#flash-aufteilung)
2. ~~littlefs + Wallpaper~~ – erledigt, siehe [Wallpaper-Ablage](#wallpaper-ablage)
3. ~~Sensorik~~ – erledigt: UNIT ENV III, SHT30 (0x44) + QMP6988 (0x70),
   SDA = IO16, SCL = IO15 (geteilt mit dem Touch)
4. ~~Hochformat + Testbild ausgelagert~~ – erledigt: `testscreen.c`, Anzeige 240×320
5. ~~JPEG-Anzeige~~ – erledigt: `wallpaper.c`, `ocean-sunset.jpg` wird dekodiert
6. ~~Temperatur im Großformat~~ – erledigt: `bigdigit.c`, Kachel x 25…215 / y 25…175
7. ~~Luftfeuchte, Luftdruck, Datum/Zeit in der unteren Glasfläche~~ – erledigt:
   `infotile.c`, drei zentrierte Zeilen im Bereich x 25…214 / y 205…294
8. ~~WLAN + NTP~~ – erledigt: `wifi.c`, `clock.c`, Konfiguration in
   `connect.h`, Zeitzone Europe/Berlin, SNTP im Hintergrund; bis zur ersten
   Antwort zeigen die Zeilen `APP_FALLBACK_*` aus [main/main.c](main/main.c)
9. ~~Sonnenauf-/-untergang von sunrise-sunset.org~~ – erledigt: `sun.c` holt die
   Werte per HTTPS, rechnet sie in Ortszeit um und legt sie in `clock.c` ab
   (`clock_sunrise()` / `clock_sunset()`), Auffrischung alle 6 h
10. ~~Bildauswahl automatisch~~ – erledigt: `wallpaper_daytime_for()` (Sonnenaufgang,
    09:00 bzw. 10:00 im Winter, Sonnenuntergang, 23:00) und
    `wallpaper_season_from_month()`; beim Slot-Wechsel wird eines der drei Bilder
    zufällig gewählt. Regeln und Zeiten stehen als `WALLPAPER_*` in
    [main/wallpaper.h](main/wallpaper.h) – die Sonnenzeiten können dabei die Grenze zwischen Tag und Nacht genauer ziehen
    als feste Stunden
11. ~~WLAN am Gerät einrichten~~ – erledigt: Access Point `thermometer`, Seite unter
    `http://192.168.4.1`, Ablage im NVS ([main/configmode.c](main/configmode.c),
    [main/netcfg.c](main/netcfg.c))
12. ~~HTTP-Endpunkt mit JSON~~ – erledigt: `GET /api/values` über
    [main/webserver.c](main/webserver.c), gespeist aus dem Messwertspeicher in
    [main/sensor.c](main/sensor.c)
14. ~~Außentemperatursensor abfragen~~ – erledigt: `outdoor.c` holt den Wert alle 60 s
    und zeigt ihn unter der Raumtemperatur an
15. Mehrere Außensensoren / Räume gleichzeitig (für die Wetterstation)

Optional, nicht für das Raumthermometer nötig:

* microSD-Karte über SPI3 (CS=5) – Achtung: derselbe Bus wie der Touch
* Lautsprecher (GPIO26) mit I2S oder LEDC-PWM, Verstärker über GPIO4
