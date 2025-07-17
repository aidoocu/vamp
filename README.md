# VAMP  - Virtual Address Mapping Protocol

El objetivo es integrar dispositivos nRF24L01+ (y chips similares) directamente en redes IP y aplicaciones, creando una abstracción donde cada nodo físico tiene un **gemelo digital** en la red que actúa como su representante.

### Nodo como Gemelo Digital (Digital Twin)
```
[Nodo RF] ←→ [Gateway] ←→ [Gemelo Digital] ←→ [Aplicación IP]
  Hardware    Bridge RF     Virtual Node      HTTP/TCP/IP
```

### Tabla de Mapeo Multi-nivel en Gateway

El gateway mantiene una tabla de asociación que vincula cada nodo RF con su gemelo digital:

```
┌─────────────────┬─────────────────┬─────────────────┬─────────────────┐
│   Nodo RF       │  Gemelo Digital │   Red Virtual   │  Endpoint App   │
├─────────────────┼─────────────────┼─────────────────┼─────────────────┤
│ RF_ID: 0x1A2B3C │ IP: 10.0.1.101  │ VLAN: sensors   │ api.farm.com    │
│ RF_ID: 0x4D5E6F │ IP: 10.0.1.102  │ VLAN: actuators │ ctrl.farm.com   │
│ RF_ID: 0x7A8B9C │ IP: 10.0.2.101  │ VLAN: mobile    │ track.fleet.com │
└─────────────────┴─────────────────┴─────────────────┴─────────────────┘
```

### Niveles de Abstracción

#### Nivel 1: Identidad Física (nRF24L01+)
- **ID único del hardware**: Dirección MAC de 5 bytes del chip RF
- **Protocolo**: VAMP sobre nRF24L01+
- **Alcance**: Red de área local RF (LAN-RF)

#### Nivel 2: Identidad Virtual (Gemelo Digital)
- **IP virtual asignado**: Dirección IP única en subnet virtual
- **Protocolo**: TCP/IP estándar
- **Alcance**: Red virtual gestionada por gateway

#### Nivel 3: Identidad de Aplicación (Endpoint)
- **Servicio final**: URL/dominio de la aplicación destino
- **Protocolo**: HTTP/HTTPS, MQTT, CoAP, etc.
- **Alcance**: Internet/WAN

### Funcionamiento del Sistema NAT-like

#### Desde la Perspectiva del Nodo:
```
Nodo sensor → "Envío datos al Gateway" (solo conoce ID del GW)
              ↓
          [No conoce IP, no conoce endpoint final]
```

#### Desde la Perspectiva del Gateway:
```
Recibe: [RF_ID: 0x1A2B3C] + [datos: temp=23°C]
        ↓
Consulta tabla: RF_ID → IP virtual 10.0.1.101 → api.farm.com/sensors
        ↓
Crea sesión: Gemelo(10.0.1.101) → api.farm.com
        ↓
Envía: POST api.farm.com/sensors {"node_ip": "10.0.1.101", "temp": 23}
```

#### Desde la Perspectiva de la Aplicación:
```
Aplicación ve: "El sensor 10.0.1.101 reportó temperatura 23°C"
               ↓
           [No sabe que es un nRF24L01+, ve solo IP]
```

### Ventajas del Gemelo Digital

#### 1. **Transparencia Bidireccional**
- **Nodo**: Solo conoce al gateway, no necesita saber de IPs o endpoints
- **Aplicación**: Ve nodos como dispositivos IP normales
- **Gateway**: Maneja toda la traducción y mapeo

#### 2. **Integración Natural con Infraestructura IP**
- Cada nodo RF aparece como un dispositivo IP real
- Compatible con herramientas de monitoreo de red existentes
- Soporte para VLANs, QoS, firewalls estándar

#### 3. **Escalabilidad de Red**
- Subnets virtuales para diferentes tipos de nodos
- Asignación dinámica de IPs virtuales
- Enrutamiento inteligente basado en tipo de dispositivo

#### 4. **Gestión Centralizada**
- Tabla de mapeo única en gateway
- Políticas de red por VLAN/subnet
- Logs y auditoría unificados

### Casos de Uso

