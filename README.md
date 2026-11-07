# ESPHome DLMS meter

Komponent ESPHome odbierający strumień P1, odszyfrowujący ramki i publikujący
sensory przez MQTT Discovery dla Home Assistant.

Przetestowany na liczniku Kaifa obsługiwanym przez Stoen Operator, z ESPHome
2026.10.2, ESP32-S3 i ESP-IDF. Dane były odbierane przez TCP, a aktualizacje
sensorów sprawdzono w MQTT. Nie oznacza to zgodności ze wszystkimi licznikami
Stoen ani z każdym formatem ich ramek. Nie jest to uniwersalny dekoder DLMS.

Opis profilu poniżej opiera się na dokumencie Stoen Operator „Szyfrowanie i
odszyfrowanie P1”, strony 1–4, oraz na zaobserwowanym działaniu testowanego
licznika. Rozróżnienie między informacjami z dokumentu a ograniczeniami
implementacji jest istotne.

## Konfiguracja

Do konfiguracji ESPHome można dodać poniższy fragment (adresy są przykładami):

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

Tag wskazuje konkretną wersję komponentu. Bez tagu można użyć
`github://kosmita14/esphome-dlms-meter`, ale zmiany domyślnej gałęzi będą wtedy
wpływać na kolejne kompilacje. Edycje README na gałęzi `main` nie zmieniają
już istniejącego taga.

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
sieci oraz brokera MQTT połączonego z Home Assistant. Diodę i pin trzeba
dopasować do płytki: GPIO38 i RGB dotyczą używanej tutaj płytki Waveshare,
nie wszystkich ESP32-S3.

Alternatywnie można użyć fizycznej magistrali `uart:` zamiast `tcp_uart:`;
należy ustawić właściwe piny i parametry transmisji oraz przekazać jej `id`
jako `dlms_meter.uart_id`. Sam komponent nie konfiguruje parametrów UART.
Fizyczne podłączenie UART nie było sprawdzane na tej instalacji.

### Opcje `dlms_meter`

| Opcja | Znaczenie |
|---|---|
| `uart_id` | Wymagane ID źródła `tcp_uart` lub `uart`. |
| `encryption_key` | Wymagany klucz EK, 16 bajtów jako 32 znaki hex. |
| `authentication_key` | Wymagany klucz AK, 16 bajtów jako 32 znaki hex. |
| `status_light` | Wymagane ID światła RGB do sygnalizacji stanu. |
| `mqtt_topic_prefix` | Prefiks topiców; domyślnie `homeassistant/sensor`. |

Prefiks nie może być pusty, zawierać `+` lub `#`, kończyć się `/` ani
przekraczać 1024 bajtów w UTF-8. Dla automatycznego wykrywania sensorów
struktura i prefiks muszą odpowiadać konfiguracji MQTT Discovery w Home
Assistant.

## Profil ramki i szyfrowania Stoen

Dokument opisuje profil AES-128-GCM z uwierzytelnianiem, zmiennym nagłówkiem
i polem długości kodowanym big-endian na dwóch bajtach. W opisanym przykładzie
właściwa część obejmuje bajt Security Control (SC), czterobajtowy FrameCounter
(FC), szyfrogram i 12-bajtowy tag GCM. Długość obejmuje te elementy, ale nie
obejmuje pola długości ani końcówki zewnętrznej ramki.

Według dokumentu w tym profilu SC ma wartość `0x30`, System Title ma osiem
bajtów zerowych, a FC cztery bajty zerowe. IV jest złożeniem System Title i FC;
AAD jest złożeniem SC i klucza AK. EK jest kluczem AES. Te założenia dotyczą
opisanego profilu, a nie dowolnego licznika DLMS.

Obecna implementacja:

1. Zbiera bajty od znacznika `/K` do następnego `/K`, z limitem 32768 bajtów.
   Znacznik może być podzielony pomiędzy odczyty. Ostatnia ramka oczekuje na
   następny znacznik i nie jest publikowana natychmiast po nadejściu końca.
