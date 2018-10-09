//Охранная сигнализация на 2 шлейфа
// - управление с 2-х кнопочного пульта: включение двумя кнопками одновременно, отмена включения в течение 20 сек любой кнопкой
// - ввод секретного кода после 6 сек удержания обеих кнопок (5-ВВОД КОДА), окончание ввода -> пауза более 5 сек
// - при сработке сигнал сирены 60 сек, затем попытка перехода в режим 2-ОХРАНА
// - отключение сирены секретным кодом в режиме задержки тревоги 30 сек (3-ЗАДЕРЖКА) или при работе сирены (4-ТРЕВОГА)

#include <Button.h>
#include <MsTimer2.h>
#include <avr/wdt.h>
#include <EEPROM.h>

//Назначение выводов
#define BUTTON_1_PIN 9 //вывод для кнопки №1 пульта
#define BUTTON_2_PIN 10 //вывод для кнопки №2 пульта
#define LED_PIN 11 //вывод для светодиода и внешней индикаци
#define SIREN_PIN 12 //вывод для сирены
#define SHLEIF_1_PIN A0 //вывод для шлейфа №1
#define SHLEIF_2_PIN A1 //вывод для шлейфа №2

//Установка параметров
#define SET_COD_TIME 3000 //время удержания кнопок для перехода на режим установки кода (*2 мс = 6 сек)
#define FAST_TIME_FLASH_LED 50 //период частого мигания светодиода (*2 мс = 0,1 сек)
#define SLOW_TIME_FLASH_LED 250 //период редкого мигания светодиода (*2 мс = 0,5 сек)
#define TIME_SET_GUARD 10000 //время постановки на охрану (*2 мс = 20 сек)
#define TIME_BLOCK 15000 //время задержки сирены на отключение вводом секретного кода (*2 мс = 30 сек)
#define TIME_ALARM 30000 //время на звучание сирены (*2 мс = 60 сек)
#define TIME_SET_COD 2500 //время паузы между вводом цифр кода (*2 мс = 5 сек)
#define MAX_U 36864 //верхний предел напряжения шлейфов (3,6В, 3,6*1024/5*50=36864)
#define MIN_U 10650 //нижний предел напряжения шлейфов (1,04В, 1,04*1024/5*50=10650)

//Адреса EEPROM
#define COD_ADR 2 //адрес секретного кода в EEPROM
#define NUMBER_ADR 4 //адрес числа битов секретного кода в EEPROM

//Объявление переменных
volatile unsigned int sumShleif1, sumShleif2; //переменные для суммирования кодов АЦП
volatile unsigned int averageShleif1, averageShleif2; //суммы кодов АЦП (среднее напряжение шлейфов * 50)
volatile int averageCount; //Счетчик для усреднения кодов АЦП (напряжения шлейфов)
volatile int serialCount; //Счетчик времени передачи отладочных данных через UART
byte mode; //режим работы сигнализации
boolean flagTwoButtons; //признак нажатия двух кнопок
volatile unsigned int commonTimer; //счетчик общего таймера для разных целей
volatile unsigned int ledFlashCount; //счетчик для организации мигания светодиода
byte secretCode; //переменная для секретного кода
byte bitNum; //переменная для побитной обработки секретного кода (размер кода в битах=число нажатий кнопок при вводе кода)

//Создание объектов
volatile Button button1(BUTTON_1_PIN, 25); //создание объекта для кнопки пульта №1 (сканирование дребезга *2=50 мс)
volatile Button button2(BUTTON_2_PIN, 25); //создание объекта для кнопки пульта №2 (сканирование дребезга *2=50 мс)

void setup() {
  pinMode(LED_PIN, OUTPUT); //определяем режим выхода для светодиода
  pinMode(SIREN_PIN, OUTPUT); //определяем режим выхода для сирены
  MsTimer2::set(2, timerInterupt); //устанавливаем аппаратный таймер прерываний на 2 мс
  MsTimer2::start(); //разрешение прерываний по таймеру
  Serial.begin(9600); //инициализируем порт для отладки, скорость 9600 бод
  wdt_enable(WDTO_15MS); //разрешаем работу сторожевого таймера AVR wdt с тайм-аутом 15 мс
  flagTwoButtons = false;
}

