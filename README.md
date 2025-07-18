# VAMP  - Virtual Address Mapping Protocol

El objetivo es integrar dispositivos nRF24L01+ (y chips similares) directamente en redes IP y aplicaciones. Partimos de que los payload que manejan estos dispositivos (32 bytes en nRF24L01+) son insuficientes para suportar cualquier tipo de encabezados. Estos dispositivos dependen completamente de una entidad intermediaria en todos los niveles del stack de red para comunicarse a con una entidad en la red IP.
Creando una abstracción donde cada nodo físico tiene un **gemelo digital** en un gateway que actúa como su representante se puede lograr una comunicación semi-transparente entre el nodo y el extremo IP a nivel de aplicación donde el GW funciona como un NAT.

## Arquitectura

``` text
[Nodo VAMP] ←→ [VAMP Bridge] ←→ [IP Service]
  RF/VAMP      VAMP/IP Stack      IP Stack

```

Tenemos tres roles:

| Rol         | Descripción                                                                                |
| ----------- | ------------------------------------------------------------------------------------------ |
| **Nodo**    | Dispositivo con/sin identidad preconfigurada, busca asociarse. Puede ser motes, sensores...|
| **Gateway** | Nodo central que gestiona las asociaciones, mapea cada nodo a una dirección lógica.        |
| **VAMP Registry** | Entidad central federada que genera y mantiene las tablas de mapeo RF_ID↔Puerto↔Endpoint con control de permisos. |

## Protocolo VAMP

El protocolo contiene un pseudoencabezado de un solo byte que combina el tipo de mensaje con el largo del mismo.
Hay dos tipos de mensajes: el de datos y el de comandos.

Se utiliza un pseudoencabezado de **2 bytes** para optimizar la comunicación:

- **Byte 0**: Tipo de mensaje + tamaño de payload (como antes)
- **Byte 1**: ID compacto (verificación + índice) - solo en mensajes de datos

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|T|   Tamaño/Comando ID     |    ID Compacto    |     Datos     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Datos (cont.)                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                             ...                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Pseudoencabezado

**Byte 0 (Tipo + Tamaño/Comando)**:
```text
 0   1   2   3   4   5   6   7
+---+---+---+---+---+---+---+---+
| T |     Tamaño/Comando ID     |
+---+---+---+---+---+---+---+---+
```

**Byte 1 (ID Compacto - solo datos)**:
```text
 0   1   2   3   4   5   6   7
+---+---+---+---+---+---+---+---+
|  Verificación |    Índice     |
|   (3 bits)    |   (5 bits)    |
+---+---+---+---+---+---+---+---+
```

## Sistema de Identificación Compacta

VAMP utiliza un sistema eficiente de identificación que permite acceso directo a la tabla de dispositivos y verificación de consistencia usando solo **1 byte adicional** en el payload.

### Estructura del ID Compacto

```text
Byte de ID (8 bits):
 7   6   5   4   3   2   1   0
+---+---+---+---+---+---+---+---+
|  Verificación |    Índice     |
|   (3 bits)    |   (5 bits)    |
+---+---+---+---+---+---+---+---+
```

- **Bits 7-5**: Número de verificación (0-7)
- **Bits 4-0**: Índice en tabla (0-31)

### Formato del Puerto NAT

El puerto NAT se genera automáticamente:

```text
Puerto = 8000 + (Verificación << 5) + Índice
```

**Ejemplo**:

- ID Byte = 0xA5 (10100101)
- Verificación = 5 (bits 7-5)
- Índice = 5 (bits 4-0)
- Puerto = 8000 + (5 × 32) + 5 = 8165

### Protocolo de Datos con ID

```text
Mensaje de datos (pseudoencabezado de 2 bytes):
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|0|    Tamaño Payload       |    ID Compacto    |     Datos     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Datos (cont.)                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**Ejemplo completo**:

- Byte 0: `0x05` = Datos, 5 bytes de payload
- Byte 1: `0xA5` = Verificación 5, Índice 5
- Bytes 2-6: Datos del sensor

### Ventajas del Sistema

1. **Eficiencia extrema**: Solo 1 byte adicional por mensaje
2. **Acceso directo**: `tabla[índice & 0x1F]` sin búsquedas
3. **Verificación**: Detecta reutilización de índices (probabilidad de error: 12.5%)
4. **Compatibilidad NAT**: Puertos en rango válido (8000-8255)
5. **Escalabilidad**: Hasta 32 dispositivos por gateway

### Proceso de Validación

```c
// Extraer campos del mensaje de datos
uint8_t tipo_tamaño = payload[0];
uint8_t id_byte = payload[1];
uint8_t* datos = &payload[2];

