/** 
 * 
 * 
 * 
 */

 /** @brief Inicializa el ESP8266 */
bool esp8266_init(void);

bool esp8266_init(const char * wifi_ssid, const char * wifi_password);

/** @brief Realiza una solicitud HTTP/HTTPS
 * 
 * @param profile Perfil de comunicación
 * @param data Datos a enviar
 * @param data_size Tamaño de los datos
 * @return Tamaño de los datos recibidos, 0 en caso de error
 */
size_t esp8266_http_request(const vamp_profile_t * profile, char * data, size_t data_size);