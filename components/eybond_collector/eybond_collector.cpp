#ifdef USE_ARDUINO

#include "eybond_collector.h"
#include "endpoint_readback.h"

#include <cinttypes>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include <Arduino.h>

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"

#ifdef USE_DASHBOARD_IMPORT
#include "esphome/components/dashboard_import/dashboard_import.h"
#endif

#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

#ifdef USE_EYBOND_BLE
#include "esphome/components/esp32_ble/ble.h"
#include "esphome/components/esp32_ble/ble_uuid.h"
#include "esphome/components/esp32_ble_server/ble_server.h"
#include "esphome/components/esp32_ble_server/ble_service.h"
#include "esphome/components/esp32_ble_server/ble_2902.h"
#include <esp_gatt_common_api.h>  // esp_ble_gatt_set_local_mtu
#include <esp_gap_ble_api.h>      // esp_ble_tx_power_set
#endif

#ifdef USE_ESP8266
#include <ESP8266WiFi.h>
#else
// ESP32 (esp-arduino) and BK72xx/RTL87xx (LibreTiny) both provide the ESP32-style
// Arduino WiFi/WiFiClient/WiFiUDP classes via <WiFi.h>.
#include <WiFi.h>
#endif

namespace esphome {
namespace eybond_collector {

static const char *const TAG = "eybond_collector";

#ifdef USE_EYBOND_WEB_CONFIG
namespace {

std::string html_escape(const std::string &value) {
  std::string out;
  out.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += ch; break;
    }
  }
  return out;
}

const char *web_message_key(const std::string &message) {
  if (message == "Settings saved. A PN change restarts the collector automatically.") return "msg_saved";
  if (message == "Collector identity saved. A PN change restarts the collector automatically.")
    return "msg_identity_saved";
  if (message == "Endpoint saved. The collector will reconnect using it.") return "msg_endpoint_saved";
  if (message == "UART settings saved in non-volatile memory.") return "msg_uart_saved";
  if (message == "Runtime UART changes are not supported on this platform.") return "msg_uart_unsupported";
  if (message == "Complete UART framing is required.") return "msg_uart_complete";
  if (message == "Invalid endpoint or UART settings.") return "msg_invalid_settings";
  if (message == "Enter a valid host and port.") return "msg_invalid_endpoint";
  if (message == "Unsupported UART framing.") return "msg_invalid_uart";
  if (message == "PN must be one letter followed by 13 or 17 digits.") return "msg_invalid_pn";
  return "";
}

const char *web_value_key(const std::string &value) {
  if (value == "not configured") return "not_configured";
  if (value == "automatic / none") return "automatic_none";
  if (value == "shared with status LED") return "shared_status_led";
  return "";
}

const char *dashboard_project_name() {
#ifdef ESPHOME_PROJECT_NAME
  return ESPHOME_PROJECT_NAME;
#else
  return "";
#endif
}

const char *dashboard_project_version() {
#ifdef ESPHOME_PROJECT_VERSION
  return ESPHOME_PROJECT_VERSION;
#else
  return "";
#endif
}

std::string dashboard_package_import_url() {
#ifdef USE_DASHBOARD_IMPORT
  return dashboard_import::get_package_import_url();
#else
  return "";
#endif
}

static const char I18N_EN[] PROGMEM = R"JS(window.EYBOND_I18N={language:'Language',firmware:'Firmware',hostname:'Hostname',loading_networks:'Loading nearby networks…',network_name:'Network name (SSID)',wifi_password:'New Wi-Fi password',wifi_hint:'Saving changes the network immediately. Leave this section untouched if Wi-Fi is already correct.',save_wifi:'Save Wi-Fi and reconnect',current_network:'Current network',connected_network:'Connected',saved_network:'Saved, not connected',network_not_connected:'No current connection',unknown_network:'unknown network',collector_identity:'Collector identity',use_default_pn:'Use default PN',collector_pn:'Collector PN',pn_hint:'Changing PN changes this device’s identity. If it was already added to Home Assistant, remove it there and add it again.',confirm_pn_change:'Changing the collector PN changes its identity. If this device was already added to Home Assistant, you will need to remove and add it again. Continue?',confirm_endpoint_change:'Changing the endpoint disconnects the collector from its current Home Assistant connection. Make sure the new host and port are reachable. Continue?',confirm_uart_change:'Changing UART settings can stop communication with the inverter. The new framing will be saved in non-volatile memory. Continue?',save_identity:'Save collector identity',ha_endpoint:'Home Assistant endpoint',host:'Host',port:'Port',endpoint_hint:'TCP is used. Saving reconnects the collector to this endpoint.',save_endpoint:'Save endpoint',inverter_uart:'Inverter UART',current_framing:'Current framing',baud_rate:'Baud rate',data_bits:'Data bits',parity:'Parity',stop_bits:'Stop bits',uart_readonly:'This platform cannot change UART at runtime. Change the YAML and reflash the device.',uart_hint:'The complete UART framing is saved in the device’s non-volatile memory, applied immediately, and used after every restart.',save_uart:'Save UART settings',hardware_mapping:'Hardware mapping',tx_pin:'TX pin',rx_pin:'RX pin',direction_pin:'RS485 direction pin',status_led_pin:'Status LED pin',com_led_pin:'Communication LED pin',esphome_hostname:'ESPHome hostname',hardware_hint:'Pins and the ESPHome hostname are build-time hardware identity settings. They are shown here but are not rewritten by this firmware version.',firmware_update:'Firmware update',upload_firmware:'Upload firmware',ota_hint:'Upload an ESPHome firmware .bin built for this board.',no_networks:'No nearby networks found. Enter the SSID manually.',scan_unavailable:'Wi-Fi scan unavailable. Enter the SSID manually.',msg_saved:'Settings saved. A PN change restarts the collector automatically.',msg_identity_saved:'Collector identity saved. A PN change restarts the collector automatically.',msg_endpoint_saved:'Endpoint saved. The collector will reconnect using it.',msg_uart_saved:'UART settings saved in non-volatile memory.',msg_uart_unsupported:'Runtime UART changes are not supported on this platform.',msg_uart_complete:'Complete UART framing is required.',msg_invalid_settings:'Invalid endpoint or UART settings.',msg_invalid_endpoint:'Enter a valid host and port.',msg_invalid_uart:'Unsupported UART framing.',msg_invalid_pn:'PN must be one letter followed by 13 or 17 digits.',not_configured:'not configured',automatic_none:'automatic / none',shared_status_led:'shared with status LED',switch_light_theme:'Switch to light theme',switch_dark_theme:'Switch to dark theme',uart_diagnostics:'UART / RS485 diagnostics',collector_link:'Home Assistant link',connected:'Connected',disconnected:'Disconnected',response_pending:'Inverter request',idle:'Idle',waiting:'Waiting for response',transmit:'Transmit (TX)',receive:'Receive (RX)',tx_bytes:'Total bytes',rx_bytes:'Total bytes',uart_requests:'Requests sent',last_tx:'Last transmit',last_rx:'Last receive',never:'never',seconds_ago:'s ago',diagnostics_hint:'Counters reset after reboot. RX shows all bytes received on the physical line; activity without an inverter can indicate noise, wiring issues, or an incorrect UART speed.',dashboard_adoption:'ESPHome Dashboard',dashboard_import_available:'Dashboard adoption available',dashboard_import_unavailable:'Dashboard adoption is not included in this build',project:'Project',package_import:'Import package'};)JS";

static const char I18N_UK[] PROGMEM = R"JS(window.EYBOND_I18N={language:'Мова',firmware:'Прошивка',hostname:'Ім’я хоста',loading_networks:'Пошук доступних мереж…',network_name:'Назва мережі (SSID)',wifi_password:'Новий пароль Wi-Fi',wifi_hint:'Збереження одразу змінить мережу. Не використовуйте цей розділ, якщо Wi-Fi уже налаштовано правильно.',save_wifi:'Зберегти Wi-Fi та перепідключитися',current_network:'Поточна мережа',connected_network:'Підключено',saved_network:'Збережено, зв’язку немає',network_not_connected:'Немає поточного підключення',unknown_network:'невідома мережа',collector_identity:'Ідентифікація колектора',use_default_pn:'Використовувати типовий PN',collector_pn:'PN колектора',pn_hint:'Зміна PN змінює ідентичність цього пристрою. Якщо його вже додано до Home Assistant, видаліть його там і додайте знову.',confirm_pn_change:'Зміна PN колектора змінює його ідентичність. Якщо пристрій уже додано до Home Assistant, його доведеться видалити та додати знову. Продовжити?',confirm_endpoint_change:'Зміна endpoint розірве поточне з’єднання колектора з Home Assistant. Переконайтеся, що новий хост і порт доступні. Продовжити?',confirm_uart_change:'Зміна параметрів UART може припинити обмін з інвертором. Новий framing буде збережено в енергонезалежній пам’яті. Продовжити?',save_identity:'Зберегти ідентичність колектора',ha_endpoint:'Endpoint Home Assistant',host:'Хост',port:'Порт',endpoint_hint:'Використовується TCP. Після збереження колектор перепідключиться до цього endpoint.',save_endpoint:'Зберегти endpoint',inverter_uart:'UART інвертора',current_framing:'Поточний framing',baud_rate:'Швидкість',data_bits:'Біти даних',parity:'Парність',stop_bits:'Стоп-біти',uart_readonly:'Ця платформа не підтримує зміну UART під час роботи. Змініть YAML і перепрошийте пристрій.',uart_hint:'Повний UART framing зберігається в енергонезалежній пам’яті пристрою, застосовується одразу та використовується після кожного перезапуску.',save_uart:'Зберегти параметри UART',hardware_mapping:'Апаратна конфігурація',tx_pin:'Пін TX',rx_pin:'Пін RX',direction_pin:'Пін напрямку RS485',status_led_pin:'Пін LED стану',com_led_pin:'Пін LED обміну',esphome_hostname:'Hostname ESPHome',hardware_hint:'Піни та hostname ESPHome є апаратними параметрами збірки. Вони показані тут, але ця версія прошивки їх не змінює.',firmware_update:'Оновлення прошивки',upload_firmware:'Завантажити прошивку',ota_hint:'Завантажте ESPHome-файл .bin, зібраний для цієї плати.',no_networks:'Мереж поблизу не знайдено. Введіть SSID вручну.',scan_unavailable:'Сканування Wi-Fi недоступне. Введіть SSID вручну.',msg_saved:'Налаштування збережено. Після зміни PN колектор перезапуститься автоматично.',msg_identity_saved:'Ідентичність колектора збережено. Після зміни PN колектор перезапуститься автоматично.',msg_endpoint_saved:'Endpoint збережено. Колектор перепідключиться з його використанням.',msg_uart_saved:'Параметри UART збережено в енергонезалежній пам’яті.',msg_uart_unsupported:'Ця платформа не підтримує зміну UART під час роботи.',msg_uart_complete:'Потрібно вказати повний UART framing.',msg_invalid_settings:'Некоректний endpoint або параметри UART.',msg_invalid_endpoint:'Введіть коректний хост і порт.',msg_invalid_uart:'Непідтримуваний UART framing.',msg_invalid_pn:'PN має складатися з однієї літери та 13 або 17 цифр.',not_configured:'не налаштовано',automatic_none:'автоматично / відсутній',shared_status_led:'спільний з LED стану',switch_light_theme:'Увімкнути світлу тему',switch_dark_theme:'Увімкнути темну тему',uart_diagnostics:'Діагностика UART / RS485',collector_link:'Зв’язок із Home Assistant',connected:'Підключено',disconnected:'Не підключено',response_pending:'Запит до інвертора',idle:'Очікування запитів',waiting:'Очікування відповіді',transmit:'Передавання (TX)',receive:'Приймання (RX)',tx_bytes:'Усього байтів',rx_bytes:'Усього байтів',uart_requests:'Надіслано запитів',last_tx:'Останнє передавання',last_rx:'Останнє приймання',never:'ніколи',seconds_ago:'с тому',diagnostics_hint:'Лічильники скидаються після перезапуску. RX показує всі байти з фізичної лінії; активність без інвертора може вказувати на шум, помилку підключення або неправильну швидкість UART.',dashboard_adoption:'ESPHome Dashboard',dashboard_import_available:'Доступне додавання до Dashboard',dashboard_import_unavailable:'Ця збірка не підтримує додавання до Dashboard',project:'Проєкт',package_import:'Пакет імпорту'};)JS";

static const char I18N_RU[] PROGMEM = R"JS(window.EYBOND_I18N={language:'Язык',firmware:'Прошивка',hostname:'Имя хоста',loading_networks:'Поиск доступных сетей…',network_name:'Имя сети (SSID)',wifi_password:'Новый пароль Wi-Fi',wifi_hint:'Сохранение сразу изменит сеть. Не используйте этот раздел, если Wi-Fi уже настроен правильно.',save_wifi:'Сохранить Wi-Fi и переподключиться',current_network:'Текущая сеть',connected_network:'Подключено',saved_network:'Сохранено, связи нет',network_not_connected:'Нет текущего подключения',unknown_network:'неизвестная сеть',collector_identity:'Идентификация коллектора',use_default_pn:'Использовать PN по умолчанию',collector_pn:'PN коллектора',pn_hint:'Изменение PN меняет идентичность этого устройства. Если оно уже добавлено в Home Assistant, удалите его там и добавьте заново.',confirm_pn_change:'Изменение PN коллектора меняет его идентичность. Если устройство уже добавлено в Home Assistant, его потребуется удалить и добавить заново. Продолжить?',confirm_endpoint_change:'Изменение эндпоинта разорвёт текущее соединение коллектора с Home Assistant. Убедитесь, что новые хост и порт доступны. Продолжить?',confirm_uart_change:'Изменение параметров UART может остановить обмен с инвертором. Новый framing будет сохранён в энергонезависимой памяти. Продолжить?',save_identity:'Сохранить идентичность коллектора',ha_endpoint:'Эндпоинт Home Assistant',host:'Хост',port:'Порт',endpoint_hint:'Используется TCP. После сохранения коллектор переподключится к этому эндпоинту.',save_endpoint:'Сохранить эндпоинт',inverter_uart:'UART инвертора',current_framing:'Текущий framing',baud_rate:'Скорость',data_bits:'Биты данных',parity:'Чётность',stop_bits:'Стоп-биты',uart_readonly:'Эта платформа не поддерживает изменение UART во время работы. Измените YAML и перепрошейте устройство.',uart_hint:'Полный UART framing сохраняется в энергонезависимой памяти устройства, применяется сразу и используется после каждой перезагрузки.',save_uart:'Сохранить параметры UART',hardware_mapping:'Аппаратная конфигурация',tx_pin:'Пин TX',rx_pin:'Пин RX',direction_pin:'Пин направления RS485',status_led_pin:'Пин LED состояния',com_led_pin:'Пин LED обмена',esphome_hostname:'Hostname ESPHome',hardware_hint:'Пины и hostname ESPHome являются аппаратными параметрами сборки. Они показаны здесь, но эта версия прошивки их не изменяет.',firmware_update:'Обновление прошивки',upload_firmware:'Загрузить прошивку',ota_hint:'Загрузите ESPHome-файл .bin, собранный для этой платы.',no_networks:'Сети поблизости не найдены. Введите SSID вручную.',scan_unavailable:'Сканирование Wi-Fi недоступно. Введите SSID вручную.',msg_saved:'Настройки сохранены. После изменения PN коллектор перезапустится автоматически.',msg_identity_saved:'Идентичность коллектора сохранена. После изменения PN коллектор перезапустится автоматически.',msg_endpoint_saved:'Эндпоинт сохранён. Коллектор переподключится с его использованием.',msg_uart_saved:'Параметры UART сохранены в энергонезависимой памяти.',msg_uart_unsupported:'Эта платформа не поддерживает изменение UART во время работы.',msg_uart_complete:'Необходимо указать полный UART framing.',msg_invalid_settings:'Некорректный эндпоинт или параметры UART.',msg_invalid_endpoint:'Введите корректный хост и порт.',msg_invalid_uart:'Неподдерживаемый UART framing.',msg_invalid_pn:'PN должен состоять из одной буквы и 13 или 17 цифр.',not_configured:'не настроено',automatic_none:'автоматически / отсутствует',shared_status_led:'общий с LED состояния',switch_light_theme:'Включить светлую тему',switch_dark_theme:'Включить тёмную тему',uart_diagnostics:'Диагностика UART / RS485',collector_link:'Связь с Home Assistant',connected:'Подключено',disconnected:'Не подключено',response_pending:'Запрос к инвертору',idle:'Ожидание запросов',waiting:'Ожидание ответа',transmit:'Передача (TX)',receive:'Приём (RX)',tx_bytes:'Всего байт',rx_bytes:'Всего байт',uart_requests:'Отправлено запросов',last_tx:'Последняя передача',last_rx:'Последний приём',never:'никогда',seconds_ago:'с назад',diagnostics_hint:'Счётчики сбрасываются после перезагрузки. RX показывает все байты с физической линии; активность без инвертора может указывать на шум, ошибку подключения или неправильную скорость UART.',dashboard_adoption:'ESPHome Dashboard',dashboard_import_available:'Доступно добавление в Dashboard',dashboard_import_unavailable:'Эта сборка не поддерживает добавление в Dashboard',project:'Проект',package_import:'Пакет импорта'};)JS";

static const char WEB_CSS[] PROGMEM = R"CSS(:root{color-scheme:dark;--bg:#101316;--panel:#1a1f24;--panel-soft:#20262c;--input:#252c32;--text:#eef2f5;--muted:#aeb8c1;--border:#3b454e;--accent:#039bd3;--accent-soft:#103747;--row:#22292f;--row-hover:#293740;--signal-off:#59656e;--ok:#69d58a;--warn:#f4bf58;--err:#ff8178}:root[data-theme=light]{color-scheme:light;--bg:#f4f6f8;--panel:#fff;--panel-soft:#f5f7f8;--input:#fff;--text:#202428;--muted:#626d75;--border:#c8d0d6;--accent:#027fae;--accent-soft:#e0f3fa;--row:#f0f3f5;--row-hover:#e3f2f8;--signal-off:#adb6bd;--ok:#187333;--warn:#9a6500;--err:#b3261e}*{box-sizing:border-box}body{font-family:system-ui,-apple-system,sans-serif;max-width:50rem;margin:0 auto;padding:1.4rem 1rem 3rem;color:var(--text);background:var(--bg);line-height:1.45}.page-header{margin:.7rem 0 1.3rem}.page-header h1{margin:.2rem 0 .8rem;font-size:1.85rem}.identity-grid{display:grid;grid-template-columns:1.5fr .7fr 1.2fr;gap:.65rem}.meta-card{min-width:0;padding:.7rem .8rem;background:var(--panel);border:1px solid var(--border);border-radius:.55rem}.meta-label{display:block;color:var(--muted);font-size:.75rem;text-transform:uppercase;letter-spacing:.04em}.meta-value{display:block;margin-top:.2rem;overflow-wrap:anywhere;font-weight:600}fieldset{border:1px solid var(--border);border-radius:.7rem;margin:1rem 0;padding:1rem 1.1rem;background:var(--panel)}legend{padding:0 .4rem;font-weight:650}label{display:block;margin:.7rem 0}.hint{color:var(--muted);font-size:.9rem}.grid{display:grid;grid-template-columns:1fr 1fr;gap:.7rem}.status-grid{display:grid;grid-template-columns:1fr 1fr;gap:.65rem;margin:.3rem 0 1rem}.status-card{display:flex;align-items:center;gap:.6rem;padding:.65rem .75rem;border:1px solid var(--border);border-radius:.55rem;background:var(--panel-soft)}.status-dot{flex:0 0 auto;width:.72rem;height:.72rem;border-radius:50%;background:var(--signal-off);box-shadow:0 0 0 3px color-mix(in srgb,var(--signal-off) 18%,transparent)}.status-dot.good{background:var(--ok)}.status-dot.bad{background:var(--err)}.status-dot.waiting{background:var(--warn)}.status-copy{display:flex;flex-direction:column}.status-copy small{color:var(--muted)}.traffic-grid{display:grid;grid-template-columns:1fr 1fr;gap:.75rem}.traffic-card{padding:.8rem;border:1px solid var(--border);border-radius:.6rem;background:var(--panel-soft)}.traffic-card h3{margin:0 0 .55rem}.stat{display:flex;justify-content:space-between;gap:1rem;padding:.38rem 0;border-bottom:1px solid var(--border)}.stat:last-child{border-bottom:0}input,select,button{font:inherit;padding:.58rem;width:100%;color:var(--text);background:var(--input);border:1px solid var(--border);border-radius:.4rem}input[type=checkbox]{width:auto;margin-right:.5rem}input:disabled,input[readonly]{opacity:.76}.form-actions{display:flex;justify-content:flex-end;margin-top:.8rem}.form-actions button{width:auto;min-width:11rem}button{background:var(--accent);color:#fff;border:0;cursor:pointer;font-weight:600}button:hover{filter:brightness(1.08)}.network{display:flex;align-items:center;justify-content:space-between;width:100%;margin:.38rem 0;padding:.65rem .75rem;background:var(--row);color:var(--text);border:1px solid transparent;text-align:left}.network:hover{background:var(--row-hover)}.network.current{border-color:var(--accent);background:var(--accent-soft)}.network.offline{border-style:dashed}.network-name{min-width:0;overflow:hidden;text-overflow:ellipsis}.network-meta{display:flex;align-items:center;gap:.55rem;margin-left:.7rem}.network-badge{width:auto;padding:.16rem .42rem;border-radius:999px;background:var(--accent);color:#fff;font-size:.72rem;white-space:nowrap}.network.offline .network-badge{background:var(--err)}.wifi-icon{position:relative;display:inline-flex;align-items:flex-end;gap:2px;width:23px;height:18px}.wifi-icon i{display:block;width:4px;background:var(--signal-off);border-radius:2px}.wifi-icon i.on{background:var(--text)}.wifi-icon i:nth-child(1){height:4px}.wifi-icon i:nth-child(2){height:8px}.wifi-icon i:nth-child(3){height:12px}.wifi-icon i:nth-child(4){height:16px}.wifi-icon.offline:after{content:'×';position:absolute;right:-5px;top:-8px;color:var(--err);font-weight:800;font-size:1rem}.toolbar{display:flex;align-items:center;justify-content:flex-end;gap:.55rem}.toolbar select{width:auto;padding:.38rem}.theme-toggle{width:2.45rem;height:2.3rem;padding:0;background:var(--input);color:var(--text);border:1px solid var(--border);font-size:1.15rem}.ok{color:var(--ok)}.err{color:var(--err)}.message{padding:.7rem .85rem;border-radius:.5rem;background:var(--panel)}code{word-break:break-all}.package-url{font-size:.82rem}@media(max-width:38rem){.identity-grid,.grid,.status-grid,.traffic-grid{grid-template-columns:1fr}.form-actions button{width:100%}.stat{font-size:.93rem}})CSS";

static const char WEB_JS[] PROGMEM = R"JS(let lang=new URLSearchParams(location.search).get('lang')||localStorage.getItem('eybond_lang')||((navigator.language||'en').toLowerCase().startsWith('uk')?'uk':(navigator.language||'en').toLowerCase().startsWith('ru')?'ru':'en'),T={},lastDiag=null;
function tr(k,f){return T[k]||f||k}function setText(id,value){let e=document.getElementById(id);if(e)e.textContent=value}function applyTranslations(){document.querySelectorAll('[data-i18n]').forEach(e=>{let k=e.dataset.i18n;if(k&&T[k])e.textContent=T[k]});document.querySelectorAll('[data-i18n-value]').forEach(e=>{let k=e.dataset.i18nValue;if(k&&T[k])e.value=T[k]});document.querySelectorAll('.wifi-icon').forEach(updateSignalLabel);renderDiagnostics(lastDiag);syncThemeButton()}
function loadLanguage(next){lang=['en','uk','ru'].includes(next)?next:'en';localStorage.setItem('eybond_lang',lang);document.documentElement.lang=lang;document.getElementById('language').value=lang;let s=document.createElement('script');s.src='/i18n.js?lang='+lang;s.dataset.lang=lang;s.onload=()=>{if(s.dataset.lang!==lang)return;T=window.EYBOND_I18N||{};applyTranslations()};document.head.appendChild(s)}
const themeButton=document.getElementById('theme-toggle');function currentTheme(){return document.documentElement.dataset.theme==='light'?'light':'dark'}function syncThemeButton(){let dark=currentTheme()==='dark';themeButton.textContent=dark?'☀':'☾';themeButton.title=tr(dark?'switch_light_theme':'switch_dark_theme');themeButton.setAttribute('aria-label',themeButton.title)}themeButton.onclick=()=>{let next=currentTheme()==='dark'?'light':'dark';document.documentElement.dataset.theme=next;localStorage.setItem('eybond_theme',next);syncThemeButton()};syncThemeButton();
document.getElementById('language').onchange=e=>loadLanguage(e.target.value);loadLanguage(lang);
const pnDefault=document.getElementById('use-default-pn'),pnInput=document.getElementById('collector-pn'),identityForm=document.getElementById('identity-form');function targetPn(){return(pnDefault.checked?pnInput.dataset.defaultPn:pnInput.value).toUpperCase()}function syncPn(){pnInput.readOnly=pnDefault.checked;if(pnDefault.checked)pnInput.value=pnInput.dataset.defaultPn}pnDefault.onchange=syncPn;pnInput.oninput=()=>pnInput.value=pnInput.value.toUpperCase();identityForm.onsubmit=()=>targetPn()===pnInput.dataset.currentPn||confirm(tr('confirm_pn_change'));syncPn();
const endpointForm=document.getElementById('endpoint-form'),endpointHost=document.getElementById('endpoint-host'),endpointPort=document.getElementById('endpoint-port');endpointForm.onsubmit=()=>endpointHost.value.trim()===endpointHost.dataset.currentValue&&endpointPort.value===endpointPort.dataset.currentValue||confirm(tr('confirm_endpoint_change'));
const uartForm=document.getElementById('uart-form');function uartFraming(){let f=new FormData(uartForm);return[f.get('baud'),f.get('data_bits'),f.get('stop_bits'),f.get('parity')].join(',')}uartForm.onsubmit=()=>uartFraming()===uartForm.dataset.currentFraming||confirm(tr('confirm_uart_change'));
function ageText(value){return value==null?tr('never','never'):(Math.floor(value/100)/10)+' '+tr('seconds_ago','s ago')}function setStatus(id,state,text){let e=document.getElementById(id);if(!e)return;e.className='status-dot '+state;setText(id+'-text',text)}function renderDiagnostics(d){if(!d)return;setStatus('diag-link-dot',d.collector_connected?'good':'bad',tr(d.collector_connected?'connected':'disconnected'));setStatus('diag-pending-dot',d.response_pending?'waiting':'good',tr(d.response_pending?'waiting':'idle'));setText('diag-tx-bytes',d.tx_bytes);setText('diag-rx-bytes',d.rx_bytes);setText('diag-requests',d.tx_operations);setText('diag-last-tx',ageText(d.last_tx_age_ms));setText('diag-last-rx',ageText(d.last_rx_age_ms))}function refreshDiagnostics(){fetch('/diagnostics.json',{cache:'no-store'}).then(r=>r.json()).then(d=>{lastDiag=d;renderDiagnostics(d)}).catch(()=>{})}refreshDiagnostics();setInterval(refreshDiagnostics,2000);
function updateSignalLabel(sig){let label=sig.dataset.offline==='1'?tr('network_not_connected','No current connection'):sig.dataset.rssi+' dBm';sig.title=label;sig.setAttribute('aria-label',label)}function signalIcon(rssi,offline){let sig=document.createElement('span');sig.className='wifi-icon'+(offline?' offline':'');sig.dataset.offline=offline?'1':'0';sig.dataset.rssi=rssi;let level=offline?0:rssi>=-55?4:rssi>=-67?3:rssi>=-75?2:rssi>=-85?1:0;for(let i=1;i<=4;i++){let bar=document.createElement('i');if(i<=level)bar.className='on';sig.appendChild(bar)}updateSignalLabel(sig);return sig}function networkRow(a,current,connected){let b=document.createElement('button');b.type='button';b.className='network'+(current?' current':'')+(current&&!connected?' offline':'');let n=document.createElement('span');n.className='network-name';n.textContent=a.ssid;let m=document.createElement('span');m.className='network-meta';if(current){let badge=document.createElement('span');badge.className='network-badge';badge.dataset.i18n=connected?'connected_network':'saved_network';m.appendChild(badge)}let lock=document.createElement('span');lock.textContent=a.lock?'🔒':'';m.append(lock,signalIcon(Number(a.rssi==null?-100:a.rssi),current&&!connected));b.append(n,m);b.onclick=()=>{document.getElementById('ssid').value=a.ssid;document.querySelector("input[name='psk']").focus()};return b}
fetch('/config.json').then(r=>r.json()).then(c=>{let d=document.getElementById('wifi-networks'),current=d.dataset.currentSsid||'',connected=d.dataset.currentConnected==='1',currentRssi=Number(d.dataset.currentRssi||-100);d.textContent='';let aps=(c.aps||[]).filter(a=>a.ssid).sort((a,b)=>(b.rssi||-100)-(a.rssi||-100)),found=false;aps.forEach(a=>{let isCurrent=!found&&!!current&&a.ssid===current;if(isCurrent){found=true;if(connected)a.rssi=currentRssi}d.appendChild(networkRow(a,isCurrent,connected))});if(current&&!found)d.prepend(networkRow({ssid:current,rssi:currentRssi,lock:true},true,connected));if(!aps.length&&!current){let p=document.createElement('p');p.className='hint';p.dataset.i18n='no_networks';d.appendChild(p)}applyTranslations()}).catch(()=>{let d=document.getElementById('wifi-networks'),current=d.dataset.currentSsid||'',connected=d.dataset.currentConnected==='1';d.textContent='';if(current)d.appendChild(networkRow({ssid:current,rssi:Number(d.dataset.currentRssi||-100),lock:true},true,connected));else{let p=document.createElement('p');p.className='hint';p.dataset.i18n='scan_unavailable';d.appendChild(p)}applyTranslations()});)JS";