// Verificar tipo de mensaje
if (tipo_tamaño & 0x80) {
    // Es comando, no datos
    return false;
}

// Extraer tamaño y validar
uint8_t tamaño_total = tipo_tamaño & 0x7F;
uint8_t tamaño_datos = tamaño_total - 1; // Restar ID compacto

// Validar ID compacto
uint8_t indice = id_byte & 0x1F;        // Extraer índice
uint8_t verificacion = id_byte >> 5;     // Extraer verificación

if (tabla[indice].verification == verificacion) {
    // ID válido, procesar mensaje
    uint16_t puerto = 8000 + (verificacion << 5) + indice;
    procesar_datos(datos, tamaño_datos, puerto);
} else {
    // ID inválido, rechazar o re-asignar
    enviar_error_al_nodo(id_byte);
}
```

**Campos:**

- **T (Tipo)**: 1 bit
  - 0 = Mensaje de datos
  - 1 = Mensaje de comando
- **Tamaño/Comando ID**: 7 bits
  - Si T=0: Tamaño del payload de datos (0-127 bytes)
  - Si T=1: Identificador del comando (0-127)

### Mensajes de Datos (T = 0 / Bit 7 = 0)

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|0|   Tamaño (0-127)        |    ID Compacto    |     Datos     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Datos (cont.)                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

#### Ejemplo de mensaje de datos

```text
Byte 0: 0x05 = 00000101 (binario)
        |      |
        |      └─ Tamaño: 5 bytes de datos
        └─ Tipo: 0 (mensaje de datos)

Byte 1: 0xA5 = 10100101 (binario)
        |   |  |     |
        |   |  └─────┴─ Índice: 5 (tabla[5])
        └───┴─ Verificación: 5

Payload completo: [0x05] [0xA5] [0x12] [0x34] [0x56] [0x78]
                    ^      ^      ^──── 4 bytes de datos ────^
                    │      │
                    │      └─ ID Compacto (verification=5, index=5)
                    └─ Pseudoencabezado (datos, tamaño=5)
```

**Nota**: El tamaño incluye el ID compacto + datos reales

### Mensajes de Comando (T=1 / Bit 7 = 1)

**Nota**: Los comandos no usan ID compacto en el byte 1, van directamente los datos del comando.

#### Ejemplo mensaje de comando

```text
Byte 0: 0x81 = 10000001 (binario)
        |      |
        |      └─ Comando ID: 0x01 (JOIN_REQ)
        └─ Tipo: 1 (mensaje de comando)

Payload completo: [0x81] [datos del comando...]
                    ^      ^
                    │      └─ Datos específicos del comando
                    └─ Pseudoencabezado con comando JOIN_REQ
```

#### Comandos Disponibles

| Comando   | Valor | Descripción                          |
|-----------|-------|--------------------------------------|
| JOIN_REQ  | 0x81  | Solicitud de unión a la red          |
| JOIN_ACK  | 0x82  | Confirmación de unión                |
| PING      | 0x83  | Mensaje de verificación de conexión  |
| PONG      | 0x84  | Respuesta a mensaje PING             |

#### JOIN_REQ (0x81)

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|1|0 0 0 0 0 0 1|                ID del Nodo                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           ID del Nodo (cont.)           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

#### JOIN_ACK (0x82)

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|1|0 0 0 0 0 1 0|    ID Compacto    |     ID del Gateway        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          ID del Gateway (cont.)         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**Campos**:

- **Byte 0**: `0x82` (comando JOIN_ACK)
- **Byte 1**: ID compacto asignado (verificación + índice)
- **Bytes 2-6**: ID del gateway (5 bytes)

#### PING (0x83) / PONG (0x84)

```text
 0                   1
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|1|0 0 0 0 0 1 1|   Timestamp   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Timestamp (cont.)         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

## Nodo como Extremo de Puerto NAT

### Implementación del Concepto

#### Gateway como NAT por Puerto

El gateway actúa como un **NAT (Network Address Translation)** por puerto:

