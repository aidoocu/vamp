

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
#include "vamp_callbacks.h"

/* Dirección MAC del gateway por defecto, direccion de broadcast */
static uint8_t vamp_gw_addr[VAMP_ADDR_LEN] = VAMP_BROADCAST_ADDR;

/* Es como nos conoce el gateway 3 bits de verificación + 5 bits de dirección */
static uint8_t id_in_gateway = 0;

/* Contador de reintentos para detectar pérdida de conexión con gateway */
static uint8_t send_failure_count = 0;
#define MAX_SEND_FAILURES 3  // Máximo de fallos consecutivos antes de re-join

/** @brief Buffer para almacenar datos de envío y recepción */
static uint8_t req_resp_wsn_buff[VAMP_MAX_PAYLOAD_SIZE];


void vamp_client_init(uint8_t * vamp_client_id) {

	/* El modo de radio RMODE_A esta por defecto */


	/* Inicializar la comunicación WSN */
	vamp_wsn_init(vamp_client_id);


 	Serial.println("vclient id:");
	uint8_t * local_wsn_addr = vamp_get_local_wsn_addr();
	for (int i = 0; i < VAMP_ADDR_LEN; i++) {
		Serial.print(local_wsn_addr[i], HEX);
		if (i < VAMP_ADDR_LEN - 1) {
			Serial.print(":");
		}
	}
	Serial.println();

    /* Se intenta unir a la red VAMP */
    vamp_join_network();

}


/* Función para resetear la conexión con el gateway */
void vamp_reset_connection(void) {
	/* Resetear dirección del gateway a broadcast */
	for (int i = 0; i < VAMP_ADDR_LEN; i++) {
		vamp_gw_addr[i] = 0xFF;
	}
	/* Resetear contador de fallos */
	send_failure_count = 0;
	id_in_gateway = 0;
}

/*  ----------------------------------------------------------------- */
bool vamp_is_joined(void) {
	// Verificar si la dirección del gateway es válida (no broadcast)
	for (int i = 0; i < VAMP_ADDR_LEN; i++) {
		if (vamp_gw_addr[i] != 0xFF) {
			return true; // Si la dirección del gateway no es la de broadcast, está unido
		}
	}
	return false; // Si la dirección del gateway es la de broadcast, no está unido
}

/*  ----------------------------------------------------------------- */
/* Función para forzar un re-join (útil para testing o recuperación manual) */
bool vamp_force_rejoin(void) {
	vamp_reset_connection();
	return vamp_join_network();
}

/*  ----------------------------------------------------------------- */
bool vamp_join_network(void) {
	// Verificar si ya se ha unido previamente
	if (vamp_is_joined()) {
		return true; // Ya está unido, no es necesario volver a unirse
	}

	/* Resetear la conexión */
	vamp_reset_connection();

	uint8_t payload_len = 0;

	/*  Pseudoencabezado: T=1 (comando), Comando ID=0x01 (JOIN_REQ) = 0x81 */
	req_resp_wsn_buff[payload_len++] = VAMP_JOIN_REQ | VAMP_IS_CMD_MASK;

	/* Copiar dirección local */
	uint8_t * local_wsn_addr = vamp_get_local_wsn_addr();
	for (int i = 0; i < VAMP_ADDR_LEN; i++) {
		req_resp_wsn_buff[payload_len++] = local_wsn_addr[i];
	}

	Serial.print("Join message:");
	for (int i = 0; i < payload_len; i++) {
		Serial.print(req_resp_wsn_buff[i], HEX);
		if (i < payload_len - 1) {
			Serial.print(":");
		}
	}
	Serial.println();

	/*  Enviar mensaje de unión al gateway */
	payload_len = vamp_wsn_comm(vamp_gw_addr, req_resp_wsn_buff, payload_len);
	/* El mensaje recibido debe ser un JOIN_ACK (0x82) + ID_IN_GW (1 byte) + dirección del gateway (5 bytes) */
	if ((!payload_len) || (req_resp_wsn_buff[0] != (VAMP_JOIN_ACK | VAMP_IS_CMD_MASK)) || (payload_len != (2 + VAMP_ADDR_LEN))) {
		Serial.println("JOIN_REQ failed");
		return false;
	}

	/* Extraer el ID en el gateway desde la respuesta */
	id_in_gateway = req_resp_wsn_buff[1];

	/*  Extraer la dirección MAC del gateway desde la respuesta */
	for (int i = 0; i < VAMP_ADDR_LEN; i++) {
		vamp_gw_addr[i] = req_resp_wsn_buff[i + 2]; // Copiar la dirección MAC del gateway

		/* Verificar que la dirección del gateway sea válida */
		if (!vamp_is_rf_id_valid(vamp_gw_addr)) {
			Serial.println("gw addr invalid");

			/* 	Resetear conexión si la dirección es inválida para mantener los chequeos
				consistentes */
			vamp_reset_connection();
			return false;
		}
	}

	Serial.print("joined: ");
	Serial.print(id_in_gateway);
	Serial.print("-");
	for (int i = 0; i < VAMP_ADDR_LEN; i++) {
		Serial.print(vamp_gw_addr[i], HEX);
		if (i < VAMP_ADDR_LEN - 1) {
			Serial.print(":");
		}
	}
	Serial.println();

	/* Resetear contador de fallos ya que tenemos nueva conexión */
	send_failure_count = 0;

	return true; // Unión exitosa
}