class EybondWebConfigurationHandler : public AsyncWebHandler {
 public:
  explicit EybondWebConfigurationHandler(EybondCollector *owner) : owner_(owner) {}

  // ESPAsyncWebServer skips parsing POST bodies for trivial handlers. Our
  // settings form is application/x-www-form-urlencoded, so opt into parsing.
  bool isRequestHandlerTrivial() const override { return false; }

  bool canHandle(AsyncWebServerRequest *request) const override {
    const String &url = request->url();
    return (request->method() == HTTP_GET &&
            (url == "/" || url == "/eybond" || url == "/config.json" || url == "/diagnostics.json" ||
             url == "/wifisave" ||
             url == "/i18n.js" || url == "/app.css" || url == "/app.js")) ||
           (request->method() == HTTP_POST &&
            (url == "/identity/save" || url == "/endpoint/save" || url == "/uart/save" ||
             url == "/settings/save" || url == "/eybond/save"));
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    const String &url = request->url();
    if (url == "/identity/save") {
      this->handle_identity_save_(request);
      return;
    }
    if (url == "/endpoint/save") {
      this->handle_endpoint_save_(request);
      return;
    }
    if (url == "/uart/save") {
      this->handle_uart_save_(request);
      return;
    }
    if (url == "/settings/save" || url == "/eybond/save") {
      this->handle_save_(request);
      return;
    }
    if (url == "/eybond") {
      request->redirect("/");
      return;
    }
    if (url == "/i18n.js") {
      this->send_translations_(request);
      return;
    }
    if (url == "/app.css") {
      this->send_progmem_(request, "text/css; charset=utf-8", WEB_CSS);
      return;
    }
    if (url == "/app.js") {
      this->send_progmem_(request, "application/javascript; charset=utf-8", WEB_JS);
      return;
    }
    if (url == "/diagnostics.json") {
      this->send_diagnostics_(request);
      return;
    }
    if (url == "/") {
      this->send_page_(request, "", false);
      return;
    }

    // Reuse ESPHome's own Wi-Fi scan/save implementation; only the HTML page
    // is ours. This keeps Wi-Fi persistence and reconnect behavior canonical.
    auto *portal = captive_portal::global_captive_portal;
    if (portal != nullptr) {
      portal->handleRequest(request);
    } else {
      request->send(404, "text/plain", "Not found");
    }
  }