1. **Traducción de Protocolos**: VAMP ↔ TCP/UDP
2. **Mapeo de Puertos**: RF_ID ↔ Puerto virtual
3. **Gestión de Sesiones**: Mantiene estado de conexiones activas
4. **Enrutamiento Bidireccional**: Permite comunicación iniciada desde cualquier extremo

#### Tabla de Estado de Conexiones

```text
┌─────────────┬─────────┬─────────────┬─────────────┬─────────────┬────────────────┐
│   RF_ID     │ Índice  │ Verificac.  │   Puerto    │   Estado    │  Endpoint      │
├─────────────┼─────────┼─────────────┼─────────────┼─────────────┼────────────────┤
│  0x1A2B3C   │   01    │     5       │    8165     │   ACTIVO    │ api.farm.com   │
│  0x4D5E6F   │   02    │     3       │    8098     │  INACTIVO   │ ctrl.farm.com  │
│  0x7A8B9C   │   03    │     7       │    8227     │   ACTIVO    │ track.fleet.com│
└─────────────┴─────────┴─────────────┴─────────────┴─────────────┴────────────────┘
```

**Cálculo de puertos**:

- Dispositivo 1: Puerto = 8000 + (5 << 5) + 1 = 8000 + 160 + 1 = 8161
- Dispositivo 2: Puerto = 8000 + (3 << 5) + 2 = 8000 + 96 + 2 = 8098  
- Dispositivo 3: Puerto = 8000 + (7 << 5) + 3 = 8000 + 224 + 3 = 8227

### Niveles de Abstracción

#### Nivel 1: Identidad Física (nRF24L01+)

- **ID único del hardware**: Dirección MAC de 5 bytes del chip RF
- **Protocolo**: VAMP sobre nRF24L01+
- **Alcance**: Red de área local RF (LAN-RF)

#### Nivel 2: Identidad Virtual (Puerto Gateway)

- **Puerto virtual asignado**: Puerto único en el gateway NAT
- **Protocolo**: UDP/TCP sobre IP del gateway
- **Alcance**: Red virtual gestionada por gateway

#### Nivel 3: Identidad de Aplicación (Endpoint)

- **Servicio final**: URL/dominio de la aplicación destino
- **Protocolo**: HTTP/HTTPS, MQTT, CoAP, etc.
- **Alcance**: Internet/WAN

### Funcionamiento del Sistema NAT-like

#### Desde la Perspectiva del Nodo

``` text
Nodo sensor → "Envío datos al Gateway" (solo conoce ID del GW)
              ↓
          [No conoce puertos, no conoce endpoint final]
```

#### Desde la Perspectiva del Gateway

``` text
Recibe: [RF_ID: 0x1A2B3C] + [datos: temp=23°C]
        ↓
Consulta tabla: RF_ID → Puerto 8001 → api.farm.com/sensors
        ↓
Crea mapeo NAT: Gateway:8001 ↔ api.farm.com
        ↓
Envía: POST api.farm.com/sensors {"gateway_port": 8001, "temp": 23}
```

#### Desde la Perspectiva de la Aplicación

``` text
Aplicación ve: "El sensor en gateway_ip:8001 reportó temperatura 23°C"
               ↓
           [Para responder: envía a gateway_ip:8001]
```

#### Comunicación Bidireccional

``` text
Aplicación → Gateway:8001 → Consulta tabla → RF_ID: 0x1A2B3C → Nodo
```

### Ventajas del Sistema NAT por Puerto

#### 1. **Transparencia Bidireccional**

- **Nodo**: Solo conoce al gateway, no necesita saber de puertos o endpoints
- **Aplicación**: Ve nodos como puertos específicos del gateway
- **Gateway**: Maneja toda la traducción y mapeo NAT

#### 2. **Comunicación Bidireccional Nativa**

- Cada nodo RF aparece como un puerto específico en el gateway
- Aplicaciones pueden iniciar comunicación enviando a gateway:puerto
- Gateway traduce automáticamente puerto → RF_ID
- Soporte nativo para request/response patterns

#### 3. **Eficiencia de Red**

- Sin necesidad de IPs virtuales o subnets
- Mapeo directo puerto ↔ RF_ID
- Menor overhead en gateway
- Escalabilidad limitada solo por rango de puertos

#### 4. **Gestión Simplificada**