#### Escenario 1: Granja Inteligente
```
Sensores de campo (nRF24L01+) → Gateway rural → Internet → Sistema de gestión agrícola
RF IDs: 0x001, 0x002, 0x003   →   IPs: 10.0.1.x   →         api.farm.com
```

#### Escenario 2: IoT Industrial
```
Sensores de fábrica → Gateway industrial → Red corporativa → ERP industrial
RF IDs: 0x100-0x1FF →  IPs: 192.168.10.x →              → erp.company.com
```

#### Escenario 3: Ciudad Inteligente
```
Sensores urbanos → Gateways distribuidos → Red municipal → Plataforma smart city
RF IDs: variados →    IPs: 172.16.x.x    →              → city.platform.gov
```

### Implementación del Concepto

#### Gateway como NAT Virtual
El gateway actúa como un **NAT (Network Address Translation)** especializado:

1. **Traducción de Protocolos**: VAMP ↔ TCP/IP
2. **Mapeo de Direcciones**: RF_ID ↔ IP virtual
3. **Gestión de Sesiones**: Mantiene estado de conexiones activas
4. **Enrutamiento Inteligente**: Dirige tráfico según políticas definidas

#### Tabla de Estado de Conexiones
```
┌─────────────┬─────────────┬─────────────┬─────────────┬────────────────┐
│   RF_ID     │  IP Virtual │   Estado    │  Última Act │  Endpoint      │
├─────────────┼─────────────┼─────────────┼─────────────┼────────────────┤
│  0x1A2B3C   │ 10.0.1.101  │   ACTIVO    │  14:23:45   │ api.farm.com   │
│  0x4D5E6F   │ 10.0.1.102  │  INACTIVO   │  12:15:30   │ ctrl.farm.com  │
│  0x7A8B9C   │ 10.0.2.101  │   ACTIVO    │  14:24:01   │ track.fleet.com│
└─────────────┴─────────────┴─────────────┴─────────────┴────────────────┘
```

### Beneficios de la Arquitectura

#### Para Desarrolladores de Aplicaciones:
- **Simplicidad**: Tratan nodos RF como dispositivos IP normales
- **Compatibilidad**: Usan herramientas y librerías IP estándar
- **Escalabilidad**: Agregan nodos sin cambiar código de aplicación

#### Para Administradores de Red:
- **Visibilidad**: Monitoreo unificado de dispositivos RF e IP
- **Control**: Políticas de red estándar aplicables a nodos RF
- **Mantenimiento**: Gestión centralizada desde gateway

#### Para Dispositivos RF:
- **Simplicidad**: Solo necesitan implementar protocolo VAMP básico
- **Eficiencia**: No requieren stack TCP/IP completo
- **Autonomía**: Funcionan independientemente de infraestructura IP

Esta arquitectura de gemelo digital crea un puente transparente entre el mundo RF de baja potencia y las aplicaciones IP modernas, permitiendo integración sin fisuras de dispositivos IoT simples en infraestructuras complejas.

## Solución Propuesta: Arquitectura Multi-nivel con VAMP Bridge

### Arquitectura
```
[Nodo VAMP] ←→ [Gateway VAMP] ←→ [VAMP Bridge] ←→ [IP Service]
   RF/VAMP        RF/VAMP         VAMP/IP        HTTP/TCP/IP
```

| Rol         | Descripción                                                                                |
| ----------- | ------------------------------------------------------------------------------------------ |
| **Nodo**    | Dispositivo sin identidad preconfigurada, busca asociarse. Puede ser motes, sensores, etc. |
| **Gateway** | Nodo central que gestiona las asociaciones, mapea cada nodo a una dirección lógica.        |

## Protocolo VAMP

El protocolo contiene un pseudoencabezado de un solo byte que combina el tipo de mensaje con el largo del mismo.
Hay dos tipos de mensajes: el de datos y el de comandos.

Se utiliza un solo byte tanto para identificar el tipo de mensaje como para el tamaño del mensaje. Teniendo en cuenta que el tamaño máximo del payload es de 32 bytes, entonces solo se necesitan 6 bits para el tamaño. Solo tienen tamaño (0-32) los mensajes que contengan datos, por lo que se utiliza el bit más significativo del byte para identificar el tipo de mensaje (datos/comandos):

- Si es 0, es un mensaje de datos, y el resto de bits para el tamaño del mensaje (0-32).
- Si es 1, es un mensaje de comando, y el resto de bits se utilizan para identificar el comando en concreto. A partir de saber el comando, se puede saber el tratamiento para el resto del mensaje.