/*  ----------------------------------------------------------------- */

uint8_t vamp_client_send(const uint8_t * data, uint8_t len) {
	/* Verificar que los datos no sean nulos y esten dentro del rango permitido */
	if (data == NULL || len == 0 || len > VAMP_MAX_PAYLOAD_SIZE - 2) { // -2 para el encabezado
		return 0;
	}

	/*  Verificar si ya se ha unido previamente, de lo contrario hay que volver a intentar
		volver a unirse al menos una vez */
	if (!vamp_is_joined()) {
		if (!vamp_join_network()) {
			/* Si no se pudo unir a la red, retornar falso */
			return 0;
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
	len = vamp_wsn_comm(vamp_gw_addr, req_resp_wsn_buff, payload_len);

	if (len == 0) {
		/* Si el envío falla, incrementar contador de fallos */
		send_failure_count++;
		
		/* Si hay demasiados fallos consecutivos, resetear conexión */
		if (send_failure_count >= MAX_SEND_FAILURES) {
			vamp_reset_connection();
			
			/* Intentar re-join inmediatamente */
			if (vamp_join_network()) {
				/* Si el re-join fue exitoso, intentar enviar de nuevo */
				len = vamp_wsn_comm(vamp_gw_addr, req_resp_wsn_buff, payload_len);
				if(len > 0) {
					return len; // Envío exitoso
				}
			}
		}

		return 0; // Fallo en el re-join o en el reenvío después de re-join
	}

	/* Envío exitoso, resetear contador de fallos */
	send_failure_count = 0;
	return len; // Envío exitoso

}



//TODO: ESt funcion no cantrola si hay o no conexion, debera unirsr a la funcion send
uint8_t vamp_client_poll(uint8_t * data, uint8_t len) {
	/* Simplemente enviar el comando de poll */

	/*  Crear mensaje de datos según el protocolo VAMP */
	uint8_t payload_len = 0;

	/*  Pseudoencabezado: T=0 (datos), resto bits = tamaño del payload
		aqui no deberia hacerse nada pues el tamaño ya se paso como argumento
		y se ha validado que es menor que VAMP_MAX_PAYLOAD_SIZE por lo que el
		bit mas significativo ya seria 0 */
	req_resp_wsn_buff[payload_len++] = (VAMP_POLL | VAMP_IS_CMD_MASK);

	/* Copiar el identificador en el GW al segundo byte */
	req_resp_wsn_buff[payload_len++] = id_in_gateway;

	/*  Enviar el mensaje */    
	return vamp_wsn_comm(vamp_gw_addr, req_resp_wsn_buff, payload_len);

}