/** 
 * 
 * 
 * 
 */

 /** @brief Inicializa el ESP8266 */
bool esp8266_init(void);

/** @brief Realiza una solicitud HTTP/HTTPS
 * 
 * @param profile Perfil de comunicación
 * @param data Datos a enviar
 * @param data_size Tamaño de los datos
 * @return true si la solicitud fue exitosa, false en caso contrario
 */
bool esp8266_http_request(const vamp_profile_t * profile, char * data, size_t data_size);