 private:
  void send_progmem_(AsyncWebServerRequest *request, const char *content_type, const char *content) const {
    auto *response = request->beginResponse(200, content_type, reinterpret_cast<const uint8_t *>(content),
                                            strlen_P(content));
    response->addHeader("Cache-Control", "public, max-age=86400");
    request->send(response);
  }

  void send_translations_(AsyncWebServerRequest *request) const {
    const String lang = request->arg("lang");
    const char *content = lang == "uk" ? I18N_UK : lang == "ru" ? I18N_RU : I18N_EN;
    this->send_progmem_(request, "application/javascript; charset=utf-8", content);
  }

  void send_diagnostics_(AsyncWebServerRequest *request) const {
    const uint32_t now = millis();
    const UartDiagnosticsSnapshot diagnostics = owner_->uart_diagnostics_snapshot(now);
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    response->printf(
        "{\"collector_connected\":%s,\"response_pending\":%s,\"tx_bytes\":%" PRIu32
        ",\"rx_bytes\":%" PRIu32 ",\"rx_response_bytes\":%" PRIu32
        ",\"rx_ignored_bytes\":%" PRIu32 ",\"tx_operations\":%" PRIu32 ",",
        owner_->collector_link_connected() ? "true" : "false",
        owner_->uart_response_pending() ? "true" : "false", diagnostics.tx_bytes, diagnostics.rx_bytes,
        diagnostics.rx_response_bytes, diagnostics.rx_ignored_bytes, diagnostics.tx_operations);
    if (diagnostics.has_tx_activity) {
      response->printf("\"last_tx_age_ms\":%u,", static_cast<unsigned>(diagnostics.last_tx_age_ms));
    } else {
      response->print(F("\"last_tx_age_ms\":null,"));
    }
    if (diagnostics.has_rx_activity) {
      response->printf("\"last_rx_age_ms\":%u}", static_cast<unsigned>(diagnostics.last_rx_age_ms));
    } else {
      response->print(F("\"last_rx_age_ms\":null}"));
    }
    request->send(response);
  }

  String form_arg_(AsyncWebServerRequest *request, const char *name) const {
    const AsyncWebParameter *parameter = request->getParam(name, true);
    if (parameter == nullptr) parameter = request->getParam(name);
    return parameter != nullptr ? parameter->value() : String();
  }

  bool has_form_arg_(AsyncWebServerRequest *request, const char *name) const {
    return request->hasParam(name, true) || request->hasParam(name);
  }

  bool same_origin_(AsyncWebServerRequest *request) const {
    if (!request->hasHeader("Origin")) {
      // Native clients such as curl do not send Origin. Browser form submits
      // from our own page do, and are checked below.
      return true;
    }
    const String expected = String("http://") + request->host();
    return request->header("Origin") == expected;
  }

  bool reject_cross_origin_(AsyncWebServerRequest *request) const {
    if (this->same_origin_(request)) return false;
    request->send(403, "text/plain", "Cross-origin configuration request rejected.");
    return true;
  }

  bool parse_port_(AsyncWebServerRequest *request, uint16_t *port) const {
    const String port_text = this->form_arg_(request, "port");
    char *port_end = nullptr;
    const uint32_t value = strtoul(port_text.c_str(), &port_end, 10);
    if (port_text.length() == 0 || port_end == nullptr || *port_end != '\0' || value == 0 || value > 65535)
      return false;
    *port = static_cast<uint16_t>(value);
    return true;
  }

  std::string uart_form_value_(AsyncWebServerRequest *request) const {
    return std::string(this->form_arg_(request, "baud").c_str()) + "," +
           this->form_arg_(request, "data_bits").c_str() + "," +
           this->form_arg_(request, "stop_bits").c_str() + "," +
           this->form_arg_(request, "parity").c_str();
  }

  void handle_identity_save_(AsyncWebServerRequest *request) {
    if (this->reject_cross_origin_(request)) return;
    std::string error;
    if (!owner_->stage_web_collector_identity(this->has_form_arg_(request, "use_default_pn"),
                                              this->form_arg_(request, "collector_pn").c_str(), &error)) {
      this->send_page_(request, error, true);
      return;
    }
    this->send_page_(request, "Collector identity saved. A PN change restarts the collector automatically.", false);
  }

  void handle_endpoint_save_(AsyncWebServerRequest *request) {
    if (this->reject_cross_origin_(request)) return;
    uint16_t port = 0;
    std::string error;
    if (!this->parse_port_(request, &port) ||
        !owner_->stage_web_endpoint(this->form_arg_(request, "host").c_str(), port, &error)) {
      this->send_page_(request, error.empty() ? "Enter a valid host and port." : error, true);
      return;
    }
    this->send_page_(request, "Endpoint saved. The collector will reconnect using it.", false);
  }

  void handle_uart_save_(AsyncWebServerRequest *request) {
    if (this->reject_cross_origin_(request)) return;
    const bool complete = this->has_form_arg_(request, "baud") && this->has_form_arg_(request, "data_bits") &&
                          this->has_form_arg_(request, "stop_bits") && this->has_form_arg_(request, "parity");
    if (!complete) {
      this->send_page_(request, "Complete UART framing is required.", true);
      return;
    }
    std::string error;
    if (!owner_->stage_web_uart(this->uart_form_value_(request), &error)) {
      this->send_page_(request, error, true);
      return;
    }
    this->send_page_(request, "UART settings saved in non-volatile memory.", false);
  }

