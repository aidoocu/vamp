

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
static uint8_t gw_rf_id[VAMP_ADDR_LEN] = VAMP_BROADCAST_ADDR;

/* Dirección MAC local del cliente */
static uint8_t local_rf_id[VAMP_ADDR_LEN];
/* Es como nos conoce el gateway 3 bits de verificación + 5 bits de dirección */
static uint8_t id_in_gateway = 0;

/* Callback para la comunicación WSN */
static vamp_wsn_callback_t wsn_comm_callback = NULL;

/* Contador de reintentos para detectar pérdida de conexión con gateway */
static uint8_t send_failure_count = 0;
#define MAX_SEND_FAILURES 3  // Máximo de fallos consecutivos antes de re-join

/** @brief Buffer para almacenar datos de envío y recepción */
static uint8_t req_resp_wsn_buff[VAMP_MAX_PAYLOAD_SIZE];


/* Función para resetear la conexión con el gateway */
void vamp_reset_connection(void) {
	/* Resetear dirección del gateway a broadcast */
	for (int i = 0; i < VAMP_ADDR_LEN; i++) {
		gw_rf_id[i] = 0xFF;
	}
	/* Resetear contador de fallos */
	send_failure_count = 0;
	id_in_gateway = 0;
}


void vamp_local_client_init(const uint8_t * vamp_client_id, vamp_wsn_callback_t wsn_callback) {

	/* Copiar el ID del cliente a la dirección local */
	memcpy(local_rf_id, vamp_client_id, VAMP_ADDR_LEN);

	/* Asignar callback */
	wsn_comm_callback = wsn_callback;

	Serial.println("Cliente VAMP inicializado con ID:");
	for (int i = 0; i < VAMP_ADDR_LEN; i++) {
		Serial.print(local_rf_id[i], HEX);
		if (i < VAMP_ADDR_LEN - 1) {
			Serial.print(":");
		}
	}
	Serial.println();

	/* Buscar la red VAMP */
	vamp_join_network();

}


bool vamp_send_data(const uint8_t * data, uint8_t len) {
	/* Verificar que los datos no sean nulos y esten dentro del rango permitido */
	if (data == NULL || len == 0 || len > VAMP_MAX_PAYLOAD_SIZE - 2) { // -2 para el encabezado
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
	uint8_t payload_len = 0;

	/*  Pseudoencabezado: T=0 (datos), resto bits = tamaño del payload
		aqui no deberia hacerse nada pues el tamaño ya se paso como argumento
		y se ha validado que es menor que VAMP_MAX_PAYLOAD_SIZE por lo que el
		bit mas significativo ya seria 0 */
	req_resp_wsn_buff[payload_len++] = len;

	/* Copiar el identificador en el GW al segundo byte */
	req_resp_wsn_buff[payload_len++] = id_in_gateway;

	/*  Copiar los datos del payload */
	for (int i = 0; i < len; i++) {
		req_resp_wsn_buff[payload_len++] = data[i];
	}

	/*  Enviar el mensaje */    
	payload_len = wsn_comm_callback(gw_rf_id, VAMP_TELL, req_resp_wsn_buff, payload_len);
	
	if (payload_len == 0) {
		/* Si el envío falla, incrementar contador de fallos */
		send_failure_count++;
		
		/* Si hay demasiados fallos consecutivos, resetear conexión */
		if (send_failure_count >= MAX_SEND_FAILURES) {
			vamp_reset_connection();
			
			/* Intentar re-join inmediatamente */
			if (vamp_join_network()) {
				/* Si el re-join fue exitoso, intentar enviar de nuevo */
				//mac_dst_add(gw_rf_id);
				if(/* mac_send(payload_buffer, payload_len) */ 1) {
					send_failure_count = 0; // Resetear contador en caso de éxito
					return true; // Envío exitoso
				}
			}
		}

		return false; // Fallo en el re-join o en el reenvío después de re-join
	}

	// Aqui se asume que el envío fue exitoso pero es probable que el gateway quiera decirnos algo
	// por lo que el payload_len debe sera mayor que 0 y hay que procesar la respuesta
	/// ...

	/* Envío exitoso, resetear contador de fallos */
	send_failure_count = 0;
	return true; // Envío exitoso

}

/*  ----------------------------------------------------------------- */
bool vamp_is_joined(void) {
	// Verificar si la dirección del gateway es válida (no broadcast)
	for (int i = 0; i < VAMP_ADDR_LEN; i++) {
		if (gw_rf_id[i] != 0xFF) {
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

	uint8_t payload_len = 0;

	/*  Pseudoencabezado: T=1 (comando), Comando ID=0x01 (JOIN_REQ) */
	req_resp_wsn_buff[payload_len++] = VAMP_JOIN_REQ | VAMP_IS_CMD_MASK; // 0x81

	for (int i = 0; i < VAMP_ADDR_LEN; i++) {
		req_resp_wsn_buff[payload_len++] = local_rf_id[i];
	}

	Serial.print("Join message:");
	for (int i = 0; i < payload_len; i++) {
		Serial.print(req_resp_wsn_buff[i], HEX);
		if (i < payload_len - 1) {
			Serial.print(":");
		}
	}
	Serial.println();

	/* Resetear la conexión */
	vamp_reset_connection();

	/*  Enviar mensaje de unión al gateway */
	payload_len = wsn_comm_callback(gw_rf_id, VAMP_TELL, req_resp_wsn_buff, payload_len);
	/* El mensaje recibido debe ser un JOIN_ACK (0x82) + ID_IN_GW (1 byte) + dirección del gateway (5 bytes) */
	if (!payload_len || req_resp_wsn_buff[0] != VAMP_JOIN_ACK || payload_len < (2 + VAMP_ADDR_LEN)) {
		Serial.println("Error al enviar JOIN_REQ o respuesta inválida");
		return false;
	}

	/* Extraer el ID en el gateway desde la respuesta */
	id_in_gateway = req_resp_wsn_buff[1];

	/*  Extraer la dirección MAC del gateway desde la respuesta */
	for (int i = 0; i < VAMP_ADDR_LEN; i++) {
		gw_rf_id[i] = req_resp_wsn_buff[i + 2]; // Copiar la dirección MAC del gateway

		/* Verificar que la dirección del gateway sea válida */
		if (!vamp_is_rf_id_valid(gw_rf_id)) {
			Serial.println("Dirección del gateway inválida");

			/* 	Resetear conexión si la dirección es inválida para mantener los chequeos
				consistentes */
			vamp_reset_connection();
			return false;
		}
	}

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
