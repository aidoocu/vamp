/** 
 * 
 * 
 */

#ifndef _RTC_H_
#define _RTC_H_

 /* Include la biblioteca oficial */
#include <Arduino.h>
#include <uRTCLib.h>
#include <NTPClient.h>

#define RTC_DEV DS3231

#if RTC_DEV == DS3231

#define RTC_ADDR 0x68

#endif /* RTC_DEV == RTC_DEV_DEFAULT */

/* Configuración NTP */
#define RTC_NTP_SERVER "pool.ntp.org" 
#define RTC_NTP_OFFSET 0
#define RTC_NTP_UPDATE_INTERVAL 60000
#define RTC_NTP_TIMEOUT 1500  // ms
#define RTC_NTP_MAX_TRIES 5
#define RTC_NTP_EPOCH_CHECK 1600000000UL //(2020-09-09 00:00:00 UTC)

/* Tamaño mínimo de buffer para cadenas de fecha/hora (ISO 8601 u otras)
 * yyyy-mm-ddThh:mm:ssZ -> 20 chars + '\0' -> 21, usar 32 para holgura
 */
#define DATE_TIME_BUFF 32

/** Esta funcion inicializa el RTC */
void rtc_init(void);

/** Esta funcion escribe la cadena en formato ISO 8601: yyyy-mm-ddThh:mm:ssZ
*  a partir de la hora UTC almacenada en el RTC.
*  El buffer debe tener al menos DATE_TIME_BUFF caracteres.
*/
void rtc_get_utc_time(char * time);

/** Esta funcion obtiene la hora actual en formato hh:mm-dd/mm/yyyy */
void rtc_get_time(char * time);


/**
 * Esta funcion valida la cadena de fecha y hora
 * en el formato ISO 8601: yyyy-mm-ddThh:mm:ssZ
 * Devuelve true si es correcta, false si no lo es
 */
bool rtc_validate_date_time(const char* date_time);

/**
 * Esta funcion setea la fecha y hora en el RTC a partir de una cadena ISO 8601 UTC.
 */
bool rtc_set_date_time(char * date_time);

// No timezone handling needed, all timestamps are in simple UTC format


#endif /* _RTC_H_ */