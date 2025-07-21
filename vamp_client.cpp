

/** @brief VAMP client
 * Cuando el nodo se despierta no tiene idea de si esta en una red o no,
 * ni cual es el gateway, por lo que su direccion de destino es
 * la direccion de broadcast. Lo que se hace es enviar un mensaje
 * de tipo VAMP_JOIN_REQ, que es un mensaje de solicitud de unión a la red
 * y el controlador de red responderá con un mensaje de tipo
 * VAMP_JOIN_ACK, que contiene la dirección MAC del gateway.
 * Asi la dirección de destino se actualiza a la dirección del gateway
 * y se puede enviar mensajes de tipo VAMP_DATA.
 */

#include "vamp_client.h"

/* Dirección MAC del gateway por defecto, direccion de broadcast */
static uint8_t dst_mac[VAMP_ADDR_LEN] = VAMP_BROADCAST_ADDR;

/* Contador de reintentos para detectar pérdida de conexión con gateway */
static uint8_t send_failure_count = 0;
#define MAX_SEND_FAILURES 3  // Máximo de fallos consecutivos antes de re-join

/* Función para resetear la conexión con el gateway */
static void vamp_reset_connection(void) {
    /* Resetear dirección del gateway a broadcast */
    for (int i = 0; i < VAMP_ADDR_LEN; i++) {
        dst_mac[i] = 0xFF;
    }
    send_failure_count = 0;
}

bool vamp_send_data(const uint8_t * data, uint8_t len) {
    // Verificar que los datos no sean nulos y esten dentro del rango permitido
    if (data == NULL || len == 0 || len > VAMP_MAX_PAYLOAD_SIZE - 1) {
        return false;
    }

    /*  Verificar si ya se ha unido previamente, de lo contrario hay que volver a intentar
        volver a unirse al menos una vez */
    if (!vamp_is_joined()) {
        if (!vamp_join_network()) {
            /* Si no se pudo unir a la red, retornar falso */
            return false;
        }
    }

    /*  Crear mensaje de datos según el protocolo VAMP */
    uint8_t payload_buffer[VAMP_MAX_PAYLOAD_SIZE];
    uint8_t payload_len = 0;

    /*  Pseudoencabezado: T=0 (datos), resto bits = tamaño del payload
        aqui no deberia hacerse nada pues el tamaño ya se paso como argumento
        y se ha validado que es menor que VAMP_MAX_PAYLOAD_SIZE por lo que el
        bit mas significativo ya seria 0 */
    payload_buffer[payload_len++] = len;
    
    /*  Copiar los datos del payload */
    for (int i = 0; i < len; i++) {
        payload_buffer[payload_len++] = data[i];
    }

    /*  Configurar dirección de destino como la del gateway */
    //mac_dst_add(dst_mac);

    /*  Enviar el mensaje */    
    (void)payload_buffer; // Suprimir warning - será usado cuando se implemente mac_send
    if (/* !mac_send(payload_buffer, payload_len) */ 1) {
        /* Si el envío falla, incrementar contador de fallos */
        send_failure_count++;
        
        /* Si hay demasiados fallos consecutivos, resetear conexión */
        if (send_failure_count >= MAX_SEND_FAILURES) {
            vamp_reset_connection();
            
            /* Intentar re-join inmediatamente */
            if (vamp_join_network()) {
                /* Si el re-join fue exitoso, intentar enviar de nuevo */
                //mac_dst_add(dst_mac);
                if(/* mac_send(payload_buffer, payload_len) */ 1) {
                    send_failure_count = 0; // Resetear contador en caso de éxito
                    return true; // Envío exitoso
                }
            }
        }

        return false; // Fallo en el re-join o en el reenvío después de re-join
    }

    /* Envío exitoso, resetear contador de fallos */
    send_failure_count = 0;
    return true; // Envío exitoso

}

/*  ----------------------------------------------------------------- */
bool vamp_is_joined(void) {
    // Verificar si la dirección del gateway es válida (no broadcast)
    for (int i = 0; i < VAMP_ADDR_LEN; i++) {
        if (dst_mac[i] != 0xFF) {
            return true; // Si la dirección del gateway no es la de broadcast, está unido
        }
    }
    return false; // Si la dirección del gateway es la de broadcast, no está unido
}

/*  ----------------------------------------------------------------- */
bool vamp_join_network(void) {
    // Verificar si ya se ha unido previamente
    if (vamp_is_joined()) {
        return true; // Ya está unido, no es necesario volver a unirse
    }

    /*  Armar el mensaje de solicitud de unión según el protocolo VAMP */
    uint8_t join_req_payload[VAMP_ADDR_LEN + 1]; // 1 byte para el tipo de mensaje + VAMP_ADDR_LEN bytes
    uint8_t payload_len = 0;

    /*  Pseudoencabezado: T=1 (comando), Comando ID=0x01 (JOIN_REQ) */
    join_req_payload[payload_len++] = VAMP_JOIN_REQ; // 0x81

    /*  Copiar la dirección MAC local al mensaje (ID del nodo) */
    uint8_t node_mac[VAMP_ADDR_LEN];
    //mac_get_address(node_mac);
    for (int i = 0; i < VAMP_ADDR_LEN; i++) {
        join_req_payload[payload_len++] = node_mac[i];
    }

    /*  Configurar dirección de destino como broadcast para JOIN_REQ */
    //mac_dst_add(dst_mac);

    /*  Enviar el mensaje JOIN_REQ */
    if (/* !mac_send(join_req_payload, payload_len) */ 1) {
        // Manejar el error de envío
        return false;
    }

    /*  Esperar respuesta JOIN_ACK del gateway */
    //payload_len = mac_long_poll(join_req_payload);

    if (payload_len == 0) {
        // Timeout - no se recibió respuesta
        return false;
    }

    /*  Verificar que el mensaje sea JOIN_ACK (0x82) */
    if (join_req_payload[0] != VAMP_JOIN_ACK || payload_len < (1 + VAMP_ADDR_LEN)) { 
        // El mensaje recibido no es un JOIN_ACK o está incompleto
        return false;
    }

    /*  Extraer la dirección MAC del gateway desde la respuesta */
    for (int i = 0; i < VAMP_ADDR_LEN; i++) {
        dst_mac[i] = join_req_payload[i + 1]; // Copiar la dirección MAC del gateway
    }

    /*  Configurar la dirección de destino como la del gateway para futuras comunicaciones */
    //mac_dst_add(dst_mac);

    /* Resetear contador de fallos ya que tenemos nueva conexión */
    send_failure_count = 0;

    return true; // Unión exitosa
}


/*  ----------------------------------------------------------------- */
/* Función para forzar un re-join (útil para testing o recuperación manual) */
bool vamp_force_rejoin(void) {
    vamp_reset_connection();
    return vamp_join_network();
}