  void handle_save_(AsyncWebServerRequest *request) {
    if (this->reject_cross_origin_(request)) return;
    const std::string host = this->form_arg_(request, "host").c_str();
    const String port_text = this->form_arg_(request, "port");
    char *port_end = nullptr;
    const uint32_t port_value = strtoul(port_text.c_str(), &port_end, 10);
    const bool port_valid = port_text.length() > 0 && port_end != nullptr && *port_end == '\0' &&
                            port_value > 0 && port_value <= 65535;
    std::string uart;
    const bool has_uart_fields = this->has_form_arg_(request, "baud") || this->has_form_arg_(request, "data_bits") ||
                                 this->has_form_arg_(request, "stop_bits") ||
                                 this->has_form_arg_(request, "parity");
    const bool has_complete_uart = this->has_form_arg_(request, "baud") &&
                                   this->has_form_arg_(request, "data_bits") &&
                                   this->has_form_arg_(request, "stop_bits") &&
                                   this->has_form_arg_(request, "parity");
    if (!owner_->runtime_uart_configuration_supported() && has_uart_fields) {
      this->send_page_(request, "Runtime UART changes are not supported on this platform.", true);
      return;
    }
    if (owner_->runtime_uart_configuration_supported() && has_uart_fields && !has_complete_uart) {
      this->send_page_(request, "Complete UART framing is required.", true);
      return;
    }
    if (owner_->runtime_uart_configuration_supported() && has_complete_uart) {
      uart = std::string(this->form_arg_(request, "baud").c_str()) + "," +
             this->form_arg_(request, "data_bits").c_str() + "," +
             this->form_arg_(request, "stop_bits").c_str() + "," +
             this->form_arg_(request, "parity").c_str();
    }
    const bool pn_config_present = this->has_form_arg_(request, "pn_config_present");
    const bool use_default_pn = this->has_form_arg_(request, "use_default_pn");
    const std::string pn = this->form_arg_(request, "collector_pn").c_str();
    std::string error;
    if (!port_valid ||
        !owner_->stage_web_settings(host, static_cast<uint16_t>(port_value), uart, pn_config_present,
                                    use_default_pn, pn, &error)) {
      this->send_page_(request, error.empty() ? "Invalid endpoint or UART settings." : error, true);
      return;
    }
    this->send_page_(request, "Settings saved. A PN change restarts the collector automatically.", false);
  }

  void send_page_(AsyncWebServerRequest *request, const std::string &message, bool error) {
    const std::string host = html_escape(owner_->web_display_server_host());
    const unsigned port = static_cast<unsigned>(owner_->web_display_server_port());
    const std::string display_uart = owner_->web_display_uart_settings();
    eybond::UartSettings display_uart_settings{};
    const bool display_uart_valid = eybond::parse_uart_settings(display_uart, &display_uart_settings);
    const unsigned baud = display_uart_valid ? static_cast<unsigned>(display_uart_settings.baud)
                                             : static_cast<unsigned>(owner_->current_baud_rate());
    const unsigned data_bits = display_uart_valid ? static_cast<unsigned>(display_uart_settings.data_bits)
                                                  : static_cast<unsigned>(owner_->current_uart_data_bits());
    const unsigned stop_bits = display_uart_valid ? static_cast<unsigned>(display_uart_settings.stop_bits)
                                                  : static_cast<unsigned>(owner_->current_uart_stop_bits());
    const std::string parity = display_uart_valid
                                   ? (display_uart_settings.parity == eybond::UartParity::EVEN
                                          ? "EVEN"
                                          : display_uart_settings.parity == eybond::UartParity::ODD ? "ODD" : "NONE")
                                   : owner_->current_uart_parity();
    const bool uart_runtime = owner_->runtime_uart_configuration_supported();
    const std::string uart = html_escape(display_uart);
    const std::string collector_pn = html_escape(owner_->web_display_collector_pn());
    const std::string default_collector_pn = html_escape(owner_->default_collector_pn());
    const bool use_default_pn = owner_->web_display_uses_default_collector_pn();
    const std::string hostname = html_escape(App.get_name());
    const std::string wifi_ssid = html_escape(WiFi.SSID().c_str());
    const bool wifi_connected = network::is_connected();
    const int wifi_rssi = wifi_connected ? WiFi.RSSI() : -100;
    const int tx_pin = owner_->current_uart_tx_pin();
    const int rx_pin = owner_->current_uart_rx_pin();
    const std::string direction_pin = html_escape(owner_->current_direction_pin());
    const std::string status_led_pin = html_escape(owner_->current_status_led_pin());
    const std::string com_led_pin = html_escape(owner_->current_com_led_pin());
    const std::string dashboard_project = html_escape(dashboard_project_name());
    const std::string dashboard_version = html_escape(dashboard_project_version());
    const std::string dashboard_package = html_escape(dashboard_package_import_url());
    const bool dashboard_available = !dashboard_project.empty() && !dashboard_package.empty();
    const std::string tx_pin_label = tx_pin >= 0 ? std::string("GPIO") + std::to_string(tx_pin) : "not configured";
    const std::string rx_pin_label = rx_pin >= 0 ? std::string("GPIO") + std::to_string(rx_pin) : "not configured";
    AsyncResponseStream *response = request->beginResponseStream("text/html; charset=utf-8");
    response->addHeader("Cache-Control", "no-store");
    response->print(F(
        "<!doctype html><html data-theme='dark'><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>EyeBond Collector</title><link rel='stylesheet' href='/app.css'>"
        "<script>if(localStorage.getItem('eybond_theme')==='light')document.documentElement.dataset.theme='light'</script></head>"
        "<body><div class='toolbar'><button id='theme-toggle' class='theme-toggle' type='button'>☀</button>"
        "<span data-i18n='language'>Language</span>"
        "<select id='language'><option value='en'>English</option><option value='uk'>Українська</option>"
        "<option value='ru'>Русский</option></select></div><header class='page-header'><h1>EyeBond Collector</h1>"
        "<div class='identity-grid'>"));
    response->printf(
        "<div class='meta-card'><span class='meta-label'>PN</span><code class='meta-value'>%s</code></div>"
        "<div class='meta-card'><span class='meta-label' data-i18n='firmware'>Firmware</span>"
        "<span class='meta-value'>%s</span></div>"
        "<div class='meta-card'><span class='meta-label' data-i18n='hostname'>Hostname</span>"
        "<code class='meta-value'>%s</code></div></div></header>",
        collector_pn.c_str(), eybond::BRIDGE_VERSION, hostname.c_str());
    if (!message.empty()) {
      response->printf("<p class='message %s' data-i18n='%s'>%s</p>", error ? "err" : "ok", web_message_key(message),
                       html_escape(message).c_str());
    }
    response->print(F(
        "<form method='get' action='/wifisave'><fieldset><legend>Wi-Fi</legend>"
        "<div id='wifi-networks'"));
    response->printf(" data-current-ssid='%s' data-current-connected='%u' data-current-rssi='%d'>",
                     wifi_ssid.c_str(), wifi_connected ? 1U : 0U, wifi_rssi);
    response->print(F(
        "<p class='hint' data-i18n='loading_networks'>Loading nearby networks…</p></div>"
        "<label><span data-i18n='network_name'>Network name (SSID)</span><input id='ssid' name='ssid' maxlength='32' required autocomplete='off'></label>"
        "<label><span data-i18n='wifi_password'>New Wi-Fi password</span><input name='psk' maxlength='64' type='password' autocomplete='new-password'></label>"
        "<p class='hint' data-i18n='wifi_hint'>Saving changes the network immediately. Leave this section untouched if Wi-Fi is already correct.</p>"
        "<div class='form-actions'><button type='submit' data-i18n='save_wifi'>Save Wi-Fi and reconnect</button></div>"
        "</fieldset></form>"));

    response->print(F(
        "<form id='identity-form' method='post' action='/identity/save'>"
        "<fieldset><legend data-i18n='collector_identity'>Collector identity</legend>"));
    response->printf(
        "<label><input id='use-default-pn' type='checkbox' name='use_default_pn' value='1'%s>"
        "<span data-i18n='use_default_pn'>Use default PN</span></label>",
        use_default_pn ? " checked" : "");
    response->printf(
        "<label><span data-i18n='collector_pn'>Collector PN</span><input id='collector-pn' name='collector_pn' "
        "maxlength='18' pattern='[A-Z][0-9]{13}|[A-Z][0-9]{17}' required value='%s' data-current-pn='%s' "
        "data-default-pn='%s'%s></label>",
        collector_pn.c_str(), collector_pn.c_str(), default_collector_pn.c_str(), use_default_pn ? " readonly" : "");
    response->print(F(
        "<p class='hint' data-i18n='pn_hint'>Changing PN changes this device's identity. If it was already added to Home Assistant, remove it there and add it again.</p>"
        "<div class='form-actions'><button type='submit' data-i18n='save_identity'>Save collector identity</button></div>"
        "</fieldset></form><form id='endpoint-form' method='post' action='/endpoint/save'>"
        "<fieldset><legend data-i18n='ha_endpoint'>Home Assistant endpoint</legend>"));
    response->printf(
        "<label><span data-i18n='host'>Host</span><input id='endpoint-host' name='host' maxlength='63' required "
        "value='%s' data-current-value='%s' placeholder='192.168.1.50'></label>",
        host.c_str(),
        host.c_str());
    response->printf(
        "<label><span data-i18n='port'>Port</span><input id='endpoint-port' name='port' type='number' min='1' "
        "max='65535' required value='%u' data-current-value='%u'></label>",
        port == 0 ? 18899U : port, port == 0 ? 18899U : port);
    response->print(F(
        "<p class='hint' data-i18n='endpoint_hint'>TCP is used. Saving reconnects the collector to this endpoint.</p>"
        "<div class='form-actions'><button type='submit' data-i18n='save_endpoint'>Save endpoint</button></div>"
        "</fieldset></form><form id='uart-form' method='post' action='/uart/save' data-current-framing='"));
    response->print(uart.c_str());
    response->print(F("'>"
        "<fieldset><legend data-i18n='inverter_uart'>Inverter UART</legend>"));
    response->printf("<p class='hint'><span data-i18n='current_framing'>Current framing</span>: %s</p>", uart.c_str());
    response->printf("<div class='grid'><label><span data-i18n='baud_rate'>Baud rate</span><select name='baud' %s>", uart_runtime ? "" : "disabled");
    static const uint32_t BAUDS[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
    for (const uint32_t candidate : BAUDS) {
      response->printf("<option value='%u'%s>%u</option>", static_cast<unsigned>(candidate),
                       candidate == baud ? " selected" : "", static_cast<unsigned>(candidate));
    }
    response->print(F("</select></label>"));
    response->printf("<label><span data-i18n='data_bits'>Data bits</span><select name='data_bits' %s>", uart_runtime ? "" : "disabled");
    for (unsigned candidate = 5; candidate <= 8; candidate++) {
      response->printf("<option value='%u'%s>%u</option>", candidate, candidate == data_bits ? " selected" : "",
                       candidate);
    }
    response->print(F("</select></label>"));
    response->printf("<label><span data-i18n='parity'>Parity</span><select name='parity' %s>", uart_runtime ? "" : "disabled");
    static const char *const PARITIES[] = {"NONE", "EVEN", "ODD"};
    for (const char *candidate : PARITIES) {
      response->printf("<option value='%s'%s>%s</option>", candidate, parity == candidate ? " selected" : "",
                       candidate);
    }
    response->print(F("</select></label>"));
    response->printf("<label><span data-i18n='stop_bits'>Stop bits</span><select name='stop_bits' %s>", uart_runtime ? "" : "disabled");
    for (unsigned candidate = 1; candidate <= 2; candidate++) {
      response->printf("<option value='%u'%s>%u</option>", candidate, candidate == stop_bits ? " selected" : "",
                       candidate);
    }
    response->print(F("</select></label></div>"));
    if (!uart_runtime) {
      response->print(F(
          "<p class='hint' data-i18n='uart_readonly'>This platform cannot change UART at runtime. Change the YAML and reflash the device.</p>"));
    } else {
      response->print(F(
          "<p class='hint' data-i18n='uart_hint'>The complete UART framing is saved in the device's non-volatile memory, applied immediately, and used after every restart.</p>"
          "<div class='form-actions'><button type='submit' data-i18n='save_uart'>Save UART settings</button></div>"));
    }
    response->print(F(
        "</fieldset></form><fieldset><legend data-i18n='uart_diagnostics'>UART / RS485 diagnostics</legend>"
        "<div class='status-grid'>"
        "<div class='status-card'><span id='diag-link-dot' class='status-dot'></span><div class='status-copy'>"
        "<small data-i18n='collector_link'>Home Assistant link</small><strong id='diag-link-dot-text'>—</strong></div></div>"
        "<div class='status-card'><span id='diag-pending-dot' class='status-dot'></span><div class='status-copy'>"
        "<small data-i18n='response_pending'>Inverter request</small><strong id='diag-pending-dot-text'>—</strong></div></div>"
        "</div><div class='traffic-grid'>"
        "<section class='traffic-card'><h3 data-i18n='transmit'>Transmit (TX)</h3>"
        "<div class='stat'><span data-i18n='tx_bytes'>Total bytes</span><strong id='diag-tx-bytes'>0</strong></div>"
        "<div class='stat'><span data-i18n='uart_requests'>Requests sent</span><strong id='diag-requests'>0</strong></div>"
        "<div class='stat'><span data-i18n='last_tx'>Last transmit</span><strong id='diag-last-tx'>—</strong></div></section>"
        "<section class='traffic-card'><h3 data-i18n='receive'>Receive (RX)</h3>"
        "<div class='stat'><span data-i18n='rx_bytes'>Total bytes</span><strong id='diag-rx-bytes'>0</strong></div>"
        "<div class='stat'><span data-i18n='last_rx'>Last receive</span><strong id='diag-last-rx'>—</strong></div></section>"
        "</div><p class='hint' data-i18n='diagnostics_hint'>Counters reset after reboot. RX shows all bytes received on the physical line; activity without an inverter can indicate noise, wiring issues, or an incorrect UART speed.</p>"
        "</fieldset><fieldset><legend data-i18n='hardware_mapping'>Hardware mapping</legend>"));
    response->printf("<div class='grid'><label><span data-i18n='tx_pin'>TX pin</span><input disabled value='%s' data-i18n-value='%s'></label>",
                     tx_pin_label.c_str(), web_value_key(tx_pin_label));
    response->printf("<label><span data-i18n='rx_pin'>RX pin</span><input disabled value='%s' data-i18n-value='%s'></label>",
                     rx_pin_label.c_str(), web_value_key(rx_pin_label));
    response->printf("<label><span data-i18n='direction_pin'>RS485 direction pin</span><input disabled value='%s' data-i18n-value='%s'></label>", direction_pin.c_str(), web_value_key(direction_pin));
    response->printf("<label><span data-i18n='status_led_pin'>Status LED pin</span><input disabled value='%s' data-i18n-value='%s'></label>", status_led_pin.c_str(), web_value_key(status_led_pin));
    response->printf("<label><span data-i18n='com_led_pin'>Communication LED pin</span><input disabled value='%s' data-i18n-value='%s'></label>", com_led_pin.c_str(), web_value_key(com_led_pin));
    response->printf("<label><span data-i18n='esphome_hostname'>ESPHome hostname</span><input disabled value='%s'></label></div>", hostname.c_str());
    response->print(F(
        "<p class='hint' data-i18n='hardware_hint'>Pins and the ESPHome hostname are build-time hardware identity settings. They are shown here "
        "but are not rewritten by this firmware version.</p></fieldset>"));
    response->print(F("<fieldset><legend data-i18n='dashboard_adoption'>ESPHome Dashboard</legend>"));
    if (dashboard_available) {
      response->print(F("<p class='ok' data-i18n='dashboard_import_available'>Dashboard adoption available</p>"));
      response->printf(
          "<p><span data-i18n='project'>Project</span>: <code>%s</code> <code>%s</code></p>"
          "<p class='package-url'><span data-i18n='package_import'>Import package</span>: <code>%s</code></p>",
          dashboard_project.c_str(), dashboard_version.c_str(), dashboard_package.c_str());
    } else {
      response->print(F(
          "<p class='hint' data-i18n='dashboard_import_unavailable'>Dashboard adoption is not included in this build</p>"));
    }
    response->print(F(
        "</fieldset>"
        "<form method='post' action='/update' enctype='multipart/form-data'><fieldset><legend data-i18n='firmware_update'>Firmware update</legend>"
        "<input type='file' name='update' accept='.bin' required>"
        "<p class='hint' data-i18n='ota_hint'>Upload an ESPHome firmware .bin built for this board.</p>"
        "<div class='form-actions'><button type='submit' data-i18n='upload_firmware'>Upload firmware</button></div>"
        "</fieldset></form>"
        "<script src='/app.js' defer></script></body></html>"));
    request->send(response);
  }

  EybondCollector *owner_;
};

}  // namespace
#endif