- Tabla de mapeo simple: RF_ID ↔ Puerto
- Identificación única por puerto
- Logs y debugging más directos

### Casos de Uso

#### Escenario 1: Granja Inteligente

``` text
Sensores de campo (nRF24L01+) → Gateway rural → Internet → Sistema de gestión agrícola
RF IDs: 0x001, 0x002, 0x003   →   Puertos: 8001, 8002, 8003   →   api.farm.com
```

**Comunicación bidireccional:**

- Sensor → Gateway:8001 → api.farm.com/sensors (datos de temperatura)
- api.farm.com → Gateway:8001 → Sensor (comando de calibración)

#### Escenario 2: IoT Industrial

``` text
Sensores de fábrica → Gateway industrial → Red corporativa → ERP industrial
RF IDs: 0x100-0x1FF →  Puertos: 8100-8199 →              → erp.company.com
```

**Control remoto:**

- Aplicación → Gateway:8150 → Actuador (comando de activación)
- Actuador → Gateway:8150 → Aplicación (confirmación de estado)

#### Escenario 3: Ciudad Inteligente

``` text
Sensores urbanos → Gateways distribuidos → Red municipal → Plataforma smart city
RF IDs: variados →    Puertos: 8000-9999    →              → city.platform.gov
```

### Beneficios de la Arquitectura

#### Para Desarrolladores de Aplicaciones

- **Simplicidad**: Tratan nodos RF como puertos específicos del gateway
- **Comunicación Bidireccional**: Pueden iniciar comunicación hacia gateway:puerto
- **Escalabilidad**: Agregan nodos sin cambiar código de aplicación
- **Debugging**: Identificación directa por puerto

#### Para Administradores de Red

- **Visibilidad**: Monitoreo unificado por puerto
- **Control**: Políticas de firewall por puerto
- **Mantenimiento**: Gestión simple de mapeo puerto↔RF_ID
- **Escalabilidad**: Hasta 65535 puertos por gateway

#### Para Dispositivos RF

- **Simplicidad**: Solo necesitan implementar protocolo VAMP básico
- **Eficiencia**: No requieren conocimiento de puertos o IPs
- **Autonomía**: Funcionan independientemente de infraestructura IP
- **Bidireccionalidad**: Pueden recibir comandos desde aplicaciones

Esta arquitectura NAT por puerto crea un mapeo directo y eficiente entre el mundo RF y las aplicaciones IP, permitiendo comunicación bidireccional transparente sin la complejidad de IPs virtuales.

## Gestión de Tabla NAT por Ente Central

### Arquitectura del Sistema

```text
[Nodo RF] ←→ [Gateway] ←→ [Ente Central] ←→ [Servicio(s)]
                             ↓
                        [Federación]
                      ┌─────────────────┐
                      │ Ente Central A  │ (empresa-x.com)
                      │ Ente Central B  │ (empresa-y.com)  
                      │ Ente Central C  │ (empresa-z.com)
                      └─────────────────┘
```

### Protocolo HTTP/REST Gateway ↔ Ente Central

#### Inicialización del Gateway

```http
POST /api/v1/gateway/register
Content-Type: application/json

{
  "gateway_id": "GW_123",
  "location": "farm_sector_a",
  "capabilities": ["HTTP", "MQTT", "CoAP"],
  "max_nodes": 100
}
```

#### Obtención de Tabla Inicial

```http
GET /api/v1/gateway/GW_123/mappings
Authorization: Bearer <gateway_token>

Response:
{
  "gateway_id": "GW_123",
  "version": "1.2.3",
  "timestamp": "2025-07-17T10:30:00Z",
  "mappings": [
    {
      "rf_id": "0x1A2B3C",
      "port": 8001,
      "endpoints": [
        {
          "url": "api.farm.com/sensors",
          "protocol": "HTTP",
          "priority": 1,
          "permissions": {
            "read": true,
            "write": false
          }
        },
        {
          "url": "alert.farm.com/notifications",
          "protocol": "HTTP", 
          "priority": 2,
          "permissions": {
            "read": true,
            "write": true
          }
        }
      ]
    }
  ]
}
```

#### Actualizaciones Dinámicas (Polling)

