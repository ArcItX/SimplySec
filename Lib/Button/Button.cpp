/* Информация о библиотеке
 *  Button.h - библиотека цифровой обработки нажатий кнопок
 */

#include "Arduino.h"
#include "Button.h"

//Метод проверки состояния кнопки способом фильтрации по среднему
void Button::filterAverage(void)
{  
	if (flagPress != digitalRead(_pin))
	{
		if(_buttonCount != 0) _buttonCount--;
	}
	else
	{
		_buttonCount++;
		if(_buttonCount >= _timeButton)
		{																							//Иначе считаем счетчиком до TIME_BUTTON
			flagPress =! flagPress;																	//и подтверждаем ИЗМЕНЕНИЕ состояния кнопки
			_buttonCount = 0;																		//и обнуляем счетчик
			if(flagPress == true) flagClick = true;													//и фиксируем фронт, если кнопка стала НАЖАТОЙ
		}
	}
}

//Метод проверки состояния кнопки способом временной задержки
void Button::scanState(void)
{
	if (flagPress == (!digitalRead(_pin)))
	{
		_buttonCount = 0;																			//Если текущее состояние кнопки не меняется просто сбрасываем счетчик
	}
	else
	{
		_buttonCount++;
		if (_buttonCount >= _timeButton)
		{																							//Иначе считаем счетчиком до TIME_BUTTON
			flagPress =! flagPress;																	//и подтверждаем ИЗМЕНЕНИЕ состояния кнопки
			_buttonCount = 0;																		//и обнуляем счетчик
			if(flagPress == true) flagClick = true;													//и фиксируем фронт, если кнопка стала НАЖАТОЙ
		}
	}  
}

//Метод установки параметров кнопки (номер вывода пина и период замера времени)
void Button::setPinTime(byte pin, byte timeButton)
{
	_pin=pin;
	_timeButton=timeButton;
	pinMode(_pin, INPUT_PULLUP);																	//Режим входа с подтягивающим резистором для кнопки на выводе _pin
}

//Конструктор класса
Button::Button(byte pin, byte timeButton)
{
	_pin=pin;
	_timeButton=timeButton;
	pinMode(_pin, INPUT_PULLUP);																	//Режим входа с подтягивающим резистором для кнопки на выводе _pin
}
