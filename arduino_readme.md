# VAMP Protocol - Uso de Callbacks en Arduino

Este documento explica cómo implementar y usar los callbacks del protocolo VAMP en proyectos Arduino, específicamente para ESP8266 como gateway.

## Visión General

El protocolo VAMP utiliza un sistema de callbacks para separar la lógica del protocolo de las implementaciones específicas de comunicación. Esto permite que VAMP funcione con diferentes interfaces de comunicación sin modificar su código core.

## Tipos de Callbacks

VAMP define dos tipos principales de callbacks:

### 1. HTTP Callback (`vamp_http_callback_t`)

Maneja la comunicación HTTP/HTTPS con el servidor VREG (VAMP Registry).

```cpp
typedef bool (*vamp_http_callback_t)(
    const char* endpoint_url,  // URL del endpoint VREG
    uint8_t method,           // Método HTTP: VAMP_HTTP_GET o VAMP_HTTP_POST
    char* data,               // Buffer para datos de request/response
    size_t data_size          // Tamaño del buffer
);
```

**Constantes disponibles:**
- `VAMP_HTTP_GET` (0): Método GET
- `VAMP_HTTP_POST` (1): Método POST

### 2. Radio Callback (`vamp_radio_callback_t`)

Maneja la comunicación por radio con dispositivos IoT.

```cpp
typedef bool (*vamp_radio_callback_t)(
    const char* rf_id,        // ID del dispositivo RF
    uint8_t mode,            // Modo: VAMP_RADIO_READ o VAMP_RADIO_WRITE
    uint8_t* data,           // Buffer de datos
    size_t len               // Longitud de datos
);
```

**Constantes disponibles:**
- `VAMP_RADIO_READ` (0): Leer datos del radio
- `VAMP_RADIO_WRITE` (1): Escribir datos al radio

## Implementación de Callbacks

### Ejemplo: HTTP Callback para ESP8266

```cpp
bool wifi_https_comm(const char* endpoint_url, uint8_t method, char* data, size_t data_size) {
    if (!wifi_check_conn()) {
        Serial.println("Error: WiFi no conectado");
        return false;
    }
    
    // Configurar cliente HTTPS
    https_client.setInsecure(); // Solo para desarrollo
    https_http.begin(https_client, endpoint_url);
    https_http.setTimeout(HTTPS_TIMEOUT);
    https_http.setUserAgent(HTTPS_USER_AGENT);
    
    int httpResponseCode = -1;
    
    // Ejecutar según método
    switch (method) {
        case VAMP_HTTP_GET:
            httpResponseCode = https_http.GET();
            break;
            
        case VAMP_HTTP_POST:
            https_http.addHeader("Content-Type", "application/json");
            if (data != NULL) {
                httpResponseCode = https_http.POST(data);
            } else {
                httpResponseCode = https_http.POST("");
            }
            break;
            
        default:
            Serial.println("Método HTTP no soportado");
            https_http.end();
            return false;
    }
    
    // Procesar respuesta
    if (httpResponseCode > 0) {
        String response = https_http.getString();
        
        // Copiar respuesta al buffer
        if (data != NULL && data_size > 0) {
            strncpy(data, response.c_str(), data_size - 1);
            data[data_size - 1] = '\0';
        }
        
        return (httpResponseCode >= 200 && httpResponseCode < 300);
    }
    
    https_http.end();
    return false;
}
```

### Ejemplo: Radio Callback para NRF24L01

```cpp
bool nrf_comm(const char* rf_id, uint8_t mode, uint8_t* data, size_t len) {
    if (!radio.isChipConnected()) {
        Serial.println("Error: NRF24L01 no conectado");
        return false;
    }
    
    if (mode == VAMP_RADIO_WRITE) {
        // Enviar datos
        radio.stopListening();
        bool success = radio.write(data, len);
        radio.startListening();
        
        if (success) {
            Serial.print("Datos enviados a ");
            Serial.print(rf_id);
        } else {
            Serial.println("Error al enviar datos por NRF24L01");
        }
        
        return success;
        
    } else if (mode == VAMP_RADIO_READ) {
        // Leer datos
        radio.startListening();
        if (radio.available()) {
            radio.read(data, len);
            data[len] = '\0'; // Terminar string
            Serial.print("Datos recibidos de ");
            Serial.print(rf_id);
            return true;
        } else {
            Serial.println("No hay datos disponibles");
            return false;
        }
    }
    
    Serial.println("Modo de comunicación no soportado");
    return false;
}
```

## Inicialización del Sistema VAMP

### Método Recomendado: Inicialización Directa