```http
GET /api/v1/gateway/GW_123/mappings/updates?since=2025-07-17T10:30:00Z
Authorization: Bearer <gateway_token>

Response:
{
  "has_updates": true,
  "updates": [
    {
      "action": "ADD",
      "mapping": {
        "rf_id": "0x4D5E6F",
        "port": 8002,
        "endpoints": [...]
      }
    },
    {
      "action": "MODIFY",
      "rf_id": "0x1A2B3C",
      "changes": {
        "endpoints[0].permissions.write": true
      }
    },
    {
      "action": "DELETE",
      "rf_id": "0x7A8B9C"
    }
  ]
}
```

### Formato de Tabla con Permisos

```json
{
  "gateway_id": "GW_123",
  "version": "1.2.3",
  "timestamp": "2025-07-17T10:30:00Z",
  "mappings": [
    {
      "rf_id": "0x1A2B3C",
      "port": 8001,
      "node_type": "sensor",
      "description": "Sensor temperatura campo A",
      "endpoints": [
        {
          "service_id": "farm_monitoring",
          "url": "api.farm.com/sensors",
          "protocol": "HTTP",
          "priority": 1,
          "permissions": {
            "read": true,
            "write": false,
            "admin": false
          },
          "rate_limit": {
            "requests_per_minute": 60,
            "burst": 10
          }
        },
        {
          "service_id": "alert_system", 
          "url": "alert.farm.com/notifications",
          "protocol": "HTTP",
          "priority": 2,
          "permissions": {
            "read": true,
            "write": true,
            "admin": false
          },
          "rate_limit": {
            "requests_per_minute": 30,
            "burst": 5
          }
        },
        {
          "service_id": "maintenance_service",
          "url": "maint.farm.com/diagnostics", 
          "protocol": "HTTP",
          "priority": 3,
          "permissions": {
            "read": true,
            "write": true,
            "admin": true
          },
          "rate_limit": {
            "requests_per_minute": 10,
            "burst": 2
          }
        }
      ]
    }
  ]
}
```

### Tipos de Permisos

| Permiso | Descripción | Operaciones Permitidas |
|---------|-------------|----------------------|
| **read** | Lectura de datos del nodo | Recibir datos del nodo |
| **write** | Escritura hacia el nodo | Enviar comandos al nodo |
| **admin** | Administración del nodo | Configurar, resetear, actualizar firmware |

### Federación de Entes Centrales

#### Descubrimiento Inter-Federación

```http
GET /api/v1/federation/discover/node/0x1A2B3C
Authorization: Bearer <federation_token>

Response:
{
  "found": true,
  "authority": "empresa-x.com",
  "endpoint": "https://vamp.empresa-x.com/api/v1",
  "delegation_possible": true
}
```

#### Delegación de Autoridad

```http
POST /api/v1/federation/delegate
Content-Type: application/json
Authorization: Bearer <federation_token>

{
  "node_id": "0x1A2B3C",
  "delegate_to": "GW_456",
  "permissions": {
    "read": true,
    "write": false,
    "admin": false
  },
  "duration": "24h"
}
```

## Asociación a la Red

```text
1. Nodo → Gateway (broadcast): [0x81] [ID_NODO] (JOIN_REQ con ID del nodo)
2. Gateway → Ente Central: Consulta mapeo para RF_ID
3. Ente Central → Gateway: Respuesta con puerto y endpoints
4. Gateway → Nodo: [0x82] [ID_COMPACTO] [ID_GATEWAY] (JOIN_ACK con ID compacto + ID del gateway)
5. Nodo → Gateway (directo): [0x04] [ID_COMPACTO] [0x20] [0x25] [0x30] (Datos con ID compacto)
6. Gateway → Endpoints: Distribuye datos según permisos usando puerto calculado
```

**Proceso detallado:**

- **Paso 1**: El nodo envía JOIN_REQ por broadcast incluyendo su propio ID
- **Paso 2**: Gateway consulta al Ente Central si tiene mapeo para ese RF_ID
- **Paso 3**: Ente Central responde con puerto asignado y lista de endpoints autorizados
- **Paso 4**: Gateway genera ID compacto (verificación + índice) y responde JOIN_ACK al nodo
- **Paso 5**: Nodo envía datos incluyendo su ID compacto en el byte 1
- **Paso 6**: Gateway valida ID compacto, calcula puerto y distribuye datos a endpoints

### Manejo de Permisos en Comunicación Bidireccional

#### Flujo de Datos: Nodo → Servicios (READ)

