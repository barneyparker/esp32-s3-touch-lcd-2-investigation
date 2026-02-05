#include "storage_manager.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "storage_mgr";

esp_err_t storage_init(void)
{
  // Initialize default NVS partition
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    // NVS partition was truncated and needs to be erased
    ESP_LOGW(TAG, "Erasing NVS partition due to: %s", esp_err_to_name(ret));
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }

  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "NVS initialized successfully");
  return ESP_OK;
}

esp_err_t storage_get_string(const char *namespace, const char *key, char *out, size_t max_len)
{
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READONLY, &handle);
  if (ret != ESP_OK)
  {
    ESP_LOGD(TAG, "Cannot open namespace '%s': %s", namespace, esp_err_to_name(ret));
    return ret;
  }

  ret = nvs_get_str(handle, key, out, &max_len);
  nvs_close(handle);

  if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND)
  {
    ESP_LOGW(TAG, "Error reading string '%s' from '%s': %s", key, namespace, esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t storage_set_string(const char *namespace, const char *key, const char *value)
{
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &handle);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Cannot open namespace '%s' for writing: %s", namespace, esp_err_to_name(ret));
    return ret;
  }

  ret = nvs_set_str(handle, key, value);
  if (ret == ESP_OK)
  {
    ret = nvs_commit(handle);
  }

  nvs_close(handle);

  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Error writing string '%s' to '%s': %s", key, namespace, esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t storage_get_u8(const char *namespace, const char *key, uint8_t *out)
{
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READONLY, &handle);
  if (ret != ESP_OK)
  {
    ESP_LOGD(TAG, "Cannot open namespace '%s': %s", namespace, esp_err_to_name(ret));
    return ret;
  }

  ret = nvs_get_u8(handle, key, out);
  nvs_close(handle);

  if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND)
  {
    ESP_LOGW(TAG, "Error reading u8 '%s' from '%s': %s", key, namespace, esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t storage_set_u8(const char *namespace, const char *key, uint8_t value)
{
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &handle);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Cannot open namespace '%s' for writing: %s", namespace, esp_err_to_name(ret));
    return ret;
  }

  ret = nvs_set_u8(handle, key, value);
  if (ret == ESP_OK)
  {
    ret = nvs_commit(handle);
  }

  nvs_close(handle);

  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Error writing u8 '%s' to '%s': %s", key, namespace, esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t storage_get_u32(const char *namespace, const char *key, uint32_t *out)
{
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READONLY, &handle);
  if (ret != ESP_OK)
  {
    ESP_LOGD(TAG, "Cannot open namespace '%s': %s", namespace, esp_err_to_name(ret));
    return ret;
  }

  ret = nvs_get_u32(handle, key, out);
  nvs_close(handle);

  if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND)
  {
    ESP_LOGW(TAG, "Error reading u32 '%s' from '%s': %s", key, namespace, esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t storage_set_u32(const char *namespace, const char *key, uint32_t value)
{
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &handle);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Cannot open namespace '%s' for writing: %s", namespace, esp_err_to_name(ret));
    return ret;
  }

  ret = nvs_set_u32(handle, key, value);
  if (ret == ESP_OK)
  {
    ret = nvs_commit(handle);
  }

  nvs_close(handle);

  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Error writing u32 '%s' to '%s': %s", key, namespace, esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t storage_get_u64(const char *namespace, const char *key, uint64_t *out)
{
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READONLY, &handle);
  if (ret != ESP_OK)
  {
    ESP_LOGD(TAG, "Cannot open namespace '%s': %s", namespace, esp_err_to_name(ret));
    return ret;
  }

  ret = nvs_get_u64(handle, key, out);
  nvs_close(handle);

  if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND)
  {
    ESP_LOGW(TAG, "Error reading u64 '%s' from '%s': %s", key, namespace, esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t storage_set_u64(const char *namespace, const char *key, uint64_t value)
{
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &handle);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Cannot open namespace '%s' for writing: %s", namespace, esp_err_to_name(ret));
    return ret;
  }

  ret = nvs_set_u64(handle, key, value);
  if (ret == ESP_OK)
  {
    ret = nvs_commit(handle);
  }

  nvs_close(handle);

  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Error writing u64 '%s' to '%s': %s", key, namespace, esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t storage_get_blob(const char *namespace, const char *key, void *out, size_t *len)
{
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READONLY, &handle);
  if (ret != ESP_OK)
  {
    ESP_LOGD(TAG, "Cannot open namespace '%s': %s", namespace, esp_err_to_name(ret));
    return ret;
  }

  ret = nvs_get_blob(handle, key, out, len);
  nvs_close(handle);

  if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND)
  {
    ESP_LOGW(TAG, "Error reading blob '%s' from '%s': %s", key, namespace, esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t storage_set_blob(const char *namespace, const char *key, const void *data, size_t len)
{
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &handle);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Cannot open namespace '%s' for writing: %s", namespace, esp_err_to_name(ret));
    return ret;
  }

  ret = nvs_set_blob(handle, key, data, len);
  if (ret == ESP_OK)
  {
    ret = nvs_commit(handle);
  }

  nvs_close(handle);

  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Error writing blob '%s' to '%s': %s", key, namespace, esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t storage_delete_key(const char *namespace, const char *key)
{
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &handle);
  if (ret != ESP_OK)
  {
    ESP_LOGD(TAG, "Cannot open namespace '%s': %s", namespace, esp_err_to_name(ret));
    return ret;
  }

  ret = nvs_erase_key(handle, key);
  if (ret == ESP_OK)
  {
    ret = nvs_commit(handle);
  }

  nvs_close(handle);

  return ret;
}

esp_err_t storage_delete_namespace(const char *namespace)
{
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &handle);
  if (ret != ESP_OK)
  {
    ESP_LOGD(TAG, "Cannot open namespace '%s': %s", namespace, esp_err_to_name(ret));
    return ret;
  }

  ret = nvs_erase_all(handle);
  if (ret == ESP_OK)
  {
    ret = nvs_commit(handle);
  }

  nvs_close(handle);

  return ret;
}

// Alias for delete_key for convenience
esp_err_t storage_delete(const char *namespace, const char *key)
{
  return storage_delete_key(namespace, key);
}

bool storage_key_exists(const char *namespace, const char *key)
{
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READONLY, &handle);
  if (ret != ESP_OK)
  {
    return false;
  }

  nvs_type_t type;
  ret = nvs_find_key(handle, key, &type);
  nvs_close(handle);

  return ret == ESP_OK;
}

esp_err_t storage_commit(const char *namespace)
{
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &handle);
  if (ret != ESP_OK)
  {
    return ret;
  }

  ret = nvs_commit(handle);
  nvs_close(handle);

  return ret;
}
