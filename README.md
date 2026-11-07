# ESPHome DLMS meter

Komponent ESPHome odbierający strumień licznika Kaifa P1, odszyfrowujący
ramki i publikujący sensory przez MQTT Discovery dla Home Assistant.

Przetestowany z ESPHome 2026.10.2, ESP32-S3 i ESP-IDF. Nie jest uniwersalnym
dekoderem DLMS: obsługuje format opisany poniżej. Inne liczniki lub platformy
mogą wymagać zmian.

## Konfiguracja

Po opublikowaniu repozytorium i utworzeniu stabilnego tagu można dodać do
konfiguracji ESPHome (adresy są przykładami):

```yaml
external_components:
  - source: github://kosmita14/esphome-dlms-meter@v1.0.0
    components: [dlms_meter]

tcp_uart:
  - id: meter_source
    host: 192.0.2.10
    port: 22222

mqtt:
  broker: 192.0.2.20
  username: !secret mqtt_username
  password: !secret mqtt_password
  discovery: false

light:
  - platform: esp32_rmt_led_strip
    id: device_status_led
    internal: true
    pin: GPIO38
    num_leds: 1
    chipset: ws2812
    channel_colors: RGB
    default_transition_length: 0s
    restore_mode: ALWAYS_OFF

dlms_meter:
  uart_id: meter_source
  encryption_key: !secret dlms_encryption_key
  authentication_key: !secret dlms_authentication_key
  mqtt_topic_prefix: homeassistant/sensor
  status_light: device_status_led
```

Tag `v1.0.0` w przykładzie należy najpierw utworzyć i wysłać na GitHub. Bez
tagu można użyć `github://kosmita14/esphome-dlms-meter`, ale zmiany domyślnej
gałęzi będą wtedy wpływać na kolejne kompilacje.

Uzupełnij osobny, niewysyłany do repozytorium plik `secrets.yaml`:

```yaml
mqtt_username: "YOUR_MQTT_USERNAME"
mqtt_password: "YOUR_MQTT_PASSWORD"
dlms_encryption_key: "YOUR_32_HEX_DIGIT_ENCRYPTION_KEY"
dlms_authentication_key: "YOUR_32_HEX_DIGIT_AUTHENTICATION_KEY"
```

Oba klucze muszą mieć po 32 znaki szesnastkowe. Podane napisy są wyłącznie
symbolicznymi placeholderami; należy zastąpić je rzeczywistymi wartościami.
Przykład jest fragmentem: wymaga również podstawowej konfiguracji urządzenia,
sieci oraz brokera MQTT połączonego z Home Assistant. Diode i pin trzeba
dopasować do płytki: GPIO38 i RGB dotyczą używanej tutaj płytki Waveshare,
nie wszystkich ESP32-S3.

Alternatywnie można użyć fizycznej magistrali `uart:` zamiast `tcp_uart:`;
należy ustawić właściwe piny i parametry transmisji oraz przekazać jej `id`
jako `dlms_meter.uart_id`. Sam komponent nie konfiguruje parametrów UART.

## Działanie

- Początek ramki to `/K`; kolejny znacznik zamyka poprzednią. Maksymalny
  rozmiar ramki to 32768 bajtów.
- AES-128-GCM: marker `00 82`, dwubajtowa długość big-endian, security
  control, czterobajtowy licznik, szyfrogram i 12-bajtowy tag. IV składa się
  z 12 zer, AAD to security control i klucz AK. Tag jest weryfikowany.
  Jest to specyficzne dla obsługiwanego formatu, a nie ogólna implementacja
  wszystkich profili DLMS.
- MQTT publikuje 22 konfiguracje i stany sensorów z QoS 1 i retain pod
  `<mqtt_topic_prefix>/<meter_id>/<sensor_id>/config` i `/state`. Identyfikator
  pochodzi z danych licznika; gdy go brak, używany jest `han_meter`. Jeśli
  zmienisz prefiks discovery, dopasuj go również w Home Assistant. Stare
  wiadomości retained mogą wymagać ręcznego usunięcia.
- Brakujące wartości mogą być publikowane jako `null` lub `undefined`;
  dla sensorów numerycznych konfiguracja zawiera odpowiedni `value_template`.
- `status_light` jest wymagane: zielony oznacza ukończenie publikacji niedawnej
  ramki, żółty początkowe oczekiwanie, a czerwony brak poprawnej publikacji
  przez 5 sekund lub problem z połączeniem.
- Przy niedostępnym MQTT niepełne dane są odrzucane. Standardowy `tcp_uart`
  utrzymuje połączenie TCP niezależnie od MQTT; dane mogą pozostawać w
  buforach sieciowych.

## Publikacja na GitHub

Utwórz **puste** repozytorium `kosmita14/esphome-dlms-meter` w GitHubie,
bez inicjalnego README, `.gitignore` i licencji. Następnie w tym katalogu:

```sh
git add .gitignore README.md components/dlms_meter
git diff --cached --stat
git commit -m "Add ESPHome DLMS meter component"
git branch -M main
git remote add origin https://github.com/kosmita14/esphome-dlms-meter.git
git push -u origin main
git tag v1.0.0
git push origin v1.0.0
```

Git może poprosić o skonfigurowanie autora commita i uwierzytelnienie.
Przed `git add` sprawdź, że nie dodajesz prawdziwych sekretów, prywatnej
konfiguracji ani danych z licznika. Nie dodano licencji; wybierz ją osobno,
jeśli chcesz określić warunki używania i rozpowszechniania.