```text
Nodo 0x1A2B3C → Gateway:8001 → Datos de temperatura
                                ↓
Gateway consulta permisos:
├── farm_monitoring (read: ✓) → api.farm.com/sensors
├── alert_system (read: ✓) → alert.farm.com/notifications  
└── maintenance_service (read: ✓) → maint.farm.com/diagnostics
```

#### Flujo de Comandos: Servicios → Nodo (WRITE)

```text
Comando de calibración → Gateway:8001
                        ↓
Gateway verifica permisos:
├── farm_monitoring (write: ✗) → Comando RECHAZADO
├── alert_system (write: ✓) → Comando PERMITIDO
└── maintenance_service (write: ✓) → Comando PERMITIDO
                        ↓
Gateway → Nodo 0x1A2B3C (solo si hay permisos write)
```

#### Flujo de Administración: Servicios → Nodo (ADMIN)

```text
Comando de reset → Gateway:8001
                  ↓
Gateway verifica permisos:
├── farm_monitoring (admin: ✗) → Comando RECHAZADO
├── alert_system (admin: ✗) → Comando RECHAZADO
└── maintenance_service (admin: ✓) → Comando PERMITIDO
                  ↓
Gateway → Nodo 0x1A2B3C (solo maintenance_service)
```

### Implementación de Rate Limiting

```text
Servicio intenta enviar comando:
├── Verificar permisos (write/admin)
├── Consultar rate limit del servicio
├── Si dentro del límite: procesar
└── Si excede límite: HTTP 429 (Too Many Requests)
```

### Casos de Uso de Permisos

#### Escenario 1: Sensor de Temperatura

```json
{
  "rf_id": "0x1A2B3C",
  "port": 8001,
  "endpoints": [
    {
      "service_id": "monitoring_dashboard",
      "permissions": {"read": true, "write": false, "admin": false},
      "description": "Solo lectura para dashboard"
    },
    {
      "service_id": "alert_system", 
      "permissions": {"read": true, "write": true, "admin": false},
      "description": "Lectura + comandos de configuración"
    },
    {
      "service_id": "maintenance_team",
      "permissions": {"read": true, "write": true, "admin": true},
      "description": "Acceso completo para mantenimiento"
    }
  ]
}
```

#### Escenario 2: Actuador Crítico

```json
{
  "rf_id": "0x4D5E6F",
  "port": 8002,
  "endpoints": [
    {
      "service_id": "safety_system",
      "permissions": {"read": true, "write": true, "admin": true},
      "priority": 1,
      "description": "Sistema de seguridad - prioridad máxima"
    },
    {
      "service_id": "production_control",
      "permissions": {"read": true, "write": true, "admin": false},
      "priority": 2,
      "description": "Control de producción - sin admin"
    }
  ]
}
```

## Manejo de Pérdida de Conexión con Gateway

### Detección de Fallos

El protocolo VAMP implementa un mecanismo de detección automática de pérdida de conexión con el gateway:

- **Contador de fallos**: Se mantiene un contador de envíos fallidos consecutivos
- **Umbral configurable**: Por defecto, 3 fallos consecutivos (`MAX_SEND_FAILURES = 3`)
- **Reseteo automático**: El contador se resetea con cada envío exitoso

### Recuperación Automática

Cuando se detecta pérdida de conexión (fallos >= umbral):

1. **Reset de conexión**: La dirección del gateway se resetea a broadcast (`0xFF, 0xFF, 0xFF, 0xFF, 0xFF`)
2. **Re-join automático**: Se ejecuta automáticamente el proceso de unión a la red
3. **Reintento inmediato**: Si el re-join es exitoso, se reintenta el envío del mensaje original
4. **Recuperación transparente**: La aplicación no necesita manejar estos casos

### Escenarios Manejados

| Escenario | Detección | Recuperación |
|-----------|-----------|--------------|
| Gateway se apaga | Fallos consecutivos | Re-join automático con cualquier gateway disponible |
| Gateway cambia dirección | Fallos consecutivos | Re-join encuentra nuevo gateway |
| Pérdida temporal de red | Fallos consecutivos | Reintentos automáticos |
| Múltiples gateways | Primer gateway que responda | Se conecta al gateway más rápido en responder |

### Flujo de Recuperación

