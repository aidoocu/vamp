/** @file vamp.h
 * @brief VAMP Gateway header file
 *
 * This file contains definitions and function declarations for the VAMP Gateway.
 */

#include "vamp.h"
#include "vamp_gw.h"
#include "vamp_client.h"
#include "vamp_callbacks.h"

/** @todo hay que establecer algun control para cuando alguna de las interfaces 
 * 	no esten disponibles */

/* -------------------------------------- Gateway -------------------------------------- */

/* Initialize VAMP Gateway module (aqui una de estas funciones podria fallar y....???) */
void vamp_gw_init(const gw_config_t * gw_config, uint8_t * wsn_id) {

	/* Como es un gateway, siempre escucha por wsn RMODE_B */
	vamp_set_settings(VAMP_RMODE_B);

	/* Inicializar los recursos */
	vamp_gw_vreg_init(gw_config->vamp.vreg_resource.c_str(), gw_config->vamp.gw_id.c_str());

	/* Inicializar la comunicación con internet */
	vamp_iface_init(gw_config);

	/* Inicializar la comunicación WSN */
	vamp_wsn_init(wsn_id);

    /* Inicializar la tabla VAMP */
    vamp_table_init();

	return;
}

void vamp_gw_sync(void) {

	vamp_table_sync();
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