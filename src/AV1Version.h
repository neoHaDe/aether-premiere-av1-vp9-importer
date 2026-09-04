// Номер версии плагина. Одно место на весь проект.
//
// Заголовок нарочно пустой на всё, кроме определений: его включает и
// компилятор ресурсов, а тот C++ не понимает — ни включений, ни объявлений.
//
// Отсюда версию берут трое: сам код (пишет её в журнал), ресурс версии в
// Aether.prm (её показывают свойства файла в проводнике) и установщик,
// который читает её уже из собранного .prm, а не хранит свою копию. Копия
// разошлась бы, и разошлась бы молча — а узнать, какая сборка на самом деле
// загружена в Premiere, стало бы не по чему. Ровно на этом мы однажды и
// потеряли вечер: старую сборку от новой отличали по размеру в байтах.
#pragma once

#define AETHER_VERSION_MAJOR 1
#define AETHER_VERSION_MINOR 3
#define AETHER_VERSION_PATCH 4

// Для ресурса версии Windows: четыре числа через запятую
#define AETHER_VERSION_COMMA AETHER_VERSION_MAJOR, AETHER_VERSION_MINOR, AETHER_VERSION_PATCH, 0

// Строкой. Собирается через двойную подстановку: без промежуточного макроса
// препроцессор подставил бы имена, а не числа.
#define AETHER_STR2(x) #x
#define AETHER_STR(x)  AETHER_STR2(x)

#define AETHER_VERSION_STR AETHER_STR(AETHER_VERSION_MAJOR) "." \
                           AETHER_STR(AETHER_VERSION_MINOR) "." \
                           AETHER_STR(AETHER_VERSION_PATCH)

// То же широкими символами: окнам Windows нужны именно они. Собирается
// отдельно, а не приставкой L к готовой строке — та уже склеена из трёх
// кусков, и приставка легла бы только на первый.
#define AETHER_WSTR2(x) L#x
#define AETHER_WSTR(x)  AETHER_WSTR2(x)

#define AETHER_VERSION_WSTR AETHER_WSTR(AETHER_VERSION_MAJOR) L"." \
                            AETHER_WSTR(AETHER_VERSION_MINOR) L"." \
                            AETHER_WSTR(AETHER_VERSION_PATCH)
