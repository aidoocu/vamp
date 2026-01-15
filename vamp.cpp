/** @file vamp.h
 * @brief VAMP Gateway header file
 *
 * This file contains definitions and function declarations for the VAMP Gateway.
 */

#include "vamp.h"
#include "vamp_gw.h"
#include "vamp_client.h"
#include "vamp_callbacks.h"
#include "lib/vamp_table.h"

/** @todo hay que establecer algun control para cuando alguna de las interfaces 
 * 	no esten disponibles */

/* -------------------------------------- Gateway -------------------------------------- */

/* Initialize VAMP Gateway module (aqui una de estas funciones podria fallar y....???) */
/* ToDo la estructura gw_config parece que la guarda la app del GW y deberia (o no, hay que ver)
estar disponible desde dentro de la implementacion del gateway
Probablemente el gateway solo sea posible como una app completa, evaluar!!!
*/
void vamp_gw_init(const gw_config_t * gw_config) {

	/* Como es un gateway, siempre escucha por wsn RMODE_B */
	vamp_set_settings(VAMP_RMODE_B);

	/* Inicializar los recursos */
	if(!vamp_gw_vreg_init(gw_config)){
		/* Si hay un problema chequeando o asignando recursos no se puede seguir */
		return;
	}

	#ifdef VAMP_DEBUG
	printf("[GW] VAMP Gateway initialized\n");
	#endif /* VAMP_DEBUG */

	/* Inicializar la comunicación con internet */
	vamp_iface_init(gw_config);

	#ifdef VAMP_DEBUG
	printf("[GW] Internet inface initialized\n");
	#endif /* VAMP_DEBUG */

	/* ToDo aqui hay que validar que efectivamente halla un ID de gateway 
	y en caso de no haberlo hay que tomar una decision. Podrria ser utlizar
	uno ya definido para todo gateway desconfiurado a al asi */
	/* Obtener el RF_ID del gateway desde la configuración */
	uint8_t nrf_id[5] = {0};
	hex_to_rf_id((const char*)gw_config->vamp.gw_id, nrf_id);

	/* Inicializar la comunicación WSN */
	vamp_wsn_init(nrf_id);

	#ifdef VAMP_DEBUG
	printf("[GW] WSN interface initialized\n");
	#endif /* VAMP_DEBUG */

	/* Inicializar la tabla VAMP */
    vamp_table_init();

	printf("[TABLE]TABLE INIT AFTER\n");
	//printf("{MEM} free heap: %d B\n", ESP.getFreeHeap());
	printf("{MEM} frag. Heap: %d%%\n", ESP.getHeapFragmentation());
	printf("{MEM} ---- max free block: %d B\n", ESP.getMaxFreeBlockSize());

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