void loop() {

  //----------Режим 0 = ОТКЛЮЧЕНО----------
  // - светодиод не горит
  // - сирена отключена
  // - если нажаты две кнопки пульта одновременно перейти в режим 1 (ПОСТАНОВКА НА ОХРАНУ)
  // - если нажаты две кнопки пульта одновременно более, чем 6 сек перейти в режим 5 (УСТАНОВКА СЕКРЕТНОГО КОДА)

  if (mode == 0) {
    digitalWrite(LED_PIN, LOW); //светодиод не горит
    digitalWrite(SIREN_PIN, LOW); //сирена отключена

    //Определение команды постановки на охрану при коротком нажатии двух кнопок пульта
    //признак flagTwoButtons устанавливается при нажатии кнопок №1 и №2
    if ((button1.flagPress == true) && (button2.flagPress == true)) flagTwoButtons = true;
    //переход к постановке на охрану (режим 1), если обе кнопки были нажаты, а затем отжаты
    if ((flagTwoButtons == true) && (button1.flagPress == false) && (button2.flagPress == false)) {
      commonTimer = 0;
      button1.flagClick = false;
      button2.flagClick = false;
      mode = 1;
    }

    //Определение команды установки секретного кода при долгом нажатии двух кнопок пульта
    //если не нажата хотя бы одна кнопка пульта, то обнуляется общий таймер commonTimer
    if ((button1.flagPress == false) || (button2.flagPress == false))commonTimer = 0;
    //переход к установке секретного кода (режим 5) после 6 сек удержания обеих кнопок пульта в нажатом состоянии
    if (commonTimer > SET_COD_TIME) {
      commonTimer = 0;
      button1.flagClick = false;
      button2.flagClick = false;
      secretCode = 0;
      bitNum = 0;
      mode = 5;
    }
  }

  //----------Режим 1 = ПОСТАНОВКА НА ОХРАНУ----------
  // - сирена отключена
  // - светодиод мигает часто (5 раз в сек)
  // - если не нажаты любые кнопки пульта более, чем 20 сек перейти в режим 2 (ОХРАНА)
  // - если нажата любая кнопка пульта, то ОТМЕНА -> перейти в режим 0 (ОТКЛЮЧЕНО)

  else if (mode == 1) {
    digitalWrite(SIREN_PIN, LOW); //сирена отключена

    if (ledFlashCount > FAST_TIME_FLASH_LED) { //светодиод мигает часто (5 раз в сек)
      ledFlashCount = 0;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }

    if (commonTimer >= TIME_SET_GUARD) { //ожидание 20 сек и переход в режим 2 (ОХРАНА)
      commonTimer = 0;
      button1.flagClick = false;
      button2.flagClick = false;
      secretCode = 0;
      bitNum = 0;
      mode = 2;
    }

    //переход в режим 0 (ВЫКЛЮЧЕНО) при нажатии любой кнопки пульта -> ОТМЕНА
    if ((button1.flagClick == true) || (button2.flagClick == true)) {
      commonTimer = 0;
      button1.flagClick = false;
      button2.flagClick = false;
      flagTwoButtons = false;
      mode = 0;
    }
  }

  //----------Режим 2 = ОХРАНА----------
  // - сирена отключена
  // - светодиод мигает редко (1 раз в секунду)
  // - проверяется состояние шлейфов, при отклонении сопротивления свыше предельных значений перейти в режим 3 (ЗАДЕРЖКА)

  else if (mode == 2) {

    digitalWrite(SIREN_PIN, LOW); //сирена отключена

    if (ledFlashCount > SLOW_TIME_FLASH_LED) { //светодиод мигает редко (1 раз в сек)
      ledFlashCount = 0;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }

    //измерение сопротивления шлейфов
    if (averageShleif1 < MIN_U || averageShleif1 > MAX_U ||
        averageShleif2 < MIN_U || averageShleif2 > MAX_U) { //переход на режим 3 (БЛОКИРОВКА)
      commonTimer = 0;
      button1.flagClick = false;
      button2.flagClick = false;
      secretCode = 0;
      bitNum = 0;
      mode = 3;
    }

    //Проверка секретного кода. Не применяется в режиме охраны.
    //secretCodeChek();  //Функция установит mode=0 (ОТКЛЮЧЕНО), если введен верный секретный код.
  }

  //----------Режим 3 = ЗАДЕРЖКА----------
  // - сирена отключена
  // - светодиод мигает часто (5 раз в секунду)
  // - ожидается ввод секретного кода кнопками пульта в течение 30 сек
  // - при вводе секретного кода перейти в режим 0 (ОТКЛЮЧЕНО)
  // - при истечении времени ожидания секретного кода перейти в режим 4 (ТРЕВОГА)

  else if (mode == 3) {

    digitalWrite(SIREN_PIN, LOW); //сирена отключена

    if (ledFlashCount > FAST_TIME_FLASH_LED) { //светодиод мигает часто (5 раз в сек)
      ledFlashCount = 0;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }

    if (commonTimer >= TIME_BLOCK) { //ожидание 30 сек и переход в режим 4 (ТРЕВОГА)
      commonTimer = 0;
      button1.flagClick = false;
      button2.flagClick = false;
      secretCode = 0;
      bitNum = 0;
      mode = 4;
    }

    //Проверка секретного кода.
    secretCodeChek();  //Функция установит mode=0 (режим ОТКЛЮЧЕНО), если введен верный секретный код.
  }

  //----------Режим 4 = ТРЕВОГА----------
  // - сирена включена на 1 минуту
  // - светодиод мигает часто (5 раз в секунду)
  // - ожидается ввод секретного кода кнопками пульта
  // - при вводе секретного кода перейти в режим 0 (ОТКЛЮЧЕНО)
  // - по истечении времени работы сирены (1 минута) перейти в режим 2 (ОХРАНА)

  else if (mode == 4) {

    digitalWrite(SIREN_PIN, HIGH); //сирена включена

    if (ledFlashCount > FAST_TIME_FLASH_LED) { //светодиод мигает часто (5 раз в сек)
      ledFlashCount = 0;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }

    if (commonTimer >= TIME_ALARM) { //ожидание 60 сек и переход в режим 0 (ОТКЛЮЧЕНО)
      commonTimer = 0;
      button1.flagClick = false;
      button2.flagClick = false;
      flagTwoButtons = false;
      mode = 2;
    }

    //Проверка секретного кода.
    secretCodeChek();  //Функция установит mode=0 (режим ОТКЛЮЧЕНО), если введен верный секретный код.
  }

  //----------Режим 5 = УСТАНОВКА СЕКРЕТНОГО КОДА----------
  // - сирена отключена
  // - светодиод светится постоянно
  // - ожидается ввод секретного кода кнопками пульта:
  //каждое нажатие кнопки пишется в отдельный бит= кнопка №1->0, кнопка №2->1
  //число нажатий кнопок не контролируется (должно быть не более 7, иначе произойдет перезапись введенного кода)
  // - при превышении допустимого времени паузы между нажатиями кнопок (5 сек)
  //секретный код secretCode и его размер (число нажатий кнопок при вводе) bitNum записать в EEPROM
  //и перейти в режим 0 (ОТКЛЮЧЕНО)

  else if (mode == 5) {
   
  digitalWrite(SIREN_PIN, LOW); //сирена отключена
  digitalWrite(LED_PIN, HIGH); //светодиод включен

  //Обработка нажатий кнопок и ввод секретного кода
  if ((button1.flagClick == true) || (button2.flagClick == true)) { //если нажата любая кнопка пульта
    commonTimer = 0; //сброс общего таймера для замера времени между нажатиями кнопок
    secretCode = secretCode << 1; //сдвиг на 1 бит влево
    if (button1.flagClick == true) { //если нажата кнопка №1 установить младший бит в 0
      button1.flagClick = false;
      secretCode &= 0xfe; //побитовое И с маской 1111 1110
    }
    if (button2.flagClick == true) { //если нажата кнопка №2 установить младший бит в 1
      button2.flagClick = false;
      secretCode |= 1; //побитовое ИЛИ с маской 0000 0001
    }
    bitNum++; //инкремент счетчика введеных бит (= нажатий кнопок)
  }
  
  //Проверка времени паузы между нажатиями кнопок
  if (commonTimer>=TIME_SET_COD) { //если прошло 5 сек запись кода и размера кода в EEPROM
      EEPROM.write(COD_ADR,secretCode);
      EEPROM.write(NUMBER_ADR,bitNum);
      commonTimer = 0;
      button1.flagClick = false;
      button2.flagClick = false;
      flagTwoButtons = false;
      mode = 0;
    }  
  }
  else mode=0; //резервное принудительное переключение в режим 0 (ОТКЛЮЧЕНО) на случай некорректных значений переменной mode

//>>>Блок отладки через UART
  if (serialCount >= 250) { //через каждые 500 мс (2 раза в секунду) обновляем данные для отладки
    serialCount = 0;
    //Режим
    Serial.print("Mode= ");
    Serial.print(mode);
    //Состояние кнопок
    if (button1.flagPress == true)Serial.print(" Btn1 -_- ");
    else Serial.print(" Btn1 _-_ ");
    if (button2.flagPress == true)Serial.print(" Btn2 -_- ");
    else Serial.print(" Btn2 _-_ ");
    //Напряжение шлейфов
    Serial.print("Shleif1= ");
    Serial.print((float)averageShleif1 * 0.000097656, 2);
    Serial.print("V ");
    Serial.print("Shleif2= ");
    Serial.print((float)averageShleif2 * 0.000097656, 2);
    Serial.println("V");
    //где 0.000097656=(1/50)*(5/1024) - расчитанная константа для пересчета кода АЦП в напряжение
  }
//<<<Блок отладки через UART
}


