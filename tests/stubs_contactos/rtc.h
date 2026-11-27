#include <stdint.h>
struct rtc_time { uint16_t year; uint8_t month, day, hour, min, sec; };
void rtc_get_datetime(struct rtc_time *t);
