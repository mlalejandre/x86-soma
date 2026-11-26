#ifndef SOMA_RTC_H
#define SOMA_RTC_H

#include <stdint.h>

struct rtc_time {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  min;
    uint8_t  sec;
};

void rtc_get_datetime(struct rtc_time *t);   /* SOMA_332_HORA: siempre UTC (lo da hora.c); para ensenarla, hora_local() */
void rtc_leer_hw(struct rtc_time *t);        /* el reloj del equipo tal cual (puede guardar la hora local, como Windows) */

#endif