2. W zebranych danych wyszukuje pierwszy ciąg `00 82`, odczytuje długość i
   sprawdza, czy część szyfrowana mieści się w buforze.
3. Odszyfrowuje AES-128-GCM z IV składającym się z 12 zer i AAD `SC + AK`.
   Odrzuca ramkę, gdy uwierzytelnienie tagu się nie powiedzie.
4. Interpretuje odszyfrowany tekst OBIS i publikuje sensory.

### Znane ograniczenia i bezpieczeństwo

- Dokument pokazuje również nagłówek rozpoczynający się inaczej niż `/K`.
  Obecny komponent **nie rozpoznaje dowolnego nagłówka**, w szczególności nie
  obsługuje przedstawionego tam przykładu `/EGM` jako początku ramki.
- Rozdzielanie po `/K` działa dla zaobserwowanego strumienia `/KFM`, ale
  szyfrogram jest binarny i może przypadkowo zawierać ten ciąg. W takim
  przypadku aktualna implementacja może podzielić ramkę w niewłaściwym miejscu.
- SC jest używany w AAD, ale kod nie wymusza wartości `0x30`; FC nie jest
  weryfikowany ani używany do wyliczenia IV. Inne wartości FC nie są więc
  poprawnie obsługiwane. Tag GCM jest sprawdzany, natomiast CRC z końcówki
  ramki opisanej w dokumencie nie jest weryfikowane.
- Stały IV wynika z założeń opisanego profilu, nie jest ogólną regułą DLMS.
  Ponowne użycie IV z tym samym kluczem AES-GCM osłabia poufność i
  uwierzytelnianie. Komponent nie zapewnia ochrony przed odtworzeniem
  wcześniej przechwyconej ramki.
- Nie publikuj kluczy, danych pomiarowych ani pliku `secrets.yaml`. Połączenie
  `tcp_uart` jest zwykłym TCP bez szyfrowania i uwierzytelnienia; korzystaj z
  zaufanej sieci. Analogicznie zabezpiecz dostęp do brokera MQTT.

## Sensory i znaczenie OBIS

Komponent generuje 22 konfiguracje i 22 stany na każdą poprawną ramkę.
Poniższe ID są obecnymi ID MQTT; ich znaczenie nie zawsze odpowiada nazwie.
Interpretacja pól opisanych jako Stoen pochodzi z załączonej specyfikacji i
może nie być uniwersalna dla innych modeli.

| OBIS | ID sensora | Znaczenie |
|---|---|---|
| `0-0:1.0.0` | `timestamp` | Data i czas licznika. |
| `0-0:96.1.0` | — | Numer licznika jako hex tekstu; po dekodowaniu tworzy `meter_id`. |
| `1-0:1.8.0` | `energy_total` | Energia czynna pobrana, suma. |
| `1-0:1.8.1`–`1.8.4` | `energy_tariff_1`–`energy_tariff_4` | Energia pobrana w taryfach T1–T4. |
| `1-0:2.8.0` | `energy_return_total` | Energia czynna oddana, suma. |
| `1-0:2.8.1`–`2.8.4` | `energy_return_t1`–`energy_return_t4` | Energia oddana w taryfach T1–T4. |
| `1-0:1.7.0` | `power` | Chwilowa moc czynna pobrana. |
| `1-0:3.7.0` | `reactive_power_l1` | Według Stoen moc bierna pobrana, **nie faza L1**. |
| `1-0:4.7.0` | `reactive_power_l2` | Według Stoen moc bierna oddana, **nie faza L2**. |
| `1-0:32.7.0`, `52.7.0`, `72.7.0` | `voltage_l1`, `voltage_l2`, `voltage_l3` | Odczyty napięć fazowych używane przez testowany licznik. |
| `0-0:17.0.0` | `meter_status` | Według Stoen próg ograniczenia mocy, a nie ogólny status. |
| `0-0:96.1.1` | `meter_serial_hex` | Według Stoen identyfikator urządzenia ID2; publikowany jako tekst bez dekodowania hex. |
| `0-0:94.98.31`, `.32` | `meter_error_31`, `meter_error_32` | Według Stoen dwa progi ograniczenia mocy, **nie błędy**. |
| `0-0:94.98.33` | `meter_error_33` | Według Stoen tryb ogranicznika mocy, **nie błąd**. |