```text
Envío → Éxito → Continuar (contador = 0)
Envío → Fallo → Incrementar contador (1)
Envío → Fallo → Incrementar contador (2)
Envío → Fallo → Contador = 3 → Reset conexión → Re-join → Reintento
```

### Funciones Adicionales

- **`vamp_force_rejoin()`**: Permite forzar un re-join manual
- **`vamp_is_joined()`**: Verifica si está conectado a un gateway

## Manejo de Nodos Huérfanos

**Problema**: Nodo cae en cobertura de 3 gateways, pero "pertenece" a entidad X con endpoint Y. Si ningún gateway local tiene registrado ese nodo, queda huérfano.

**Solución**: Gateway Proxy con Descubrimiento Distribuido

### Nuevos Comandos VAMP

```cpp
#define ENDPOINT_REG     0x89  // Registro de endpoint en nodo
#define ENDPOINT_ACK     0x8A  // Confirmación de endpoint
#define GATEWAY_QUERY    0x8B  // Consulta entre gateways
#define GATEWAY_RESPONSE 0x8C  // Respuesta entre gateways
#define TEMP_ADOPTION    0x8D  // Adopción temporal
```

### Flujo de Manejo de Huérfanos

#### Escenario 1: Nodo con Endpoint Registrado

```text
Nodo A (endpoint: api.empresa-x.com) → Broadcast: JOIN_REQ
├── Gateway 1 (empresa-y): No tiene registro → Query otros gateways
├── Gateway 2 (empresa-z): No tiene registro → Query otros gateways  
└── Gateway 3 (empresa-x): Tiene registro → Responde JOIN_ACK

Resultado: Nodo A se conecta a Gateway 3 (su propietario)
```

#### Escenario 2: Nodo Huérfano Puro

```text
Nodo B (endpoint: api.empresa-w.com) → Broadcast: JOIN_REQ
├── Gateway 1: No registrado → Query otros gateways → Sin respuesta
├── Gateway 2: No registrado → Query otros gateways → Sin respuesta
└── Gateway 3: No registrado → Query otros gateways → Sin respuesta

Flujo de recuperación:
1. Gateway 1 → Nodo B: TEMP_ADOPTION (adopción temporal)
2. Gateway 1 → VAMP Bridge: Consulta endpoint api.empresa-w.com
3. VAMP Bridge → Gateway 1: Endpoint válido, proceder
4. Gateway 1 actúa como proxy para Nodo B
```

#### Escenario 3: Descubrimiento Distribuido

```text
Nodo C (endpoint: api.empresa-x.com) → Broadcast: JOIN_REQ
├── Gateway A: No registrado → Query red de gateways
│   └── Gateway A → Gateway X: GATEWAY_QUERY(node_id=C)
│       └── Gateway X → Gateway A: GATEWAY_RESPONSE(endpoint=empresa-x.com)
└── Gateway A → Nodo C: JOIN_ACK (actuando como proxy)

Resultado: Gateway A hace proxy para Nodo C hacia Gateway X
```

### Ventajas de Esta Solución

1. **Transparencia**: El nodo solo necesita registrar su endpoint una vez
2. **Escalabilidad**: Gateways pueden hacer proxy sin conocer todos los nodos
3. **Tolerancia a fallos**: Múltiples gateways pueden manejar el mismo nodo
4. **Flexibilidad**: Soporte para diferentes protocolos (HTTP, MQTT, CoAP)
5. **Eficiencia**: Cache distribuido evita consultas repetitivas

### Ejemplo Completo de Flujo

```text
1. Nodo sensor temperatura → vamp_register_endpoint("api.factory.com/temp", HTTP)
2. Nodo se mueve a área con gateways de diferentes empresas
3. Nodo → Broadcast: JOIN_REQ
4. Gateway local → Red de gateways: GATEWAY_QUERY
5. Gateway propietario → Gateway local: GATEWAY_RESPONSE(endpoint=api.factory.com)
6. Gateway local → Nodo: JOIN_ACK (proxy mode)
7. Nodo → Gateway local: Datos de temperatura
8. Gateway local → VAMP Bridge: Datos + endpoint info
9. VAMP Bridge → api.factory.com: POST /temp (HTTP)
```

Esta solución permite que cualquier nodo se comunique con su endpoint final a través de cualquier gateway disponible, resolviendo el problema de los nodos huérfanos de manera escalable.
