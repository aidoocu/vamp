/** @file vamp.h
 * @brief VAMP Gateway header file
 *
 * This file contains definitions and function declarations for the VAMP Gateway.
 */

#include "vamp.h"
#include "vamp_gw.h"
#include "vamp_client.h"
#include "vamp_callbacks.h"

/* -------------------------------------- Gateway -------------------------------------- */

/* Initialize VAMP Gateway module (aqui una de estas funciones podria fallar y....???) */
void vamp_gw_init(char * vreg_url, char * gw_id, uint8_t * wsn_id) {

	/* Como es un gateway, siempre escucha por wsn RMODE_B */
	vamp_set_settings(VAMP_RMODE_B);

	/* Inicializar los recursos */
	vamp_gw_vreg_init(vreg_url, gw_id);

	/* Inicializar la comunicación con internet */
	vamp_iface_init();

	/* Inicializar la comunicación WSN */
	vamp_wsn_init(wsn_id);

    /* Inicializar la tabla VAMP */
    vamp_table_update();

	return;
}

void vamp_gw_sync(void) {

    /* Synchronize VAMP Gateway with VREG */
    vamp_table_update();

    /* Detect expired VAMP devices */
    vamp_detect_expired();
}

/* -------------------------------------- Client -------------------------------------- */

void vamp_client_init(uint8_t * vamp_client_id) {

	/* Inicializar la comunicación WSN */
	if (!vamp_wsn_init(vamp_client_id)) {
		Serial.println("wsn init fail");
		return;
	}

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
/* -------------------------------------- WSN -------------------------------------- */



/** ????? @todo hay que definir algun elemento que no sea correcto como una direccion nula
 00:00:00:00:00 o de broadcast o ... */
bool vamp_is_rf_id_valid(const uint8_t * rf_id) {
	// Verificar que el RF_ID no sea NULL y tenga la longitud correcta
	if (rf_id == NULL) {
		return false;
	}
	
	if (rf_id[0] == 0 && rf_id[1] == 0 && rf_id[2] == 0 &&
	    rf_id[3] == 0 && rf_id[4] == 0) {
		return false; // RF_ID nulo
	}

	if (rf_id[0] == 0xFF && rf_id[1] == 0xFF && rf_id[2] == 0xFF &&
	    rf_id[3] == 0xFF && rf_id[4] == 0xFF) {
		return false; // RF_ID de broadcast
	}

	return true;
}