Nazwy `reactive_power_l1`, `reactive_power_l2`, `meter_status` i `meter_error_*`
pozostawiono dla zgodności z istniejącymi encjami Home Assistant. Nie należy
wyciągać z ich nazw wniosków o fazie lub stanie błędu.

Przykład Stoen zawiera również moc czynną oddaną `1-0:2.7.0` i napięcie
`1-0:12.7.0`: obecnie nie są one publikowane jako sensory. Pole
`0-0:96.13.0` jest opisane jako komunikat dla użytkownika; parser zachowuje
je w wewnętrznej transformacji, ale nie tworzy sensora ani nie dekoduje tekstu.
Gdy `0-0:17.0.0` występuje kilkukrotnie, parser zachowuje ostatnią wartość,
nie rozróżniając progów. Znacznik czasu jest formatowany bez strefy czasowej;
końcowy znacznik sezonu z danych wejściowych jest pomijany.

## MQTT i status

Tematy mają postać `<mqtt_topic_prefix>/<meter_id>/<sensor_id>/config` oraz
`<mqtt_topic_prefix>/<meter_id>/<sensor_id>/state`. ID urządzenia pochodzi z
`0-0:96.1.0`; w razie jego braku używany jest `han_meter`. Wiadomości są
publikowane z QoS 1 i flagą retain. Potwierdzenie przyjęcia publikacji przez
klienta ESPHome nie jest tym samym co potwierdzenie dostarczenia przez broker.
Po zmianie identyfikatora lub prefiksu stare wiadomości retained mogą wymagać
ręcznego usunięcia. Retained stany mogą być nieaktualne po utracie łączności.

Brakujące lub niepoprawne wartości mogą być publikowane jako `undefined` lub
`null`; dla sensorów numerycznych konfiguracja zawiera `value_template`
mapujący te wartości na brak danych.

Wymagana dioda `status_light` sygnalizuje:

- żółty: początkowe oczekiwanie po starcie;
- zielony: niedawno zakończono przekazywanie publikacji poprawnej ramki;
- czerwony: brak poprawnej publikacji przez 5 sekund lub problem z połączeniem.

Przy niedostępnym MQTT niepełne dane są odrzucane. Standardowy `tcp_uart`
utrzymuje połączenie TCP niezależnie od MQTT; dane mogą pozostawać w buforach
sieciowych. Brak sprzętowego sygnału rozłączenia UART oznacza, że w tym
przypadku brak danych jest wykrywany przez timeout diody, nie przez stan kabla.

## Rozwiązywanie problemów

- Brak encji: sprawdź połączenie MQTT, integrację MQTT w Home Assistant,
  prefiks discovery oraz uprawnienia użytkownika brokera do publikacji.
- Błędy AES-GCM: sprawdź parę EK/AK, profil licznika, kompletność ramek i
  jakość transmisji. Nigdy nie publikuj kluczy w logach lub zgłoszeniach.
- Brak odczytów lub stale czerwony status: sprawdź źródło TCP/UART, kolejność
  bajtów oraz to, czy strumień faktycznie używa obsługiwanego początku `/K`.
- Zduplikowane lub stare encje: sprawdź retained konfiguracje na brokerze i
  to, czy numer licznika lub prefiks nie uległy zmianie.

## Rozwój

Po zmianie kodu i testach opublikuj nowy commit i nowy tag; nie nadpisuj
istniejącego `v1.0.0`. Przykładowe polecenia:

```sh
git add README.md components/dlms_meter
git diff --cached --stat
git commit -m "Describe supported Stoen P1 profile"
git push
```

Przed publikacją sprawdź, że nie dodajesz prawdziwych sekretów, prywatnej
konfiguracji ani danych z licznika. Repozytorium nie zawiera licencji.
