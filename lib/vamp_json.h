/** @brief 
 * 
 * 
 * 
 * 
 * 
*/

#ifndef _VAMP_JSON_H_


/** @brief respuesta de sincronización
 * actions:
 * - "ADD": agregar un dispositivo. El VREG no sabe ni debe intervenir en el puerto, 
 *          así que el gateway busca un slot vacío y asigna el nuevo nodo. Se revisa 
 *          primero si el nodo ya estaba en la tabla.
 * - "REMOVE": eliminar un dispositivo, se cambia el estado a libre y se pone a cero el rf_id
 * - "UPDATE": actualizar un dispositivo existente
 * @param json_data: puntero a los datos JSON de la respuesta
 * @return true si la respuesta es válida, false en caso contrario
 */
bool vamp_process_sync_json_response(const char* json_data);


#endif /* _VAMP_JSON_H_ */
