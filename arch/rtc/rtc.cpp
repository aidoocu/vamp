/** 
 * 
 * 
 * 
 */

#include "rtc.h"
// NTP support
#include <WiFiUdp.h>
#include <time.h>
#include <ESP8266WiFi.h>

uRTCLib rtc(RTC_ADDR);



/**
 * Sincroniza el RTC con un servidor NTP y actualiza el reloj interno.
 */
bool rtc_sync_time(void) {

    if (WiFi.status() != WL_CONNECTED) {
        printf("[RTC_SYNC] WiFi not connected\n");
        return false;
    }

    static WiFiUDP ntpUDP;
    NTPClient timeClient(ntpUDP, RTC_NTP_SERVER, RTC_NTP_OFFSET, RTC_NTP_UPDATE_INTERVAL);
    timeClient.begin();

    /* Intentar forzar actualización (reintentos cortos con backoff)
       Uso de forceUpdate porque queremos sincronizar una sola vez en el arranque.
    */
   // !!!!!!! creo que hay codigo duplicado aquÍ !!!!!!!
    int tries = 0;
    unsigned long epoch = 0;
    while (tries < RTC_NTP_MAX_TRIES) {

        /* Forzar petición NTP (bloqueante corto) */
        timeClient.forceUpdate();

        epoch = timeClient.getEpochTime();
        if (epoch >= RTC_NTP_EPOCH_CHECK) {
            break; // got a reasonable time
        }

        tries++;
        /* Pequeño backoff entre intentos */
        delay(200 + tries * 200);
    }

    if (tries == RTC_NTP_MAX_TRIES && epoch < RTC_NTP_EPOCH_CHECK) {
        printf("[RTC_SYNC] NTP update failed after retries\n");
        return false;
    }

    time_t t = (time_t)timeClient.getEpochTime();
    struct tm *utc = gmtime(&t);
    if (!utc) {
        printf("[RTC_SYNC] gmtime failed\n");
        return false;
    }

    // Construir cadena ISO 8601 UTC: yyyy-mm-ddThh:mm:ssZ
    char buf[DATE_TIME_BUFF];
    if (strftime(buf, DATE_TIME_BUFF, "%Y-%m-%dT%H:%M:%SZ", utc) == 0) {
        printf("[RTC_SYNC] strftime failed\n");
        return false;
    }

    printf("[RTC_SYNC] setting RTC to %s\n", buf);

    // Reutilizar rtc_set_date_time para parsear y aplicar al RTC
    if (!rtc_set_date_time(buf)) {
        printf("[RTC_SYNC] failed to set RTC\n");
        timeClient.end();
        ntpUDP.stop();
        return false;
    }

    // CRÍTICO: Liberar recursos NTP antes de salir
    timeClient.end();
    ntpUDP.stop();
    
    // Dar tiempo a la pila TCP/IP para limpiar sockets
    delay(100);
    
    printf("[RTC_SYNC] NTP resources released\n");
    return true;
}

/* Esta funcion inicializa el RTC */
void rtc_init() {
	/* RTC init */
	URTCLIB_WIRE.begin();

    /* Poner en hora */
    rtc_sync_time();

}

/* Esta funcion obtiene la hora actual en formato ISO 8601 UTC: : yyyy-mm-ddThh:mm:ssZ */
void rtc_get_utc_time(char * time) {

    rtc.refresh();
    
    // Formato UTC simple (ISO 8601)
    snprintf(time, DATE_TIME_BUFF, "%04d-%02d-%02dT%02d:%02d:%02dZ", 
                    (int)(2000 + rtc.year()), (int)rtc.month(), (int)rtc.day(), (int)rtc.hour(), (int)rtc.minute(), (int)rtc.second());
}

void rtc_get_time(char * time) {

    if (!rtc.refresh()) {
        sprintf(time, "RTC error");
        return;
    }

    // Formato hh:mm-dd/mm/yyyy (año completo)
    snprintf(time, DATE_TIME_BUFF, "%02d:%02d-%02d/%02d/%04d", 
                    (int)rtc.hour(), (int)rtc.minute(), (int)rtc.day(), (int)rtc.month(), (int)(2000 + rtc.year()));
}

bool rtc_set_date_time(char * date_time) {
    // Acepta formato ISO 8601 UTC (Z)
    if(!rtc_validate_date_time(date_time)) {
        printf("[RTC] Invalid date_time\n");
        return false;
    }
    int year, month, day, hour, minute, second;
    
    // Solo formato UTC
    sscanf(date_time, "%4d-%2d-%2dT%2d:%2d:%2dZ", &year, &month, &day, &hour, &minute, &second);
    uint8_t rtc_year = year % 100;
    uint8_t rtc_month = month;
    uint8_t rtc_day = day;
    uint8_t rtc_hour = hour;
    uint8_t rtc_minute = minute;
    uint8_t rtc_second = second;

    // Calcular el día de la semana (algoritmo de Zeller)
    int m = (rtc_month == 1 || rtc_month == 2) ? rtc_month + 12 : rtc_month;
    int y = (rtc_month == 1 || rtc_month == 2) ? year - 1 : year;
    uint8_t dayOfWeek = (rtc_day + (13*(m+1))/5 + y + y/4 - y/100 + y/400) % 7;
    dayOfWeek = (dayOfWeek == 0) ? 7 : dayOfWeek; // 1=Domingo, 7=Sábado
	
    rtc.set(rtc_second, rtc_minute, rtc_hour, dayOfWeek, rtc_day, rtc_month, rtc_year);
    return true;
}

bool rtc_validate_date_time(const char* date_time) {
    
    // Solo acepta el formato yyyy-mm-ddThh:mm:ssZ

    if (strcmp(date_time, "0000-00-00T00:00:00Z") == 0) {
        return true; // Permitir el valor por defecto para evitar errores
    } 

    int len = strlen(date_time);
    if (len != 20) return false;
    if (date_time[4] != '-' || date_time[7] != '-' || date_time[10] != 'T' || 
        date_time[13] != ':' || date_time[16] != ':' || date_time[19] != 'Z') return false;
    
    int year, month, day, hour, minute, second;
    if (sscanf(date_time, "%4d-%2d-%2dT%2d:%2d:%2dZ", &year, &month, &day, &hour, &minute, &second) != 6) return false;
    
    if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 || 
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return false;
    
    static const int daysInMonth[] = { 0,31,28,31,30,31,30,31,31,30,31,30,31 };
    int maxDays = daysInMonth[month];
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) maxDays = 29;
    if (day > maxDays) return false;
    
    return true;
}

// Timezone functions removed - all timestamps are now in simple UTC format
