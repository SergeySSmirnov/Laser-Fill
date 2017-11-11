#include "Arduino.h"
#include "libraries/TimerOne/TimerOne.h"
#include "libraries/ClickEncoder/ClickEncoder.h"
#include <EEPROM.h>
#include "libraries/LiquidCrystal_I2C/LiquidCrystal_I2C.h"


// ===============================================
// Объявление пинов
// ===============================================

/**
 * Пин кнопки энкодера.
 */
#define PIN_ENK_SW 3
/**
 * Пин CLK энкодера.
 */
#define PIN_ENK_CLK 4
/**
 * Пин DT энкодера.
 */
#define PIN_ENK_DT 5
/**
 * Пин для управления координатой X.
 */
#define PIN_X_COORD 7
/**
 * Пин для управления координатой Y.
 */
#define PIN_Y_COORD 8
/**
 * Пин управления лазером.
 */
#define PIN_LASER_ENABLE 9


// ===============================================
// Объявление настроек меню
// ===============================================

/**
 * Текущий шаг: настройка времени засветки.
 */
#define LIGHT_TIME 0
/**
 * Текущий шаг: настройка мощности лазера (pwm).
 */
#define LASER_POWER 1
/**
 * Текущий шаг: настройка скорости перемещения лазера (время засветки одного кадра).
 */
#define LASER_SPEED 2
/**
 * Текущий шаг: лазер в работе.
 */
#define LASER_WORK 100
/**
 * Текущий шаг: лазер остановлен.
 */
#define LASER_STOP 101
/**
 * Число пунктов меню.
 */
#define MENU_STEP_COUNT 3
/**
 * Описание пунктов меню.
 */
String MENU_STEP_DESCRIPTION[MENU_STEP_COUNT] = {"Light Time (sec)", "Lsr.Pwr. (0-255)", "Lsr.Spd. (msec)"};
/**
 * Параметры работы лазера в соответствии с настройками меню.
 */
int MENU_CONFIG_VALUES[MENU_STEP_COUNT];


/**
 * Масштаб времени, который используется при обратном отсчёте: 1UL - милисекунды, 1000UL - секунды
 */
#define LIGHT_TIME_SCALE  1000UL



/**
 * Экземпляр класса для работы с энкодером.
 */
ClickEncoder encoder(PIN_ENK_CLK, PIN_ENK_DT, PIN_ENK_SW);
#define ENC_DECODER ENC_NORMAL
#define ENC_ACCEL_INC 10
/**
 * Экземпляр класса для работы с дисплеем.
 */
LiquidCrystal_I2C lcd(0x27, 16, 2);


/**
 * Текущий шаг настройки или работы.
 */
byte currentStep = 0;
/**
 * Значение, установленное энкодером, а также его предыдущее значение.
 */
int enk_val = 0;
/**
 * Предыдущее значение энкодера.
 */
int enk_val_last = -1;

/**
 * Время окончания засветки.
 */
unsigned long end_time = 0;
/**
 * Текущее время.
 */
unsigned long time;
/**
 * Счётчик развёртки.
 */
int j = 0;


/**
 * Функция инициализации.
 */
void setup() {
	TCCR1B |= 1<<CS10;
	TCCR1B &= ~((1<<CS12)|(1<<CS11)); //изменение частоты шим
	// Считываем значения из EEPROM
	for(byte _i = 0; _i < MENU_STEP_COUNT; _i++)
		EEPROM.get(_i * 2, MENU_CONFIG_VALUES[_i]);
	enk_val = enk_val_last = MENU_CONFIG_VALUES[currentStep];
	// Инициализация лазера и системы отклонения луча
	pinMode(PIN_X_COORD, OUTPUT);
	pinMode(PIN_Y_COORD, OUTPUT);
	pinMode(PIN_LASER_ENABLE, OUTPUT);
	// Инициализируем дисплей
	lcd.init();
	lcd.backlight();
	printHello();
	lcdPrintMenu(MENU_STEP_DESCRIPTION[currentStep], MENU_CONFIG_VALUES[currentStep]);
	// Инициализация работы с энкодером
	Timer1.initialize(1000); // Инициализируем прерывание по таймеру для опроса энкодера
	Timer1.attachInterrupt(encoder_checker); // Назначаем функцию-обработчик прерывания по таймеру для опроса энкодера
	encoder.setAccelerationEnabled(true); // Разрешаем ускоренную промотку значений при изменении значений с помощью энкодера
	encoder.setDoubleClickEnabled(true); // Разрешаем двойной щелчок по кнопке энкодера
}
/**
 * Бесконечный цикл работы программы.
 */