```cpp
void setup() {
    Serial.begin(115200);
    
    // Inicializar interfaces de comunicación
    wifi_init();
    nrf_init();
    
    // Inicializar VAMP con callbacks directos
    vamp_init(
        wifi_https_comm,                    // HTTP callback
        nrf_comm,                          // Radio callback
        VAMP_REGISTRY_URL"/sync",          // URL del VREG
        VAMP_GW_ID                         // ID del gateway
    );
    
    Serial.println("Gateway VAMP listo");
}
```

### Método Alternativo: Registro por Separado

```cpp
void setup() {
    // ... inicialización de interfaces ...
    
    // Registrar callbacks por separado
    vamp_set_callbacks(wifi_https_comm, nrf_comm);
    
    // Inicializar tabla VAMP
    vamp_table_update(VAMP_REGISTRY_URL"/sync", VAMP_GW_ID);
}
```

## Patrones de Uso

### 1. Buffer Management en HTTP Callback

El HTTP callback debe manejar el buffer de manera bidireccional:

```cpp
// Para POST: data contiene los datos a enviar
// Para GET: data se usa para recibir la respuesta
// Para ambos: data_size indica el tamaño máximo del buffer

// Ejemplo de POST
if (method == VAMP_HTTP_POST) {
    // data contiene: "VAMP_SYNC,gateway_id,timestamp"
    httpResponseCode = https_http.POST(data);
    
    // Después del POST, reemplazar data con la respuesta
    String response = https_http.getString();
    strncpy(data, response.c_str(), data_size - 1);
    data[data_size - 1] = '\0';
}
```

### 2. Error Handling

Los callbacks deben implementar manejo robusto de errores:

```cpp
bool wifi_https_comm(const char* endpoint_url, uint8_t method, char* data, size_t data_size) {
    // Verificar conexión
    if (!wifi_check_conn()) {
        return false;
    }
    
    // Verificar parámetros
    if (endpoint_url == NULL || data == NULL || data_size == 0) {
        return false;
    }
    
    // Intentar comunicación con timeouts
    https_http.setTimeout(HTTPS_TIMEOUT);
    
    // ... implementación ...
    
    // Retornar false en caso de error
    if (httpResponseCode < 200 || httpResponseCode >= 300) {
        Serial.print("Error HTTP: ");
        Serial.println(httpResponseCode);
        return false;
    }
    
    return true;
}
```

### 3. Logging y Debug

Implementar logging apropiado para debugging:

```cpp
Serial.print("VAMP HTTP ");
Serial.print((method == VAMP_HTTP_GET) ? "GET" : "POST");
Serial.print(" a ");
Serial.print(endpoint_url);
Serial.print(" - Código: ");
Serial.println(httpResponseCode);
```

## Configuración de Hardware

### Dependencias para ESP8266

```cpp
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <RF24.h>
```

### Configuración NRF24L01

```cpp
#define NRF_CE_PIN 2
#define NRF_CSN_PIN 15
#define NRF_CHANNEL 76

RF24 radio(NRF_CE_PIN, NRF_CSN_PIN);

void nrf_init() {
    radio.begin();
    radio.setChannel(NRF_CHANNEL);
    radio.setDataRate(RF24_250KBPS);
    radio.setPALevel(RF24_PA_HIGH);
    radio.enableDynamicPayloads();
    radio.startListening();
}
```

### Configuración WiFi

```cpp
#define WIFI_SSID "TU_WIFI_SSID"
#define WIFI_PASSWORD "TU_WIFI_PASSWORD"

void wifi_init() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    // Esperar conexión con timeout
    int timeout = 30;
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
        delay(1000);
        timeout--;
    }
}
```

## Consideraciones de Rendimiento

1. **Reutilización de Conexiones**: Reutilizar objetos `HTTPClient` y `WiFiClientSecure` para evitar overhead de SSL handshake.

2. **Timeouts Apropiados**: Configurar timeouts razonables para evitar bloqueos.

3. **Buffer Management**: Usar buffers del tamaño apropiado para evitar fragmentación de memoria.

4. **Error Recovery**: Implementar reconexión automática para WiFi y manejo de fallos de radio.

## Integración con Loop Principal

```cpp
void loop() {
    // Tareas periódicas de VAMP
    if (millis() - last_vamp_tick >= VAMP_SYSTEM_TICK) {
        last_vamp_tick = millis();
        
        vamp_cleanup_expired();
        
        // Sincronización periódica cada 10 minutos
        static uint8_t sync_counter = 0;
        if (++sync_counter >= 60) {
            sync_counter = 0;
            vamp_table_update(VAMP_REGISTRY_URL"/sync", VAMP_GW_ID);
        }
    }
    
    // Verificar datos de radio
    if (radio.available()) {
        // Procesar datos recibidos...
    }
    
    delay(LOOP_DELAY);
}
```

Este sistema de callbacks permite que VAMP sea portable entre diferentes plataformas Arduino manteniendo la misma lógica de protocolo central.