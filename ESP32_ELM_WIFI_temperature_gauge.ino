#include <WiFi.h>
#include <TFT_eSPI.h> // Библиотека для дисплея

// ***** Настройки Wi-Fi и ELM327 *****
const char* ssid = "V-LINK";    // Название точки доступа (большими буквами)
const char* password = "";      // Нет пароля
const char* ELM_HOST = "192.168.0.10";
const int   ELM_PORT = 35000;

// Глобальный WiFi-клиент для TCP-соединения
WiFiClient elmClient;

// Глобальные переменные для ELM327
bool   elmInitialized = false;
String responseBuffer;         // Буфер для накопления ответов ELM327
int    engineTemp = 0;         // Температура охлаждающей жидкости, полученная от ELM327

// ***** Графика и дисплей *****
TFT_eSPI tft = TFT_eSPI();      // Объект дисплея

// Подключите свои массивы изображений:
#include "images.h"         // epd_bitmap_allArray[...] — фон
#include "images_digits.h"  // bitmaps_digits[...]       — цифры
#include "images_needle.h"  // bitmaps_needle[...]       — стрелки

// Параметры для вычислений и отрисовки температуры
float temperature_sensor;
float temperature_interpolated = 0;
int   value_temp_digits = 0;
int   needle_image;
int   gauge_min_value = 0;
int   gauge_max_value = 100;

// Периодический опрос ELM327 (10 секунд)
unsigned long lastElmRequest = 0;
unsigned long elmRequestInterval = 10000; // 10 секунд

// ------------------------------------------------------------------
// Функция инициализации ELM327 через TCP-соединение
// ------------------------------------------------------------------
bool initializeELM() {
  // Подключаемся к точке доступа
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Ждём подключения
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  // Подключаемся к ELM327 по TCP
  if (!elmClient.connect(ELM_HOST, ELM_PORT)) {
    return false;
  }

  // Выполняем последовательность инициализации
  elmClient.print("ATZ\r\n");   // Сброс
  delay(2000);

  // (ATI пропускаем)

  elmClient.print("ATE0\r\n");  // Отключаем эхо
  delay(2000);

  elmClient.print("ATL0\r\n");  // Отключаем «Linefeed»/пробелы
  delay(2000);

  // Выбираем протокол ISO 14230-4 KWP (5 baud init, 10.4 Kbaud)
  elmClient.print("ATSP4\r\n");
  delay(2000);

  // Опционально запрашиваем текущий протокол (описание)
  elmClient.print("ATDP\r\n");
  delay(2000);

  elmInitialized = true;
  return true;
}

// ------------------------------------------------------------------
// Функция чтения данных из TCP и парсинга ответов с символом '>'
// ------------------------------------------------------------------
void readAndParseELMData() {
  // Считываем доступные байты из сокета
  while (elmClient.available() > 0) {
    char c = (char)elmClient.read();
    responseBuffer += c;
  }

  // Ищем в накопленном буфере символ '>'
  int promptIndex = responseBuffer.indexOf('>');
  if (promptIndex != -1) {
    // Выделяем строку до '>'
    String fullResponse = responseBuffer.substring(0, promptIndex);
    fullResponse.trim();
    // Обрезаем буфер после '>'
    responseBuffer = responseBuffer.substring(promptIndex + 1);

    // Ищем "41 05" для определения температуры ОЖ
    if (fullResponse.indexOf("41 05") != -1) {
      int startIndex = fullResponse.indexOf("41 05");
      if (startIndex != -1) {
        String line = fullResponse.substring(startIndex);
        int firstSpace  = line.indexOf(' ');
        int secondSpace = line.indexOf(' ', firstSpace + 1);
        int thirdSpace  = line.indexOf(' ', secondSpace + 1);

        String hexValue;
        if (thirdSpace == -1) {
          hexValue = line.substring(secondSpace + 1);
        } else {
          hexValue = line.substring(secondSpace + 1, thirdSpace);
        }
        hexValue.trim();

        // Перевод из HEX в int и расчёт температуры
        int rawVal = (int) strtol(hexValue.c_str(), NULL, 16);
        engineTemp = rawVal - 40;  // по формуле: Температура = A - 40
      }
    }
  }
}

// ------------------------------------------------------------------
// Стандартные функции setup() и loop()
// ------------------------------------------------------------------
void setup() {
  // Если не нужен Serial — можете убрать или закомментировать:
  Serial.begin(115200);

  // Инициализация дисплея
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);   // Заливка экрана цветом, при желании поменяйте
  tft.setTextFont(4);
  tft.setSwapBytes(true);

  // Если есть фоновая картинка 240x240, выводим:
  tft.pushImage(0, 0, 240, 240, epd_bitmap_allArray[0]);

  // Инициализируем ELM327 по Wi-Fi (TCP)
  initializeELM();
}

void loop() {
  // Если ELM327 инициализирован и соединение ещё активно
  if (elmInitialized && elmClient.connected()) {
    // Читаем приходящие данные и разбираем ответы
    readAndParseELMData();

    // Периодически отправляем запрос на температуру (PID: 0105)
    unsigned long now = millis();
    if (now - lastElmRequest > elmRequestInterval) {
      lastElmRequest = now;
      responseBuffer = "";
      elmClient.print("0105\r\n");
    }

    // Обновляем показания температуры
    temperature_sensor = engineTemp;
    temperature_interpolated += (temperature_sensor - temperature_interpolated) / 5.0;

    value_temp_digits = round(temperature_interpolated);
    value_temp_digits = constrain(value_temp_digits, 0, 999);

    // Отрисовка цифр температуры
    if (value_temp_digits < 10) {
      tft.pushImage(66, 181, 36, 44, bitmaps_digits[10]);
      tft.pushImage(102, 181, 36, 44, bitmaps_digits[value_temp_digits]);
      tft.pushImage(138, 181, 36, 44, bitmaps_digits[10]);
    } else if (value_temp_digits < 100) {
      tft.pushImage(66, 181, 18, 44, bitmaps_digits[10]);
      tft.pushImage(84, 181, 36, 44, bitmaps_digits[(value_temp_digits % 100) / 10]);
      tft.pushImage(120, 181, 36, 44, bitmaps_digits[value_temp_digits % 10]);
      tft.pushImage(156, 181, 18, 44, bitmaps_digits[10]);
    } else {
      tft.pushImage(66, 181, 36, 44, bitmaps_digits[value_temp_digits / 100]);
      tft.pushImage(102, 181, 36, 44, bitmaps_digits[(value_temp_digits % 100) / 10]);
      tft.pushImage(138, 181, 36, 44, bitmaps_digits[value_temp_digits % 10]);
    }

    // Отрисовка стрелки
    needle_image = map(
      temperature_interpolated * 10.0,
      gauge_min_value * 10.0,
      gauge_max_value * 10.0,
      0, 1200
    );
    needle_image = round(needle_image / 10.0);
    needle_image = constrain(needle_image, 0, 120);
    tft.pushImage(11, 11, 218, 170, bitmaps_needle[needle_image]);

  } else {
    // При желании можно повторять попытку подключения или задержку
    delay(2000);
  }
}