//Обработчик прерывания 2 мс для MsTimer2
void timerInterupt() {
  wdt_reset(); //сброс сторожевого таймера
  button1.filterAverage(); //Вызов метода фильтрации сигнала класса Button для кнопки пульта №1
  button2.filterAverage(); //Вызов метода фильтрации сигнала класса Button для кнопки пульта №2

  //Чтение АЦП и суммирование значений напряжений шлейфов
  //результат averageSleifN=сумма напряжений шлейфа N за 50 замеров
  averageCount++; //инкремент счетчика усреднения
  sumShleif1 += analogRead(SHLEIF_1_PIN); //суммирование кодов АЦП на шлейфе №1
  sumShleif2 += analogRead(SHLEIF_2_PIN); //суммирование кодов АЦП на шлейфе №2

  //проверка количества выборок для усреднения (50)
  if (averageCount >= 50) {
    averageCount = 0;
    averageShleif1 = sumShleif1; //перегрузка накопленного значения по шлейфу №1
    averageShleif2 = sumShleif2; //перегрузка накопленного значения по шлейфу №2
    sumShleif1 = 0;
    sumShleif2 = 0;
  }
  serialCount++; //счетчик времени передачи отладочных данных через UART
  commonTimer++; //счетчик общего таймера
  ledFlashCount++; //счетчик времени мигания светодиода
}

