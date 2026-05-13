# TimeSwitch

![Static Badge](https://img.shields.io/badge/DEVICE-XIAO_ESP32--S3-8A2BE2) ![Static Badge](https://img.shields.io/badge/MCU-ESP32--S3-8A2BE2)
![Static Badge](https://img.shields.io/badge/OS-FreeRTOS-green) ![Static Badge](https://img.shields.io/badge/SDK-ESP--IDF%20v5.x-blue)

8-kanaals schakelklok gebouwd op een Seeed XIAO ESP32-S3. Stuurt tot 8 relais aan op basis van een instelbaar weekschema per relais. Ondersteunt zowel gewone relais als bistabiele impulsrelais.

## Functies

- Tot 8 relais, elk met een eigen weekschema (Ma t/m Zo, aan- en uitschakeltijd)
- Ondersteuning voor **gewone relais** (GPIO hoog/laag) én **bistabiele impulsrelais** (puls om toestand te wisselen)
- Pulsduur en relaistype per relais instelbaar via de webinterface en opgeslagen in NVS
- Ingebouwde spreiding van pulsen voorkomt PSU-overbelasting bij gelijktijdig schakelen
- Override per relais: handmatige bediening via web negeert het schema tot de volgende schema-overgang
- Real-time statusupdates via **WebSocket** — pagina blijft synchroon zonder herladen
- Mobiel-vriendelijke webinterface
- OTA firmware-update via browser met voortgangsbalk en automatische herdetectie
- WiFi configuratie via captive portal (geen hardcoded credentials)
- Automatische tijdsynchronisatie via NTP (CET/CEST tijdzone)

## Hardware

| Component | Details                      |
| --------- | ---------------------------- |
| Board     | Seeed XIAO ESP32-S3          |
| MCU       | ESP32-S3, 240 MHz dual-core  |
| Flash     | 8 MB                         |
| Relais    | Tot 8 stuks, GPIO1 t/m GPIO8 |

## Pinout relais

| Relais | XIAO-pin | GPIO   |
| ------ | -------- | ------ |
| 1      | D0       | GPIO1  |
| 2      | D1       | GPIO2  |
| 3      | D2       | GPIO3  |
| 4      | D3       | GPIO4  |
| 5      | D4       | GPIO5  |
| 6      | D5       | GPIO6  |
| 7      | D8       | GPIO7  |
| 8      | D9       | GPIO8  |

D6 (GPIO43/TX) en D7 (GPIO44/RX) zijn vrijgehouden voor UART-debugging.
Het aantal actieve relais is instelbaar via `NUM_RELAYS` in `hardware.h`.

## Relay types

**Normaal relais** — GPIO hoog = aan, GPIO laag = uit.

**Bistabiel (impuls) relais** — elke schakelactie stuurt een puls op de GPIO. De pulsduur is instelbaar via `RELAY_PULSE_MS` in `hardware.h` (standaard 250 ms) en per relais aanpasbaar via de webinterface. Bij gelijktijdig schakelen van meerdere relais worden de pulsen automatisch gespreid over `RELAY_INTER_PULSE_MS` (standaard 150 ms) om de voeding niet te overbelasten.

## Projectstructuur

```text
main/
├── main.c              Entry point, schema-check loop (elke 5s), override-logica per relais
├── hardware.h          GPIO-pinnen, relay types, puls-timing, NUM_RELAYS
├── Kconfig.projbuild   WiFi SSID/wachtwoord, NTP-server, tijdzone (menuconfig)
├── relay/              Relay aansturing: normaal + impuls, timer-gebaseerde puls,
│                       automatische spreiding bij gelijktijdige schakelingen
├── settings/           NVS-opslag: weekschema, relaynamen, relay type + pulse_ms
├── wifi/               WiFi manager + captive portal (AP-modus zonder credentials)
├── time/               NTP synchronisatie, tijdzone instelling
└── ota/                HTTP-server: webinterface, REST API, WebSocket, OTA-update
```

## Web interface

| URL                      | Omschrijving                                 |
| ------------------------ | -------------------------------------------- |
| `http://<ip>/`           | Relais overzicht en schakelknoppen           |
| `http://<ip>/schedule`   | Weekschema instellen per relais              |
| `http://<ip>/names`      | Relaynamen, type en pulsduur instellen       |
| `http://<ip>/update`     | OTA firmware-update met voortgangsbalk       |

## Bouwen

Vereist ESP-IDF v5.2 of hoger.

```bash
idf.py build
idf.py -p <poort> flash monitor
```

## Eerste opstart

1. Geen WiFi ingesteld: verbind met het access point `TimeSwitch` en open `http://192.168.4.1`
2. Vul SSID en wachtwoord in — apparaat herstart en verbindt automatisch
3. Tijd wordt gesynchroniseerd via NTP zodra WiFi actief is
4. IP-adres is zichtbaar in de seriële monitor

## Override-gedrag

- Handmatig schakelen terwijl het schema iets anders zegt → **override actief**
- Terugzetten naar de schema-staat → **override direct opgeheven**
- Override eindigt automatisch bij de volgende schema-overgang

## OTA update

Ga naar `http://<ip>/update`, selecteer een `.bin` bestand en klik Upload. Een voortgangsbalk toont de uploadstatus. Na een succesvolle flash herstart het apparaat automatisch en wacht de browser tot het apparaat terug online is.