// Status/com LED timing.
static const uint32_t STATUS_FAST_BLINK_MS = 250;   // no Wi-Fi
static const uint32_t STATUS_SLOW_BLINK_MS = 1000;  // Wi-Fi up, bridge not connected
static const uint32_t COM_FLICKER_MS = 50;          // com toggle cap -> ~10 Hz flicker, never solid
static const uint32_t COM_HOLD_MS = 120;            // treat as "data flowing" for this long after an event

void EybondCollector::setup() {
  if (status_led_pin_ != nullptr) {
    status_led_pin_->setup();
    this->write_led_(status_led_pin_, &status_led_state_, false);
  }
  if (com_led_pin_ != nullptr) {
    com_led_pin_->setup();
    this->write_led_(com_led_pin_, &com_led_state_, false);
  }

  baud_pref_ = global_preferences->make_preference<uint32_t>(BAUD_PREF_HASH);
  uart_pref_ = global_preferences->make_preference<PersistedUartSettings>(UART_PREF_HASH);
  pn_pref_ = global_preferences->make_preference<PersistedCollectorPn>(PN_PREF_HASH);
  this->restore_persisted_uart_settings_();

  eybond::CollectorProfile profile;
  uint8_t mac[6];
  get_mac_address_raw(mac);
  if (!pn_override_.empty()) {
    default_collector_pn_ = pn_override_;
  } else {
    default_collector_pn_ = eybond::synthesize_pn(mac);
  }
  collector_pn_ = default_collector_pn_;
  this->restore_persisted_collector_pn_();
  profile.pn = collector_pn_;
  profile.uart = this->uart_settings_string_();

  core_ = std::unique_ptr<eybond::CollectorCore>(
      new eybond::CollectorCore(this, profile, core_config_));

#ifdef USE_EYBOND_BLE
  // Stash the identity the BLE provisioning service advertises and reports; the
  // GATT service itself is created lazily in loop() (see setup_ble_).
  ble_pn_ = profile.pn;
  ble_fw_version_ = profile.firmware_version;
  ble_at_version_ = profile.at_version;
#endif

  this->configure_fallback_ap_(profile.pn);

  // A runtime-written server endpoint (HA "HA only" bind / move to another HA)
  // is persisted in NVS and takes priority over the YAML static_server option,
  // mirroring how ESPHome treats a saved Wi-Fi STA over the YAML default.
  // Incoming discovery still overrides it live.
  endpoint_pref_ = global_preferences->make_preference<PersistedEndpoint>(ENDPOINT_PREF_HASH);
  PersistedEndpoint saved{};
  bool restored = false;
  if (endpoint_pref_.load(&saved) && saved.host[0] != '\0' && saved.port != 0) {
    saved.host[sizeof(saved.host) - 1] = '\0';
    suppress_endpoint_persist_ = true;  // avoid rewriting the same value at boot
    core_->set_server_endpoint(std::string(saved.host), saved.port, millis());
    suppress_endpoint_persist_ = false;
    restored = true;
    ESP_LOGI(TAG, "Restored persisted server endpoint %s:%u", saved.host, saved.port);
  }
  if (!restored && !static_server_host_.empty()) {
    core_->set_server_endpoint(static_server_host_, static_server_port_, millis());
  }

#ifdef USE_EYBOND_WEB_CONFIG
  if (web_configuration_enabled_ && !web_handler_registered_ &&
      web_server_base::global_web_server_base != nullptr) {
    web_server_base::global_web_server_base->add_handler(new EybondWebConfigurationHandler(this));
    web_handler_registered_ = true;
    // Do not call WebServerBase::init() at this component's pre-Wi-Fi setup
    // priority. On ESP32 that would start AsyncTCP before lwIP has created its
    // mutexes. The handler is registered now so the captive portal can use it;
    // loop() acquires the always-on LAN server reference after STA is online.
    ESP_LOGI(TAG, "Local configuration page handler registered");
  }
#endif
}

void EybondCollector::loop() {
  if (core_ == nullptr) {
    return;
  }
  const uint32_t now = millis();

#ifdef USE_EYBOND_BLE
  // BLE provisioning must run even with no network — that is its whole point
  // (bring the bridge onto Wi-Fi over Bluetooth), so it precedes the
  // network-down early return below.
  if (ble_enabled_) {
    if (ble_ == nullptr) {
      this->setup_ble_();  // lazy: retries until esp32_ble_server is up
    }
    this->process_ble_(now);
    this->poll_wifi_scan_(now);  // keep the scan cache fresh for AT+INTPARA49?
  }
#endif

#ifdef USE_EYBOND_WEB_CONFIG
  if (web_handler_registered_ && !web_server_initialized_ && network::is_connected() &&
      web_server_base::global_web_server_base != nullptr) {
    // Keep one independent reference for the always-available LAN page. The
    // captive portal temporarily takes/releases its own reference, so ending
    // setup mode cannot tear down this server.
    web_server_base::global_web_server_base->init();
    web_server_initialized_ = true;
    ESP_LOGI(TAG, "Local configuration page enabled at /");
  }
#endif

  if (!network::is_connected()) {
    if (tcp_up_) {
      tcp_.stop();
      tcp_up_ = false;
      core_->on_tcp_closed(now);
    }
    this->process_reboot_(now);
    this->update_leds_(now);
    return;
  }

  if (!udp_started_) {
    udp_started_ = udp_.begin(udp_port_) != 0;
    if (udp_started_) {
      ESP_LOGI(TAG, "Discovery listener on UDP %u", udp_port_);
    }
  }

  // UDP discovery datagrams
  if (udp_started_) {
    int packet_len = udp_.parsePacket();
    while (packet_len > 0) {
      uint8_t buffer[128];
      const int read_len = udp_.read(buffer, sizeof(buffer));
      if (read_len > 0) {
        // udp_reply() inside the core call replies to udp_.remoteIP()/remotePort()
        core_->on_udp_datagram(buffer, static_cast<size_t>(read_len), now);
      }
      packet_len = udp_.parsePacket();
    }
  }

  // TCP link state + inbound data
  if (tcp_up_ && !tcp_.connected()) {
    tcp_.stop();
    tcp_up_ = false;
    ESP_LOGW(TAG, "Server connection lost");
    core_->on_tcp_closed(now);
  }
  if (tcp_up_) {
    int available = tcp_.available();
    while (available > 0) {
      uint8_t buffer[128];
      const int chunk = available > static_cast<int>(sizeof(buffer)) ? sizeof(buffer) : available;
      const int read_len = tcp_.read(buffer, chunk);
      if (read_len <= 0) {
        break;
      }
      core_->on_tcp_data(buffer, static_cast<size_t>(read_len), now);
      available = tcp_.available();
    }
  }

  // Inverter UART -> core
  uint32_t rx_bytes = 0;
  uint32_t rx_response_bytes = 0;
  uint32_t rx_ignored_bytes = 0;
  uint32_t rx_last_at = 0;
  while (this->available() > 0) {
    uint8_t byte;
    if (!this->read_byte(&byte)) {
      break;
    }
    rx_bytes++;
    rx_last_at = millis();
    // Only inverter bytes answering a pending forward are real comms; idle-line
    // noise on a floating/disconnected RX must not light the com LED.
    if (core_->awaiting_uart_response()) {
      rx_response_bytes++;
      this->note_activity_(millis());
    } else {
      rx_ignored_bytes++;
    }
    core_->on_uart_data(&byte, 1, millis());
  }
  if (rx_bytes > 0) {
    LockGuard lock(uart_diagnostics_mutex_);
    uart_rx_bytes_ += rx_bytes;
    uart_rx_response_bytes_ += rx_response_bytes;
    uart_rx_ignored_bytes_ += rx_ignored_bytes;
    last_uart_rx_ms_ = rx_last_at;
    has_uart_rx_activity_ = true;
  }

  core_->loop(millis());
  this->process_pending_connect_();
  this->process_endpoint_apply_(millis());
  this->process_web_uart_apply_();
  this->process_pn_apply_(millis());
  this->process_wifi_apply_(millis());
  this->process_reboot_(millis());
  this->poll_wifi_scan_(millis());
  this->update_leds_(millis());
}

bool EybondCollector::query_param(uint8_t parameter, std::string *out) {
  switch (parameter) {
    case 6: {  // hardware_version
      // Virtual-bridge marker, carried on a parameter every collector answers
      // (FC=2 param 6), so the integration identifies this bridge WITHOUT any
      // unsupported AT probe (which real collectors ignore -> timeout) and WITHOUT
      // the PN (which a user may set to any value, e.g. a real collector's). Format:
      //   "esp-collector/<bridge-version>/<platform>"
      // HA keys the bridge off the "esp-collector/" prefix, reads the version for
      // diagnostics, and matches the platform suffix (bk72/rtl87/libretiny) to
      // gate runtime-UART capability. Format is intentionally extensible: parsers
      // must ignore extra trailing segments (reserved for future capability flags,
      // e.g. a cloud-passthrough hint on reflashed factory hardware).
      const char *platform =
#if defined(USE_ESP8266)
          "ESP8266";
#elif defined(USE_LIBRETINY)
          "BK72xx/RTL87xx";
#else
          "ESP32";
#endif
      *out = std::string("esp-collector/") + eybond::BRIDGE_VERSION + "/" + platform;
      return true;
    }
    case 16: {  // local_ip_address (IPAddress::toString() is absent on LibreTiny)
      const IPAddress ip = WiFi.localIP();
      char buffer[16];
      std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
      *out = buffer;
      return true;
    }
    case 21:
      return eybond::pending_endpoint_readback(pending_endpoint_, endpoint_apply_requested_,
                                               staged_endpoint_host_, staged_endpoint_port_, out);
    case 30:  // Pending means accepted but not yet applied, not only scheduled.
      *out = (eybond::endpoint_apply_pending(pending_endpoint_, endpoint_apply_requested_) || wifi_apply_requested_ ||
              !pending_wifi_ssid_.empty() || !pending_wifi_password_.empty()) ? "1" : "0";
      return true;
    case 41:  // router_ssid (current STA network)
      *out = WiFi.SSID().c_str();
      return true;
    case 48:  // network_diagnostics; the integration extracts the RSSI from text
      *out = this->wifi_rssi();
      return true;
    case 49: {  // wifi_scan_list: "[ssid,rssi][ssid2,rssi2]..."
      if (!wifi_scan_running_) {
        WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
        wifi_scan_running_ = true;
      }
      if (wifi_scan_cache_.empty()) {
        // Scan still in flight: report at least the network we are on.
        *out = std::string("[") + WiFi.SSID().c_str() + "," + this->wifi_rssi() + "]";
      } else {
        *out = wifi_scan_cache_;
      }
      return true;
    }
    default:
      return false;
  }
}