## Ejemplo de Formato de Mensajes

### Mensaje de Datos (Bit 7 = 0)

```text
Byte 0: 0x05 = 00000101 (binario)
        |      |
        |      └─ Tamaño: 5 bytes de datos
        └─ Tipo: 0 (mensaje de datos)

Payload completo: [0x05] [0x12] [0x34] [0x56] [0x78] [0x9A]
                    ^      ^────── 5 bytes de datos ──────^
                    │
                    └─ Pseudoencabezado
```

### Mensaje de Comando (Bit 7 = 1)

```text
Byte 0: 0x81 = 10000001 (binario)
        |      |
        |      └─ Comando ID: 0x01 (JOIN_REQ)
        └─ Tipo: 1 (mensaje de comando)

Payload completo: [0x81] [datos adicionales según comando]
                    ^
                    └─ Pseudoencabezado con comando JOIN_REQ
```

### Comandos Disponibles

| Comando   | Valor | Descripción                          |
|-----------|-------|--------------------------------------|
| JOIN_REQ  | 0x81  | Solicitud de unión a la red          |
| JOIN_ACK  | 0x82  | Confirmación de unión                |
| PING      | 0x83  | Mensaje de verificación de conexión  |
| PONG      | 0x84  | Respuesta a mensaje PING             |

### Ejemplo de Flujo de Asociación

```text
1. Nodo → Gateway (broadcast): [0x81] [ID_NODO] (JOIN_REQ con ID del nodo)
2. Gateway → Nodo: [0x82] [ID_GATEWAY] (JOIN_ACK con ID del gateway)
3. Nodo → Gateway (directo): [0x03] [0x20] [0x25] [0x30] (Datos de sensores: temp=32°C, hum=37%, luz=48%)
```

**Proceso detallado:**

- **Paso 1**: El nodo envía JOIN_REQ por broadcast incluyendo su propio ID para que el gateway pueda registrarlo
- **Paso 2**: El gateway responde con JOIN_ACK incluyendo su ID para que el nodo pueda comunicarse directamente con él
- **Paso 3**: Una vez establecida la asociación, el nodo envía datos directamente al gateway usando su ID

## Formato de Mensajes VAMP

### Estructura General

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|T|    Tamaño/Comando ID      |            Datos                |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Datos (cont.)                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                             ...                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Pseudoencabezado (Byte 0)

```text
 0   1   2   3   4   5   6   7
+---+---+---+---+---+---+---+---+
| T |     Tamaño/Comando ID     |
+---+---+---+---+---+---+---+---+
```

**Campos:**

- **T (Tipo)**: 1 bit
  - 0 = Mensaje de datos
  - 1 = Mensaje de comando
- **Tamaño/Comando ID**: 7 bits
  - Si T=0: Tamaño del payload de datos (0-127 bytes)
  - Si T=1: Identificador del comando (0-127)

### Mensajes de Datos (T=0)

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|0|    Tamaño (0-127)         |         Datos de Payload        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Datos de Payload (cont.)                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Mensajes de Comando (T=1)

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
|1|0 0 0 0 0 1 0|               ID del Gateway                  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          ID del Gateway (cont.)         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

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

### Limitaciones

- Tamaño máximo del payload: 32 bytes (limitación del nRF24L01)
- Tamaño del ID de nodo/gateway: 5 bytes (compatible con nRF24L01)
- Comandos disponibles: 0-127 (7 bits)

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

## TODO

Mecanismo para dirección final:

En realidad el mote no se quiere comunicar con el gateway, se quiere comunicar con un extremo, que está detrás de un IP o un dominio. De alguna manera, el gateway es solo eso, un gateway. Las posibilidades son:

- Que el gateway lo sepa todo en realidad. O sea, el nodo es tan restringido que toda la gestión de salto, a donde va el mensaje etc lo controla el gateway y el nodo solo "conoce" al gateway. Aquí el problema es el GW: supongamos que el nodo cae en el área de cobertura de 3 entidades, pero el nodo "pertenece" a la entidad X que tiene como endpoint la dirección Y. La solución es que solo el/los GW que tenga registrado ese nodo responderá a la petición de JOIN.

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

