# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Hoog-niveau Architectuur

Dit project is een tijdschakelaar (TimeSwitch) gebouwd op een ESP32, gebruikmakend van het ESP-IDF framework. Het systeem bestuurt een relais op basis van een wekelijks schema en biedt een touch-interface voor configuratie en handmatige bediening.

De kernfunctionaliteit is verdeeld over de volgende modules in de `main/` map:

- **`main.c`**: Het hart van de applicatie. Het initialiseert alle subsystems en bevat de hoofd-loop die elke 5 seconden de relaisstatus controleert tegen het schema. Het beheert ook de logica voor handmatige "override".
- **Hardware Abstraction**:
    - `lcd.c`/`.h`: Stuurprogramma voor het LCD-scherm.
    - `touch.c`/`.h`: Stuurprogramma voor het touchpaneel, inclusief touch-calibratie (`touch_cal.c`/`.h`).
    - `relay.c`/`.h`: Aan/uit schakelen van het fysieke relais.
- **Applicatielogica & UI**:
    - `settings.c`/`.h`: Beheert persistente opslag (NVS) van het weekschema en andere configuratie.
    - `ui_main.c`/`.h`: Implementeert de gebruikersinterface met de LVGL library.
- **Connectiviteit & Services**:
    - `wifi_manager.c`/`.h`: Behandelt WiFi-connectiviteit. Start een captive portal (`captive_portal.c`/`.h`) als er geen credentials zijn opgeslagen.
    - `time_sync.c`/`.h`: Synchroniseert de systeemtijd via NTP zodra er een WiFi-verbinding is. Dit is essentieel voor de werking van het schema.
    - `ota_server.c`/`.h`: Start een webserver voor Over-The-Air (OTA) firmware-updates.
    - Een **WebSocket server** (geïmpliceerd door `ws_broadcast_relay_state()` calls) wordt gebruikt om de relaisstatus naar verbonden clients te pushen.

### Opstartprocedure

1.  **Initialisatie**: Hardware (LCD, touch, relais) en LVGL worden geïnitialiseerd.
2.  **Configuratie & Kalibratie**: Instellingen worden geladen. Als er geen calibratiegegevens voor het touchscreen zijn, wordt een calibratieroutine gestart.
3.  **WiFi & Netwerk**: De `wifi_manager` probeert verbinding te maken. Bij succes start het NTP-synchronisatie en de OTA-server. Zonder credentials wordt een access point met captive portal gestart.
4.  **Hoofd-loop**: De `check_relay_schedule()` functie draait elke 5 seconden om de relaisstatus te evalueren en bij te werken.

## Algemene Ontwikkelingstaken

Dit is een standaard ESP-IDF project. Gebruik de `idf.py` command-line tool voor ontwikkeling.

- **Project bouwen**:
  ```bash
  idf.py build
  ```

- **Firmware flashen en seriële monitor openen**:
  ```bash
  # Vervang COM3 door je daadwerkelijke poort
  idf.py -p COM3 flash monitor
  ```

- **Projectconfiguratie aanpassen** (bijv. component-instellingen):
  ```bash
  idf.py menuconfig
  ```

- **Alle bestanden opschonen**:
  ```bash
  idf.py fullclean
  ```

## Belangrijke Aandachtspunten
- De logica voor de override (`s_override_active`) in `main.c` is belangrijk. Wanneer een gebruiker het relais handmatig schakelt, wordt het schema tijdelijk genegeerd totdat de volgende geplande statusovergang plaatsvindt.
- Wijzigingen in de relaisstatus moeten altijd worden doorgegeven via `relay_set()`, `ui_main_update_relay()` en `ws_broadcast_relay_state()` om de hardware, UI en web-clients synchroon te houden.
- De tijdconversie in `check_relay_schedule()` zet de `tm_wday` (zondag=0) om naar een weekdag-index waar maandag=0 is. Let hierop bij het werken met schema's.
- Het project is afhankelijk van diverse componenten in de `managed_components` map, waaronder `esp_lvgl_port`.