uint8_t EybondCollector::set_param(uint8_t parameter, const std::string &value) {
  if (parameter == 34) {  // serial_baudrate
    const uint32_t baud = static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 10));
    return this->apply_baud_rate(baud) ? 0 : 1;
  }
  if (parameter == 21) {  // domain_address_1: staged server endpoint
    std::string host;
    uint16_t port = 0;
    if (endpoint_apply_requested_ || !eybond::parse_server_endpoint(value, &host, &port)) return 1;
    pending_endpoint_ = value;
    return 0;
  }
  if (parameter == 29) {  // system_operation: commit staged changes or restart
    bool had_staged = !pending_endpoint_.empty();
#ifdef USE_WIFI
    had_staged = had_staged || !pending_wifi_ssid_.empty() || !pending_wifi_password_.empty();
#endif
    bool committed = false;
    if (!pending_endpoint_.empty()) {
      std::string host;
      uint16_t port = 0;
      if (eybond::parse_server_endpoint(pending_endpoint_, &host, &port) && core_ != nullptr) {
        // Defer the switch: applying the endpoint closes the current TCP
        // session, and doing that synchronously here would drop the socket
        // before the FC=3 ack for THIS request is flushed to HA (mirrors the
        // deferred Wi-Fi apply below).
        staged_endpoint_host_ = host;
        staged_endpoint_port_ = port;
        endpoint_apply_requested_ = true;
        endpoint_apply_at_ms_ = millis() + 750;
        committed = true;
      }
      pending_endpoint_.clear();
    }
#ifdef USE_WIFI
    if (!pending_wifi_ssid_.empty()) {
      // Defer the Wi-Fi reconnect so the FC=3 response reaches HA first.
      wifi_apply_requested_ = true;
      wifi_apply_at_ms_ = millis() + 750;
      committed = true;
    }
#endif
    if (committed) {
      return 0;
    }
    if (had_staged) {
      return 1;  // staged but invalid/incomplete; do not reinterpret as restart
    }
    bool apply_in_flight = endpoint_apply_requested_;
#ifdef USE_WIFI
    apply_in_flight = apply_in_flight || wifi_apply_requested_;
#endif
    if (apply_in_flight) {
      // A commit is already deferred (param 21/41 + 29 arrived moments ago and
      // the apply window has not elapsed). A retried param 29 in this window
      // acknowledges that commit; treating it as a bare restart request would
      // turn the endpoint switch into an unwanted reboot.
      return 0;
    }
    this->request_reboot_(millis());
    return 0;
  }
#ifdef USE_WIFI
  switch (parameter) {
    case 41:  // target router SSID (staged)
      pending_wifi_ssid_ = value;
      return 0;
    case 43:  // target router password (staged)
      pending_wifi_password_ = value;
      return 0;
    default:
      break;
  }
#endif
  (void) value;
  return 1;
}

void EybondCollector::on_server_endpoint_changed(const std::string &host, uint16_t port) {
  if (suppress_endpoint_persist_) {
    return;  // restoring from NVS at boot; don't rewrite the same value
  }
  PersistedEndpoint saved{};
  std::snprintf(saved.host, sizeof(saved.host), "%s", host.c_str());
  saved.port = port;
  endpoint_pref_.save(&saved);
  global_preferences->sync();
  ESP_LOGI(TAG, "Persisted server endpoint %s:%u", saved.host, static_cast<unsigned>(port));
}