void loop() {
	if (currentStep == LASER_WORK) { // Если лазер работает
		time = millis();
		if (time > end_time) { // Если время засветки истекло, то останавливаем лазер
			stopLaser();
			return;
		}
		if (time % 1000 == 0) // Отображаем оставшееся время засветки
			lcdPrintVal((end_time - time) / LIGHT_TIME_SCALE);



		// TODO: Здесь необходимо выполнять работу лазера по засветке, причем жеательно, чтобы засветка одного кадра осуществлялась в течение MENU_CONFIG_VALUES[LASER_SPEED].
		for(int i = 40; i <= 255; i = i+140) {
			analogWrite(PIN_X_COORD, i);
			analogWrite(PIN_Y_COORD, j);
			j = j + 4;
			if (j >= 255)
				j = 0;
			delay(1);
		}



	} else { // Если лазер не работает, то можно изменять значения параметров
		enk_val += encoder.getValue(); // Считываем новое значение энкодера
		if (enk_val != enk_val_last) { // Если новое значение и старое отличаются, значит необходимо что-либо делать
			if (enk_val < 0) // При выходе за пределы (меньше 0) значения не меняем
				enk_val = 0;
			enk_val_last = enk_val; // Запоминаем предыдущее значение
			lcdPrintVal(enk_val); // Отображаем значение
		}
	}
	switch(encoder.getButton()) { // Считываем и анализируем состояние кнопки
		case ClickEncoder::Clicked: // Если кнопка была нажата один раз, то переходим к следующему пункту меню
			if (currentStep != LASER_WORK) { // Если лазер не работает, то находимся в меню
				if (currentStep == LASER_STOP) {
					currentStep = 12345;
				} else {
					MENU_CONFIG_VALUES[currentStep] = enk_val;
					EEPROM.put(currentStep * 2, MENU_CONFIG_VALUES[currentStep]);
				}
				currentStep++;
				if (currentStep >= MENU_STEP_COUNT)
					currentStep = 0;
				enk_val = enk_val_last = MENU_CONFIG_VALUES[currentStep];
				lcdPrintMenu(MENU_STEP_DESCRIPTION[currentStep], MENU_CONFIG_VALUES[currentStep]);
			}
			break;
		case ClickEncoder::DoubleClicked: // Если кнопка была нажата дважды, то запускаем/останавливаем процесс
			if (currentStep != LASER_WORK) { // Запускаем лазер в работу
				currentStep = LASER_WORK; // Обязательно меняем значение флага
				lcdPrintMessage("Laser started!", "");
				end_time = millis() + MENU_CONFIG_VALUES[LIGHT_TIME] * LIGHT_TIME_SCALE; // Задаём время окончания засветки
				analogWrite(PIN_LASER_ENABLE, MENU_CONFIG_VALUES[LASER_POWER]); // Задаём мощность лазера
			} else { // Останавливаем лазер
				stopLaser();
			}
			break;
	}
}
/**
 * Функция остановки работы лазера.
 */
void stopLaser() {
	currentStep = LASER_STOP;
	end_time = 0;
	analogWrite(PIN_X_COORD, 0);
	analogWrite(PIN_Y_COORD, 0);
	analogWrite(PIN_LASER_ENABLE,0);
	lcdPrintMessage("Laser", "   stopped!");
}
/**
 * Функция вывода указанного сообщения и числового значения на экран.
 * @param String mess Сообщение.
 * @param int val Числовое значение.
 */
void lcdPrintMenu(String mess, int val) {
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print(mess);
	lcd.setCursor(0, 1);
	lcd.print(val);
	lcd.print("     ");
}
/**
 * Функция вывода указанного числового значения на экран.
 * @param int val Числовое значение.
 */
void lcdPrintVal(int val) {
	lcd.setCursor(0, 1);
	lcd.print(val);
	lcd.print("     ");
}
/**
 * Функция вывода указанных сообщений на экран.
 * @param String mess1 Сообщение на первой строке.
 * @param String mess2 Сообщение на второй строке.
 */
void lcdPrintMessage(String mess1, String mess2) {
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print(mess1);
	lcd.setCursor(0, 1);
	lcd.print(mess2);
}
/**
 * Функция, реализующая вывод приветственного сообщения.
 */
void printHello() {
	lcd.clear();
	lcd.setCursor(5,0);
	lcd.print("Hello!");
	lcd.setCursor(2,1);
	lcd.print("I'm laser :)");
	delay(1000);
	lcd.clear();
}
/**
 * Функция для обработки прерывания по таймеру.
 */
void encoder_checker() {
	encoder.service(); // Проверяем произошли ли какие-либо изменения
}