//----------Функция проверки секретного кода-----------
//Сканирует кнопки №1,2 пульта на предмет нажатия
//Сравнивает число нажатий с переменной bitNum, сохраненной в EEPROM при установке секретного кода
//Сравнивает введенный секретный код с переменной secretCode, сохраненной в EEPROM при установке секретного кода
//При совпадении введенного и сохраненного кодов установит переменную mode=0 (режим ОТКЛЮЧЕНО).
//При несовпадении введенного и сохраненного кодов не меняет переменную mode (режим прежний).
//При нажатии обеих кнопок пульта сбрасывает введенный (неверный) код.

void secretCodeChek() {

  //Обработка нажатий кнопок и ввод секретного кода
  if ((button1.flagClick == true) || (button2.flagClick == true)) { //если нажата любая кнопка пульта
    secretCode = secretCode << 1; //сдвиг на 1 бит влево
    if (button1.flagClick == true) { //если нажата кнопка №1 установить младший бит в 0
      button1.flagClick = false;
      secretCode &= 0xfe; //побитовое И с маской 1111 1110
    }
    if (button2.flagClick == true) { //если нажата кнопка №2 установить младший бит в 1
      button2.flagClick = false;
      secretCode |= 1; //побитовое ИЛИ с маской 0000 0001
    }
    bitNum++; //инкремент счетчика введеных бит (= нажатий кнопок)
  }

  //Проверка введенного секретного кода
  if (bitNum == EEPROM.read(NUMBER_ADR)) { //если введены все цифры кода
    if (secretCode == EEPROM.read(COD_ADR)) { //если код введен верный перейти в режи 0 (ОТКЛЮЧЕНО)
      commonTimer = 0;
      button1.flagClick = false;
      button2.flagClick = false;
      flagTwoButtons = false;
      mode = 0;
    }
    else { //если код введен неверный сбросить код, режим не менять
      secretCode = 0;
      bitNum = 0;
    }
  }

  //Сброс неверного кода одновременным нажатием двух кнопок
  if ((button1.flagPress == true) && (button2.flagPress == true)) { //если нажаты обе кнопки одновременно
    button1.flagClick = false;
    button2.flagClick = false;
    secretCode = 0;
    bitNum = 0;
  }
}