bool EybondCollector::baud_supported_(uint32_t baud) {
  static const uint32_t SUPPORTED[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
  for (uint32_t rate : SUPPORTED) {
    if (rate == baud) {
      return true;
    }
  }
  return false;
}

// Apply UART settings to the hardware at runtime. Returns true when the platform
// supports it (ESP8266/ESP32 via load_settings). LibreTiny's UARTComponent has no
// load_settings(), so runtime reconfiguration is unsupported there: the call only
// "succeeds" when the requested values already match the YAML-configured ones.
bool EybondCollector::reconfigure_uart_(uint32_t baud, uint8_t data_bits, uint8_t stop_bits,
                                        uart::UARTParityOptions parity) {
#if defined(USE_ESP8266) || defined(USE_ESP32)
  auto *u = this->parent_;
  bool changed = false;
  if (u->get_baud_rate() != baud) { u->set_baud_rate(baud); changed = true; }
  if (u->get_data_bits() != data_bits) { u->set_data_bits(data_bits); changed = true; }
  if (u->get_stop_bits() != stop_bits) { u->set_stop_bits(stop_bits); changed = true; }
  if (u->get_parity() != parity) { u->set_parity(parity); changed = true; }
  if (changed) {
    u->load_settings(false);
    ESP_LOGI(TAG, "Inverter UART re-configured to %s", this->uart_settings_string_().c_str());
  }
  return true;
#else
  auto *u = this->parent_;
  const bool already = u->get_baud_rate() == baud && u->get_data_bits() == data_bits &&
                       u->get_stop_bits() == stop_bits && u->get_parity() == parity;
  if (!already) {
    ESP_LOGW(TAG, "Runtime UART reconfiguration not supported on this platform; set it in YAML");
  }
  return already;
#endif
}

void EybondCollector::apply_uart(const std::string &value) {
  // AT+UART=<baud,data,stop,parity> (e.g. "9600,8,1,EVEN"). Apply the full framing.
  eybond::UartSettings s;
  if (!eybond::parse_uart_settings(value, &s) || !this->baud_supported_(s.baud)) {
    return;  // malformed/unsupported; the AT reply is already W000 (factory mirror)
  }
  const uart::UARTParityOptions parity = s.parity == eybond::UartParity::EVEN ? uart::UART_CONFIG_PARITY_EVEN
                                         : s.parity == eybond::UartParity::ODD ? uart::UART_CONFIG_PARITY_ODD
                                                                               : uart::UART_CONFIG_PARITY_NONE;
  if (!this->reconfigure_uart_(s.baud, s.data_bits, s.stop_bits, parity)) {
    return;
  }
  this->persist_uart_settings_();
  if (core_ != nullptr) {
    core_->update_uart_description(this->uart_settings_string_());
  }
  if (baud_listener_) {
    baud_listener_(s.baud);
  }
}

bool EybondCollector::apply_baud_rate(uint32_t baud) {
  if (!this->baud_supported_(baud)) {
    ESP_LOGW(TAG, "Rejected unsupported baud rate %u", static_cast<unsigned>(baud));
    return false;
  }
  auto *u = this->parent_;
  if (!this->reconfigure_uart_(baud, u->get_data_bits(), u->get_stop_bits(), u->get_parity())) {
    return false;
  }
  this->persist_uart_settings_();
  if (core_ != nullptr) {
    core_->update_uart_description(this->uart_settings_string_());
  }
  if (baud_listener_) {
    baud_listener_(baud);
  }
  return true;
}

uint32_t EybondCollector::current_baud_rate() const { return this->parent_->get_baud_rate(); }

const char *EybondCollector::current_uart_parity() const {
  switch (this->parent_->get_parity()) {
    case uart::UART_CONFIG_PARITY_EVEN:
      return "EVEN";
    case uart::UART_CONFIG_PARITY_ODD:
      return "ODD";
    default:
      return "NONE";
  }
}

std::string EybondCollector::current_direction_pin() const {
  if (flow_control_pin_ == nullptr) return "automatic / none";
  std::string summary = flow_control_pin_->dump_summary();
  if (flow_control_pin_->is_internal() && static_cast<InternalGPIOPin *>(flow_control_pin_)->is_inverted()) {
    summary += " (inverted)";
  }
  return summary;
}

std::string EybondCollector::current_status_led_pin() const {
  if (status_led_pin_ == nullptr) return "not configured";
  std::string summary = status_led_pin_->dump_summary();
  if (status_led_pin_->is_internal() && static_cast<InternalGPIOPin *>(status_led_pin_)->is_inverted()) {
    summary += " (inverted)";
  }
  return summary;
}

std::string EybondCollector::current_com_led_pin() const {
  if (com_led_pin_ != nullptr) {
    std::string summary = com_led_pin_->dump_summary();
    if (com_led_pin_->is_internal() && static_cast<InternalGPIOPin *>(com_led_pin_)->is_inverted()) {
      summary += " (inverted)";
    }
    return summary;
  }
  return status_led_pin_ != nullptr ? std::string("shared with status LED") : std::string("not configured");
}

bool EybondCollector::runtime_uart_configuration_supported() const {
#if defined(USE_ESP8266) || defined(USE_ESP32)
  return true;
#else
  return false;
#endif
}

std::string EybondCollector::current_server_host() const {
  return core_ != nullptr ? core_->server_host() : std::string();
}

uint16_t EybondCollector::current_server_port() const {
  return core_ != nullptr ? core_->server_port() : 0;
}

// Web writes are applied after the HTTP response has been queued, so changing
// the endpoint cannot cut off its own acknowledgement and a PN change can
// schedule a clean restart. While that short apply window is open, the accepted
// value is the truthful state for the form even though the hardware/core still
// exposes the previous value.
std::string EybondCollector::web_display_uart_settings() const {
  return web_uart_apply_requested_ ? web_staged_uart_ : this->uart_settings_string_();
}

std::string EybondCollector::web_display_collector_pn() const {
  return pn_apply_requested_ ? web_staged_pn_ : collector_pn_;
}

bool EybondCollector::web_display_uses_default_collector_pn() const {
  return pn_apply_requested_ ? web_staged_use_default_pn_ : using_default_pn_;
}

std::string EybondCollector::web_display_server_host() const {
  return endpoint_apply_requested_ ? staged_endpoint_host_ : this->current_server_host();
}

uint16_t EybondCollector::web_display_server_port() const {
  return endpoint_apply_requested_ ? staged_endpoint_port_ : this->current_server_port();
}

UartDiagnosticsSnapshot EybondCollector::uart_diagnostics_snapshot(uint32_t now) {
  LockGuard lock(uart_diagnostics_mutex_);
  return UartDiagnosticsSnapshot{
      uart_tx_bytes_,
      uart_rx_bytes_,
      uart_rx_response_bytes_,
      uart_rx_ignored_bytes_,
      uart_tx_operations_,
      has_uart_tx_activity_,
      has_uart_rx_activity_,
      now - last_uart_tx_ms_,
      now - last_uart_rx_ms_,
  };
}

bool EybondCollector::stage_web_settings(const std::string &host, uint16_t port, const std::string &uart_value,
                                         bool pn_config_present, bool use_default_pn, const std::string &pn_value,
                                         std::string *error) {
#ifdef USE_EYBOND_WEB_CONFIG
  if (!eybond::valid_server_endpoint(host, port)) {
    if (error != nullptr) *error = "Enter a valid host and port.";
    return false;
  }
  if (!uart_value.empty()) {
    if (!this->runtime_uart_configuration_supported()) {
      if (error != nullptr) *error = "Runtime UART changes are not supported on this platform.";
      return false;
    }
    eybond::UartSettings settings;
    if (!eybond::parse_uart_settings(uart_value, &settings) || !this->baud_supported_(settings.baud) ||
        settings.data_bits < 5 || settings.data_bits > 8 ||
        (settings.stop_bits != 1 && settings.stop_bits != 2)) {
      if (error != nullptr) *error = "Unsupported UART framing.";
      return false;
    }
  }
  std::string normalized_pn = pn_value;
  for (char &ch : normalized_pn) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  if (pn_config_present && !use_default_pn && !eybond::valid_collector_pn(normalized_pn)) {
    if (error != nullptr) *error = "PN must be one letter followed by 13 or 17 digits.";
    return false;
  }

  pending_endpoint_.clear();  // This explicit web update supersedes an uncommitted FC=3 write.
  staged_endpoint_host_ = host;
  staged_endpoint_port_ = port;
  endpoint_apply_requested_ = true;
  endpoint_apply_at_ms_ = millis() + 250;
  if (!uart_value.empty() && uart_value != this->uart_settings_string_()) {
    web_staged_uart_ = uart_value;
    web_uart_apply_requested_ = true;
  }
  const std::string target_pn = use_default_pn ? default_collector_pn_ : normalized_pn;
  if (pn_config_present && (target_pn != collector_pn_ || use_default_pn != using_default_pn_)) {
    web_staged_use_default_pn_ = use_default_pn;
    web_staged_pn_ = target_pn;
    pn_apply_requested_ = true;
    pn_apply_at_ms_ = millis() + 750;
  }
  return true;
#else
  (void) host;
  (void) port;
  (void) uart_value;
  (void) pn_config_present;
  (void) use_default_pn;
  (void) pn_value;
  if (error != nullptr) *error = "Captive web configuration is not enabled.";
  return false;
#endif
}

bool EybondCollector::stage_web_collector_identity(bool use_default_pn, const std::string &pn_value,
                                                   std::string *error) {
#ifdef USE_EYBOND_WEB_CONFIG
  std::string normalized_pn = pn_value;
  for (char &ch : normalized_pn) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  if (!use_default_pn && !eybond::valid_collector_pn(normalized_pn)) {
    if (error != nullptr) *error = "PN must be one letter followed by 13 or 17 digits.";
    return false;
  }
  const std::string target_pn = use_default_pn ? default_collector_pn_ : normalized_pn;
  if (target_pn != collector_pn_ || use_default_pn != using_default_pn_) {
    web_staged_use_default_pn_ = use_default_pn;
    web_staged_pn_ = target_pn;
    pn_apply_requested_ = true;
    pn_apply_at_ms_ = millis() + 750;
  }
  return true;
#else
  (void) use_default_pn;
  (void) pn_value;
  if (error != nullptr) *error = "Captive web configuration is not enabled.";
  return false;
#endif
}

bool EybondCollector::stage_web_endpoint(const std::string &host, uint16_t port, std::string *error) {
#ifdef USE_EYBOND_WEB_CONFIG
  if (!eybond::valid_server_endpoint(host, port)) {
    if (error != nullptr) *error = "Enter a valid host and port.";
    return false;
  }
  if (host == this->current_server_host() && port == this->current_server_port() &&
      !eybond::endpoint_apply_pending(pending_endpoint_, endpoint_apply_requested_)) return true;
  pending_endpoint_.clear();
  staged_endpoint_host_ = host;
  staged_endpoint_port_ = port;
  endpoint_apply_requested_ = true;
  endpoint_apply_at_ms_ = millis() + 250;
  return true;
#else
  (void) host;
  (void) port;
  if (error != nullptr) *error = "Captive web configuration is not enabled.";
  return false;
#endif
}

bool EybondCollector::stage_web_uart(const std::string &uart_value, std::string *error) {
#ifdef USE_EYBOND_WEB_CONFIG
  if (!this->runtime_uart_configuration_supported()) {
    if (error != nullptr) *error = "Runtime UART changes are not supported on this platform.";
    return false;
  }
  eybond::UartSettings settings;
  if (!eybond::parse_uart_settings(uart_value, &settings) || !this->baud_supported_(settings.baud) ||
      settings.data_bits < 5 || settings.data_bits > 8 ||
      (settings.stop_bits != 1 && settings.stop_bits != 2)) {
    if (error != nullptr) *error = "Unsupported UART framing.";
    return false;
  }
  if (uart_value != this->uart_settings_string_()) {
    web_staged_uart_ = uart_value;
    web_uart_apply_requested_ = true;
  }
  return true;
#else
  (void) uart_value;
  if (error != nullptr) *error = "Captive web configuration is not enabled.";
  return false;
#endif
}

void EybondCollector::restore_persisted_uart_settings_() {
  auto *u = this->parent_;
  uint32_t baud = 0;
  uint8_t data_bits = u->get_data_bits();
  uint8_t stop_bits = u->get_stop_bits();
  uart::UARTParityOptions parity = u->get_parity();
  bool complete = false;

  PersistedUartSettings saved{};
  if (uart_pref_.load(&saved) && saved.version == UART_PREF_VERSION && this->baud_supported_(saved.baud) &&
      saved.data_bits >= 5 && saved.data_bits <= 8 && (saved.stop_bits == 1 || saved.stop_bits == 2) &&
      saved.parity <= static_cast<uint8_t>(uart::UART_CONFIG_PARITY_ODD)) {
    baud = saved.baud;
    data_bits = saved.data_bits;
    stop_bits = saved.stop_bits;
    parity = static_cast<uart::UARTParityOptions>(saved.parity);
    complete = true;
  } else {
    // Migrate the legacy baud-only preference while
    // retaining framing from the YAML build.
    uint32_t legacy_baud = 0;
    if (baud_pref_.load(&legacy_baud) && this->baud_supported_(legacy_baud)) {
      baud = legacy_baud;
    }
  }

  if (baud == 0) return;
  if (!this->reconfigure_uart_(baud, data_bits, stop_bits, parity)) {
    ESP_LOGW(TAG, "Persisted inverter UART settings cannot be applied on this platform");
    return;
  }
  ESP_LOGI(TAG, "Restored inverter UART settings %s", this->uart_settings_string_().c_str());
  if (!complete) this->persist_uart_settings_();
}

void EybondCollector::persist_uart_settings_() {
  PersistedUartSettings saved{};
  saved.baud = this->parent_->get_baud_rate();
  saved.version = UART_PREF_VERSION;
  saved.data_bits = this->parent_->get_data_bits();
  saved.stop_bits = this->parent_->get_stop_bits();
  saved.parity = static_cast<uint8_t>(this->parent_->get_parity());
  const bool full_ok = uart_pref_.save(&saved);
  const uint32_t legacy_baud = saved.baud;
  const bool legacy_ok = baud_pref_.save(&legacy_baud);
  if (!full_ok || !legacy_ok) {
    ESP_LOGW(TAG, "Failed to persist complete inverter UART settings");
  }
  global_preferences->sync();
}

void EybondCollector::restore_persisted_collector_pn_() {
  PersistedCollectorPn saved{};
  if (!pn_pref_.load(&saved) || saved.version != PN_PREF_VERSION) return;
  saved.pn[sizeof(saved.pn) - 1] = '\0';
  if (saved.use_default != 0) {
    collector_pn_ = default_collector_pn_;
    using_default_pn_ = true;
    return;
  }
  const std::string custom_pn(saved.pn);
  if (!eybond::valid_collector_pn(custom_pn)) {
    ESP_LOGW(TAG, "Ignoring invalid persisted collector PN");
    return;
  }
  collector_pn_ = custom_pn;
  using_default_pn_ = false;
  ESP_LOGI(TAG, "Restored custom collector PN %s", collector_pn_.c_str());
}

bool EybondCollector::persist_collector_pn_(bool use_default, const std::string &pn) {
  PersistedCollectorPn saved{};
  saved.version = PN_PREF_VERSION;
  saved.use_default = use_default ? 1 : 0;
  if (!use_default) std::snprintf(saved.pn, sizeof(saved.pn), "%s", pn.c_str());
  if (!pn_pref_.save(&saved)) {
    ESP_LOGW(TAG, "Failed to persist collector PN selection");
    return false;
  }
  global_preferences->sync();
  return true;
}

void EybondCollector::process_endpoint_apply_(uint32_t now) {
  if (!endpoint_apply_requested_ || static_cast<int32_t>(now - endpoint_apply_at_ms_) < 0) {
    return;
  }
  endpoint_apply_requested_ = false;
  if (core_ != nullptr) {
    ESP_LOGI(TAG, "Applying staged server endpoint %s:%u", staged_endpoint_host_.c_str(),
             static_cast<unsigned>(staged_endpoint_port_));
    core_->set_server_endpoint(staged_endpoint_host_, staged_endpoint_port_, now);  // persists via callback
  }
  staged_endpoint_host_.clear();
  staged_endpoint_port_ = 0;
}

void EybondCollector::process_web_uart_apply_() {
  if (!web_uart_apply_requested_) {
    return;
  }
  web_uart_apply_requested_ = false;
  const std::string uart = web_staged_uart_;
  web_staged_uart_.clear();
  this->apply_uart(uart);
}

void EybondCollector::process_pn_apply_(uint32_t now) {
  if (!pn_apply_requested_ || static_cast<int32_t>(now - pn_apply_at_ms_) < 0) return;
  pn_apply_requested_ = false;
  const bool persisted = this->persist_collector_pn_(web_staged_use_default_pn_, web_staged_pn_);
  web_staged_pn_.clear();
  if (persisted) {
    ESP_LOGI(TAG, "Collector PN selection changed; scheduling restart");
    this->request_reboot_(now);
  }
}

void EybondCollector::process_wifi_apply_(uint32_t now) {
#ifdef USE_WIFI
  if (!wifi_apply_requested_ || static_cast<int32_t>(now - wifi_apply_at_ms_) < 0) {
    return;
  }
  wifi_apply_requested_ = false;
  ESP_LOGI(TAG, "Applying new WiFi credentials (ssid length %u)",
           static_cast<unsigned>(pending_wifi_ssid_.size()));
  wifi::global_wifi_component->save_wifi_sta(pending_wifi_ssid_, pending_wifi_password_);
  pending_wifi_ssid_.clear();
  pending_wifi_password_.clear();
#else
  (void) now;
#endif
}

void EybondCollector::request_reboot_(uint32_t now) {
  reboot_requested_ = true;
  reboot_at_ms_ = now + 1500;
  ESP_LOGI(TAG, "Collector restart requested; rebooting after response flush");
}

void EybondCollector::process_reboot_(uint32_t now) {
  if (!reboot_requested_ || static_cast<int32_t>(now - reboot_at_ms_) < 0) {
    return;
  }
  reboot_requested_ = false;
  ESP_LOGI(TAG, "Restarting collector");
  ESP.restart();
}

void EybondCollector::poll_wifi_scan_(uint32_t now) {
  (void) now;
  if (!wifi_scan_running_) {
    return;
  }
  const int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) {
    return;
  }
  wifi_scan_running_ = false;
  if (count <= 0) {
    return;  // failed or empty: keep whatever cache we had
  }
  std::vector<eybond::BleWifiNetwork> entries;
  std::string cache;
  int emitted = 0;
  for (int i = 0; i < count && emitted < 10; i++) {
    const std::string ssid = WiFi.SSID(i).c_str();
    if (ssid.empty() || ssid.find_first_of(",[]") != std::string::npos) {
      continue;  // would break the [ssid,rssi] framing
    }
    const int rssi = static_cast<int>(WiFi.RSSI(i));
    entries.push_back(eybond::BleWifiNetwork{ssid, rssi});
    char entry[64];
    std::snprintf(entry, sizeof(entry), "[%s,%d]", ssid.c_str(), rssi);
    cache += entry;
    emitted++;
  }
  WiFi.scanDelete();
  if (!entries.empty()) {
    wifi_scan_entries_ = entries;
  }
  if (!cache.empty()) {
    wifi_scan_cache_ = cache;
  }
  ESP_LOGD(TAG, "WiFi scan finished: %d networks, %d cached", count, emitted);
}

#ifdef USE_EYBOND_BLE
void EybondCollector::setup_ble_() {
  using namespace esp32_ble_server;
  if (global_ble_server == nullptr) {
    return;  // esp32_ble_server not up yet; retry next loop
  }

  ble_ = std::unique_ptr<eybond::BleProvisioning>(new eybond::BleProvisioning(this));

  // Raise the local ATT MTU so a client that negotiates up (HA host BlueZ, an
  // ESP32 BT proxy) isn't capped at the 23-byte default — short responses fit
  // either way, but this gives headroom for the Wi-Fi scan list.
  esp_ble_gatt_set_local_mtu(512);

  // Marginal-link robustness: the board's advertisements and notify responses
  // must reach the Home Assistant Bluetooth adapter, which may be several metres
  // away. ESPHome leaves BLE TX power at the IDF default (~+3 dBm); raise it to
  // the maximum it exposes (+9 dBm) for both advertising and connections so a
  // distant host adapter can still complete the GATT exchange.
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);

  // Advertise the PN in manufacturer data so the integration's BLE scan can
  // discover the bridge. set_manufacturer_data() stores it on the server and
  // re-applies it on every advertising restart (a direct esp32_ble call would
  // be clobbered by the server's own restart_advertising_).
  global_ble_server->set_manufacturer_data(eybond::build_ble_manufacturer_data(ble_pn_));

  // PROVISION layout (smartess_ble.PROVISION_LAYOUT): the 0x1827 service with a
  // write characteristic (0x2ADB) and a notify characteristic (0x2ADC). The
  // integration picks this layout from the discovered GATT services and then
  // uses plain notify-based exchanges.
  BLEService *svc = global_ble_server->create_service(
      esp32_ble::ESPBTUUID::from_raw("00001827-0000-1000-8000-00805f9b34fb"), /*advertise=*/false);
  if (svc == nullptr) {
    ESP_LOGE(TAG, "BLE provisioning: service create failed");
    return;
  }
  BLECharacteristic *write_char = svc->create_characteristic(
      "00002adb-0000-1000-8000-00805f9b34fb",
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  ble_notify_char_ = svc->create_characteristic("00002adc-0000-1000-8000-00805f9b34fb",
                                                BLECharacteristic::PROPERTY_NOTIFY |
                                                    BLECharacteristic::PROPERTY_READ);
  // Notifications are only delivered to clients that subscribed via the CCCD,
  // so the notify characteristic must carry a 0x2902 descriptor.
  ble_notify_char_->add_descriptor(new BLE2902());  // NOLINT(cppcoreguidelines-owning-memory)

  write_char->EventEmitter<BLECharacteristicEvt::VectorEvt, std::vector<uint8_t>, uint16_t>::on(
      BLECharacteristicEvt::VectorEvt::ON_WRITE,
      [this](const std::vector<uint8_t> &data, uint16_t /*conn_id*/) { this->ble_feed_(data); });

  global_ble_server->enqueue_start_service(svc);
  ESP_LOGI(TAG, "BLE provisioning service ready (advertising PN %s)", ble_pn_.c_str());
}

