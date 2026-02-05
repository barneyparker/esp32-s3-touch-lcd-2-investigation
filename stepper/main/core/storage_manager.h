#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Initialize NVS storage
 */
esp_err_t storage_init(void);

/**
 * @brief Get string value from NVS
 *
 * @param namespace Namespace (partition) name
 * @param key Key name
 * @param out Output buffer
 * @param max_len Maximum length of output buffer
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if key doesn't exist
 */
esp_err_t storage_get_string(const char* namespace, const char* key, char* out, size_t max_len);

/**
 * @brief Set string value in NVS
 *
 * @param namespace Namespace (partition) name
 * @param key Key name
 * @param value String value to store
 * @return ESP_OK on success
 */
esp_err_t storage_set_string(const char* namespace, const char* key, const char* value);

/**
 * @brief Get 8-bit integer from NVS
 */
esp_err_t storage_get_u8(const char* namespace, const char* key, uint8_t* out);

/**
 * @brief Set 8-bit integer in NVS
 */
esp_err_t storage_set_u8(const char* namespace, const char* key, uint8_t value);

/**
 * @brief Get 32-bit integer from NVS
 */
esp_err_t storage_get_u32(const char* namespace, const char* key, uint32_t* out);

/**
 * @brief Set 32-bit integer in NVS
 */
esp_err_t storage_set_u32(const char* namespace, const char* key, uint32_t value);

/**
 * @brief Get 64-bit integer from NVS
 */
esp_err_t storage_get_u64(const char* namespace, const char* key, uint64_t* out);

/**
 * @brief Set 64-bit integer in NVS
 */
esp_err_t storage_set_u64(const char* namespace, const char* key, uint64_t value);

/**
 * @brief Get blob (binary) data from NVS
 *
 * @param namespace Namespace name
 * @param key Key name
 * @param out Output buffer
 * @param len [in/out] Length - provide max length, returns actual length
 * @return ESP_OK on success
 */
esp_err_t storage_get_blob(const char* namespace, const char* key, void* out, size_t* len);

/**
 * @brief Set blob (binary) data in NVS
 *
 * @param namespace Namespace name
 * @param key Key name
 * @param data Data to store
 * @param len Length of data
 * @return ESP_OK on success
 */
esp_err_t storage_set_blob(const char* namespace, const char* key, const void* data, size_t len);

/**
 * @brief Delete a key from NVS
 */
esp_err_t storage_delete_key(const char* namespace, const char* key);

/**
 * @brief Delete a key from NVS (convenience alias)
 */
esp_err_t storage_delete(const char* namespace, const char* key);

/**
 * @brief Delete an entire namespace from NVS
 */
esp_err_t storage_delete_namespace(const char* namespace);

/**
 * @brief Check if a key exists in NVS
 */
bool storage_key_exists(const char* namespace, const char* key);

/**
 * @brief Commit any pending writes (auto-commit is on by default)
 */
esp_err_t storage_commit(const char* namespace);

#endif // STORAGE_MANAGER_H