void EybondCollector::ble_feed_(const std::vector<uint8_t> &data) {
  ble_rx_.append(reinterpret_cast<const char *>(data.data()), data.size());
  ble_rx_last_ms_ = millis();
  ble_rx_pending_ = true;
}

void EybondCollector::process_ble_(uint32_t now) {
  if (!ble_rx_pending_ || ble_ == nullptr) {
    return;
  }
  // Coalesce ATT fragments: the client waits for our notify before sending the
  // next command, so a brief quiet period reliably frames exactly one command.
  if (static_cast<int32_t>(now - ble_rx_last_ms_) < static_cast<int32_t>(BLE_RX_SETTLE_MS)) {
    return;
  }
  ble_rx_pending_ = false;
  const std::string command = ble_rx_;
  ble_rx_.clear();

  const std::string response = ble_->handle_command(command);
  if (response.empty() || ble_notify_char_ == nullptr) {
    return;
  }
  ble_notify_char_->set_value(response + "\r\n");
  ble_notify_char_->notify();
  // Log only the response (status codes); the command may carry a Wi-Fi password.
  ESP_LOGD(TAG, "BLE provisioning -> %s", response.c_str());

  // Now that the response is on the air, refresh the Wi-Fi scan cache if a scan
  // was requested. Deferring it here keeps the radio idle during the notify, so
  // the response is never lost to BLE/Wi-Fi coexistence on a weak link.
  if (ble_scan_pending_ && !wifi_scan_running_) {
    ble_scan_pending_ = false;
    WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
    wifi_scan_running_ = true;
    ble_last_scan_ms_ = (now == 0) ? 1 : now;
  }
}

void EybondCollector::ble_apply_wifi(const std::string &ssid, const std::string &password) {
#ifdef USE_WIFI
  pending_wifi_ssid_ = ssid;
  pending_wifi_password_ = password;
  wifi_apply_requested_ = true;
  // The BLE notify is independent of the STA link, so there is no in-band
  // response to protect (unlike the FC=3 TCP path): apply almost immediately.
  wifi_apply_at_ms_ = millis() + 200;
  ESP_LOGI(TAG, "BLE provisioning: staged WiFi credentials (ssid length %u)",
           static_cast<unsigned>(ssid.size()));
#else
  (void) ssid;
  (void) password;
#endif
}

bool EybondCollector::ble_wifi_connected() {
#ifdef USE_WIFI
  return wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected();
#else
  return false;
#endif
}

std::vector<eybond::BleWifiNetwork> EybondCollector::ble_wifi_scan() {
  // IMPORTANT: do NOT start a Wi-Fi scan here. Scanning the radio contends with
  // BLE under coexistence and, on a weak BLE link, drops the very notification
  // that carries this response — the integration then reports "could not read
  // the Wi-Fi list / weak connection". Answer instantly from the cache plus the
  // network we are on, and schedule a refresh to run AFTER the notify (see
  // process_ble_), throttled so repeated refreshes don't keep hammering the radio.
  std::vector<eybond::BleWifiNetwork> out = wifi_scan_entries_;
  const std::string ssid = WiFi.SSID().c_str();
  if (!ssid.empty() && ssid.find_first_of(",[]") == std::string::npos) {
    bool present = false;
    for (const auto &n : out) {
      if (n.ssid == ssid) {
        present = true;
        break;
      }
    }
    if (!present) {
      out.insert(out.begin(), eybond::BleWifiNetwork{ssid, std::atoi(this->wifi_rssi().c_str())});
    }
  }
  const uint32_t now = millis();
  if (!wifi_scan_running_ && (ble_last_scan_ms_ == 0 || static_cast<int32_t>(now - ble_last_scan_ms_) > 20000)) {
    ble_scan_pending_ = true;  // refreshed after we notify, for the next request
  }
  return out;
}
#endif  // USE_EYBOND_BLE

void EybondCollector::process_pending_connect_() {
  if (!connect_pending_) {
    return;
  }
  connect_pending_ = false;
  ESP_LOGI(TAG, "Connecting to %s:%u", pending_host_.c_str(), pending_port_);
  tcp_.stop();
  if (tcp_.connect(pending_host_.c_str(), pending_port_)) {
#ifndef USE_LIBRETINY
    tcp_.setNoDelay(true);  // LibreTiny's WiFiClient (LwIPClient) has no setNoDelay
#endif
    tcp_up_ = true;
    ESP_LOGI(TAG, "Connected to %s:%u", pending_host_.c_str(), pending_port_);
    core_->on_tcp_connected(millis());
  } else {
    tcp_up_ = false;
    ESP_LOGW(TAG, "Connect to %s:%u failed", pending_host_.c_str(), pending_port_);
    core_->on_tcp_connect_failed(millis());
  }
}

void EybondCollector::udp_reply(const uint8_t *data, size_t len) {
  udp_.beginPacket(udp_.remoteIP(), udp_.remotePort());
  udp_.write(data, len);
  udp_.endPacket();
}

void EybondCollector::tcp_connect(const std::string &host, uint16_t port) {
  connect_pending_ = true;
  pending_host_ = host;
  pending_port_ = port;
}

void EybondCollector::tcp_send(const uint8_t *data, size_t len) {
  if (tcp_up_) {
    tcp_.write(data, len);
  }
}

void EybondCollector::tcp_close() {
  tcp_.stop();
  tcp_up_ = false;
}

void EybondCollector::uart_send(const uint8_t *data, size_t len) {
  this->note_activity_(millis());
  // Drop stale inverter bytes so they cannot masquerade as the new response.
  uint32_t stale_rx_bytes = 0;
  uint32_t stale_rx_last_at = 0;
  while (this->available() > 0) {
    uint8_t discard;
    if (!this->read_byte(&discard)) {
      break;
    }
    stale_rx_bytes++;
    stale_rx_last_at = millis();
  }
  if (flow_control_pin_ != nullptr) {
    flow_control_pin_->digital_write(true);
  }
  this->write_array(data, len);
  this->flush();
  {
    LockGuard lock(uart_diagnostics_mutex_);
    if (stale_rx_bytes > 0) {
      uart_rx_bytes_ += stale_rx_bytes;
      uart_rx_ignored_bytes_ += stale_rx_bytes;
      last_uart_rx_ms_ = stale_rx_last_at;
      has_uart_rx_activity_ = true;
    }
    uart_tx_bytes_ += len;
    uart_tx_operations_++;
    last_uart_tx_ms_ = millis();
    has_uart_tx_activity_ = true;
  }
  if (flow_control_pin_ != nullptr) {
    flow_control_pin_->digital_write(false);
  }
}

void EybondCollector::note_activity_(uint32_t now) {
  // Armed only by inverter UART traffic (forward TX + inverter reply), never by
  // HA<->bridge TCP chatter, so the com indicator means real inverter comms.
  last_activity_ms_ = now;
}

// Two logical indicators, like the factory collector's STATUS and COM LEDs:
//   * STATUS = connection state: fast blink (no Wi-Fi), slow blink (Wi-Fi but no
//     bridge), solid (bridge connected).
//   * COM = inverter communication: flickers while data flows on the UART, dark
//     when idle. It TOGGLES (capped at COM_FLICKER_MS) rather than holding lit, so
//     heavy load reads as a fast flicker and never as a solid glow.
// Two pins -> each drives its own LED. Only status_led_pin (single-LED dev boards)
// -> the status LED also carries com: solid when connected+idle, flickering while
// inverter data flows. Only com_led_pin -> a pure activity light.
void EybondCollector::update_leds_(uint32_t now) {
  const bool wifi_up = network::is_connected();
  const bool linked = wifi_up && tcp_up_;
  const bool active = linked && (now - last_activity_ms_) < COM_HOLD_MS;

  const bool have_status = status_led_pin_ != nullptr;
  const bool have_com = com_led_pin_ != nullptr;
  if (!have_status && !have_com) {
    return;
  }

  // COM flicker phase.
  if (active) {
    if (now - com_last_toggle_ms_ >= COM_FLICKER_MS) {
      com_phase_ = !com_phase_;
      com_last_toggle_ms_ = now;
    }
  } else {
    com_phase_ = false;
  }
  if (have_com) {
    this->write_led_(com_led_pin_, &com_led_state_, com_phase_);
  }

  if (!have_status) {
    return;
  }

  // STATUS LED (also carries com when it is the only LED).
  bool target;
  if (!wifi_up) {
    if (now - status_led_last_toggle_ms_ >= STATUS_FAST_BLINK_MS) {
      status_led_phase_ = !status_led_phase_;
      status_led_last_toggle_ms_ = now;
    }
    target = status_led_phase_;
  } else if (!linked) {
    if (now - status_led_last_toggle_ms_ >= STATUS_SLOW_BLINK_MS) {
      status_led_phase_ = !status_led_phase_;
      status_led_last_toggle_ms_ = now;
    }
    target = status_led_phase_;
  } else if (have_com) {
    target = true;  // dedicated com LED exists -> status LED is a pure link light
  } else {
    target = active ? com_phase_ : true;  // single LED: solid when idle, flicker on traffic
    status_led_phase_ = target;
  }
  this->write_led_(status_led_pin_, &status_led_state_, target);
}

void EybondCollector::write_led_(GPIOPin *pin, bool *state, bool on) {
  if (pin == nullptr || *state == on) {
    return;
  }
  pin->digital_write(on);
  *state = on;
}

std::string EybondCollector::wifi_rssi() {
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%d", wifi::global_wifi_component->wifi_rssi());
    return buffer;
  }
#endif
  return "-50";
}

void EybondCollector::configure_fallback_ap_(const std::string &pn) {
#ifdef USE_WIFI
  if (wifi::global_wifi_component == nullptr) {
    return;
  }

  auto ap = wifi::global_wifi_component->get_ap();
  const std::string current = ap.get_ssid();
  if (!current.empty()) {
    return;  // Respect a user-provided fallback AP name.
  }

  std::string ssid = pn;

  ap.set_ssid(ssid);
  wifi::global_wifi_component->set_ap(ap);
  ESP_LOGI(TAG, "Fallback setup AP SSID: %s", ssid.c_str());
#else
  (void) pn;
#endif
}

void EybondCollector::log(const char *message) { ESP_LOGD(TAG, "%s", message); }

std::string EybondCollector::uart_settings_string_() const {
  const auto *parent = this->parent_;
  const char *parity = "NONE";
  switch (parent->get_parity()) {
    case uart::UART_CONFIG_PARITY_EVEN:
      parity = "EVEN";
      break;
    case uart::UART_CONFIG_PARITY_ODD:
      parity = "ODD";
      break;
    default:
      break;
  }
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%" PRIu32 ",%u,%u,%s", parent->get_baud_rate(),
                parent->get_data_bits(), parent->get_stop_bits(), parity);
  return buffer;
}

void EybondCollector::dump_config() {
  ESP_LOGCONFIG(TAG, "Eybond Collector bridge:");
  if (core_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  UDP discovery port: %u", udp_port_);
    ESP_LOGCONFIG(TAG, "  Heartbeat interval: %" PRIu32 " ms", core_config_.heartbeat_interval_ms);
    ESP_LOGCONFIG(TAG, "  Response gap/timeout: %" PRIu32 "/%" PRIu32 " ms", core_config_.uart_gap_ms,
                  core_config_.uart_timeout_ms);
    ESP_LOGCONFIG(TAG, "  Command spacing: %" PRIu32 " ms", core_config_.command_spacing_ms);
    ESP_LOGCONFIG(TAG, "  UART: %s", uart_settings_string_().c_str());
  }
  if (!static_server_host_.empty()) {
    ESP_LOGCONFIG(TAG, "  Static server: %s:%u", static_server_host_.c_str(), static_server_port_);
  }
  ESP_LOGCONFIG(TAG, "  Status LED: %s", status_led_pin_ == nullptr ? "disabled" : "enabled");
}

}  // namespace eybond_collector
}  // namespace esphome

#endif  // USE_ARDUINO
