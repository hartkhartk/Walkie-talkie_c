/**
 * @file device_id.c
 * @brief מימוש מודול זיהוי מכשיר ייחודי
 */

#include "core/device_id.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// =============================================================================
// Platform-Specific Includes
// =============================================================================

#ifdef ESP32
    #include "esp_system.h"
    #include "esp_mac.h"
    #include "esp_efuse.h"
    #include "esp_flash.h"
    #include "nvs_flash.h"
    #include "nvs.h"
    #include "esp_log.h"
    #include "mbedtls/sha256.h"
    #include "mbedtls/md.h"
    #include "esp_random.h"
    
    static const char* TAG = "DEVICE_ID";
    #define LOG_INFO(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...) ESP_LOGD(TAG, fmt, ##__VA_ARGS__)
#else
    #include <stdlib.h>
    #include <time.h>
    #define LOG_INFO(fmt, ...) printf("[DEVICE_ID] " fmt "\n", ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) printf("[DEVICE_ID ERROR] " fmt "\n", ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...)
#endif

// =============================================================================
// NVS Keys
// =============================================================================

#define NVS_NAMESPACE       "device_id"
#define NVS_KEY_RAW_ID      "raw_id"
#define NVS_KEY_STRING_ID   "string_id"
#define NVS_KEY_SOURCE      "source"
#define NVS_KEY_COUNTER     "verify_cnt"

// =============================================================================
// Internal State
// =============================================================================

static bool g_initialized = false;
static device_id_t g_device_id = {0};
static uint32_t g_verify_counter = 0;

// Secret key for HMAC (should be unique per firmware build)
static const uint8_t HMAC_SECRET_KEY[32] = {
    0x57, 0x54, 0x2D, 0x50, 0x52, 0x4F, 0x2D, 0x53,
    0x45, 0x43, 0x52, 0x45, 0x54, 0x2D, 0x4B, 0x45,
    0x59, 0x2D, 0x46, 0x4F, 0x52, 0x2D, 0x48, 0x4D,
    0x41, 0x43, 0x2D, 0x32, 0x35, 0x36, 0x00, 0x00
};

// =============================================================================
// Internal Functions
// =============================================================================

static void compute_sha256(const uint8_t* data, size_t len, uint8_t* output) {
#ifdef ESP32
    mbedtls_sha256(data, len, output, 0);
#else
    // Simplified hash for simulator
    memset(output, 0, 32);
    for (size_t i = 0; i < len; i++) {
        output[i % 32] ^= data[i];
        output[(i + 1) % 32] ^= (data[i] << 4) | (data[i] >> 4);
    }
#endif
}

static void compute_hmac_sha256(const uint8_t* key, size_t key_len,
                                const uint8_t* data, size_t data_len,
                                uint8_t* output) {
#ifdef ESP32
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, key, key_len);
    mbedtls_md_hmac_update(&ctx, data, data_len);
    mbedtls_md_hmac_finish(&ctx, output);
    mbedtls_md_free(&ctx);
#else
    // Simplified HMAC for simulator
    uint8_t temp[64];
    memset(temp, 0, sizeof(temp));
    memcpy(temp, key, key_len < 32 ? key_len : 32);
    memcpy(temp + 32, data, data_len < 32 ? data_len : 32);
    compute_sha256(temp, sizeof(temp), output);
#endif
}

static void generate_random_bytes(uint8_t* buffer, size_t len) {
#ifdef ESP32
    esp_fill_random(buffer, len);
#else
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = true;
    }
    for (size_t i = 0; i < len; i++) {
        buffer[i] = (uint8_t)(rand() & 0xFF);
    }
#endif
}

// =============================================================================
// ID Conversion
// =============================================================================

void device_id_raw_to_string(const uint8_t* raw, size_t raw_size, char* output) {
    if (!raw || !output || raw_size == 0) {
        if (output) output[0] = '\0';
        return;
    }
    
    // Compute hash of raw ID
    uint8_t hash[32];
    compute_sha256(raw, raw_size, hash);
    
    // Convert to 8 decimal digits using hash
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        value = (value << 8) | hash[i];
    }
    
    // Ensure 8 digits (10000000 to 99999999)
    value = (value % 90000000) + 10000000;
    
    snprintf(output, DEVICE_ID_STRING_SIZE + 1, "%08u", value);
}

void device_id_raw_to_hex(const uint8_t* raw, size_t raw_size, char* output) {
    if (!raw || !output) {
        if (output) output[0] = '\0';
        return;
    }
    
    for (size_t i = 0; i < raw_size; i++) {
        sprintf(output + (i * 2), "%02X", raw[i]);
    }
    output[raw_size * 2] = '\0';
}

bool device_id_validate_format(const char* id) {
    if (!id) return false;
    
    size_t len = strlen(id);
    if (len != DEVICE_ID_STRING_SIZE) return false;
    
    for (size_t i = 0; i < len; i++) {
        if (!isdigit((unsigned char)id[i])) return false;
    }
    
    return true;
}

// =============================================================================
// Hardware ID Functions
// =============================================================================

bool device_get_wifi_mac(uint8_t* mac) {
    if (!mac) return false;
    
#ifdef ESP32
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    return ret == ESP_OK;
#else
    // Simulator: generate consistent fake MAC
    memset(mac, 0, 6);
    mac[0] = 0xDE;
    mac[1] = 0xAD;
    mac[2] = 0xBE;
    mac[3] = 0xEF;
    mac[4] = 0x00;
    mac[5] = 0x01;
    return true;
#endif
}

bool device_get_bt_mac(uint8_t* mac) {
    if (!mac) return false;
    
#ifdef ESP32
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_BT);
    return ret == ESP_OK;
#else
    // Simulator: generate consistent fake MAC
    memset(mac, 0, 6);
    mac[0] = 0xBE;
    mac[1] = 0xEF;
    mac[2] = 0xCA;
    mac[3] = 0xFE;
    mac[4] = 0x00;
    mac[5] = 0x02;
    return true;
#endif
}

bool device_get_efuse_uid(uint8_t* uid) {
    if (!uid) return false;
    
#if defined(ESP32) && defined(ESP_EFUSE_OPTIONAL_UNIQUE_ID)
    // ESP32-S3 has unique ID in eFuse
    esp_err_t ret = esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, uid, 8 * 8);
    return ret == ESP_OK;
#else
    // Not available, use MAC instead
    memset(uid, 0, 8);
    return false;
#endif
}

bool device_get_flash_id(uint8_t* flash_id) {
    if (!flash_id) return false;
    
#ifdef ESP32
    uint32_t id;
    esp_err_t ret = esp_flash_read_unique_chip_id(esp_flash_default_chip, (uint64_t*)flash_id);
    if (ret == ESP_OK) {
        return true;
    }
#endif
    
    memset(flash_id, 0, 8);
    return false;
}

// =============================================================================
// NVS Operations
// =============================================================================

#ifdef ESP32
static bool load_id_from_nvs(void) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        LOG_DEBUG("NVS namespace not found, will create new ID");
        return false;
    }
    
    size_t len = DEVICE_ID_RAW_SIZE;
    ret = nvs_get_blob(handle, NVS_KEY_RAW_ID, g_device_id.raw, &len);
    if (ret != ESP_OK) {
        nvs_close(handle);
        return false;
    }
    
    len = sizeof(g_device_id.string);
    nvs_get_str(handle, NVS_KEY_STRING_ID, g_device_id.string, &len);
    
    uint8_t source = 0;
    nvs_get_u8(handle, NVS_KEY_SOURCE, &source);
    g_device_id.source = (device_id_source_t)source;
    
    nvs_get_u32(handle, NVS_KEY_COUNTER, &g_verify_counter);
    
    nvs_close(handle);
    
    // Regenerate hex string
    device_id_raw_to_hex(g_device_id.raw, DEVICE_ID_RAW_SIZE, g_device_id.hex);
    
    g_device_id.is_valid = true;
    
    LOG_INFO("Loaded ID from NVS: %s", g_device_id.string);
    return true;
}

static bool save_id_to_nvs(void) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        LOG_ERROR("Failed to open NVS for writing");
        return false;
    }
    
    nvs_set_blob(handle, NVS_KEY_RAW_ID, g_device_id.raw, DEVICE_ID_RAW_SIZE);
    nvs_set_str(handle, NVS_KEY_STRING_ID, g_device_id.string);
    nvs_set_u8(handle, NVS_KEY_SOURCE, (uint8_t)g_device_id.source);
    nvs_set_u32(handle, NVS_KEY_COUNTER, g_verify_counter);
    
    ret = nvs_commit(handle);
    nvs_close(handle);
    
    if (ret == ESP_OK) {
        LOG_INFO("Saved ID to NVS");
        return true;
    }
    
    LOG_ERROR("Failed to commit NVS");
    return false;
}
#else
static bool load_id_from_nvs(void) { return false; }
static bool save_id_to_nvs(void) { return true; }
#endif

// =============================================================================
// Initialization
// =============================================================================

bool device_id_init(void) {
    if (g_initialized) {
        return true;
    }
    
    LOG_INFO("Initializing device ID module...");
    
    memset(&g_device_id, 0, sizeof(g_device_id));
    
    // Try to load from NVS first
    if (load_id_from_nvs()) {
        LOG_INFO("Using stored device ID: %s (source: %d)", 
                 g_device_id.string, g_device_id.source);
        g_initialized = true;
        return true;
    }
    
    // Generate new ID from hardware
    uint8_t mac[6];
    
    // Try WiFi MAC first
    if (device_get_wifi_mac(mac)) {
        memset(g_device_id.raw, 0, DEVICE_ID_RAW_SIZE);
        memcpy(g_device_id.raw, mac, 6);
        g_device_id.source = ID_SOURCE_MAC_WIFI;
        LOG_INFO("Using WiFi MAC as ID source");
    }
    // Try Bluetooth MAC
    else if (device_get_bt_mac(mac)) {
        memset(g_device_id.raw, 0, DEVICE_ID_RAW_SIZE);
        memcpy(g_device_id.raw, mac, 6);
        g_device_id.source = ID_SOURCE_MAC_BT;
        LOG_INFO("Using BT MAC as ID source");
    }
    // Try eFuse UID
    else if (device_get_efuse_uid(g_device_id.raw)) {
        g_device_id.source = ID_SOURCE_EFUSE;
        LOG_INFO("Using eFuse UID as ID source");
    }
    // Fall back to random ID
    else {
        generate_random_bytes(g_device_id.raw, DEVICE_ID_RAW_SIZE);
        g_device_id.source = ID_SOURCE_NVS_RANDOM;
        LOG_INFO("Using random ID (stored in NVS)");
    }
    
    // Generate string and hex representations
    device_id_raw_to_string(g_device_id.raw, DEVICE_ID_RAW_SIZE, g_device_id.string);
    device_id_raw_to_hex(g_device_id.raw, DEVICE_ID_RAW_SIZE, g_device_id.hex);
    
    g_device_id.is_valid = true;
    
    // Save to NVS for consistency
    save_id_to_nvs();
    
    LOG_INFO("Device ID: %s", g_device_id.string);
    LOG_INFO("Device ID (hex): %s", g_device_id.hex);
    
    g_initialized = true;
    return true;
}

// =============================================================================
// Get Functions
// =============================================================================

bool device_id_get(device_id_t* id) {
    if (!id || !g_initialized) {
        return false;
    }
    
    memcpy(id, &g_device_id, sizeof(device_id_t));
    return true;
}

bool device_id_get_string(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0 || !g_initialized) {
        return false;
    }
    
    strncpy(buffer, g_device_id.string, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return true;
}

bool device_id_get_hex(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0 || !g_initialized) {
        return false;
    }
    
    strncpy(buffer, g_device_id.hex, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return true;
}

bool device_id_get_raw(uint8_t* buffer) {
    if (!buffer || !g_initialized) {
        return false;
    }
    
    memcpy(buffer, g_device_id.raw, DEVICE_ID_RAW_SIZE);
    return true;
}

device_id_source_t device_id_get_source(void) {
    return g_device_id.source;
}

bool device_id_is_valid(void) {
    return g_initialized && g_device_id.is_valid;
}

// =============================================================================
// Generation
// =============================================================================

bool device_id_generate(bool force_regenerate) {
    if (!force_regenerate && g_initialized && g_device_id.is_valid) {
        LOG_INFO("ID already exists, not regenerating");
        return true;
    }
    
    LOG_INFO("Generating new device ID...");
    
    // Force new random ID
    generate_random_bytes(g_device_id.raw, DEVICE_ID_RAW_SIZE);
    g_device_id.source = ID_SOURCE_NVS_RANDOM;
    
    device_id_raw_to_string(g_device_id.raw, DEVICE_ID_RAW_SIZE, g_device_id.string);
    device_id_raw_to_hex(g_device_id.raw, DEVICE_ID_RAW_SIZE, g_device_id.hex);
    
    g_device_id.is_valid = true;
    
    save_id_to_nvs();
    
    LOG_INFO("Generated new ID: %s", g_device_id.string);
    return true;
}

bool device_id_set_custom(const char* id) {
    if (!id || !device_id_validate_format(id)) {
        LOG_ERROR("Invalid custom ID format");
        return false;
    }
    
    LOG_INFO("Setting custom ID: %s", id);
    
    strncpy(g_device_id.string, id, DEVICE_ID_STRING_SIZE);
    g_device_id.string[DEVICE_ID_STRING_SIZE] = '\0';
    
    // Store the string in raw as well (for consistency)
    memset(g_device_id.raw, 0, DEVICE_ID_RAW_SIZE);
    for (int i = 0; i < DEVICE_ID_STRING_SIZE; i++) {
        g_device_id.raw[i] = id[i];
    }
    
    device_id_raw_to_hex(g_device_id.raw, DEVICE_ID_RAW_SIZE, g_device_id.hex);
    
    g_device_id.source = ID_SOURCE_CUSTOM;
    g_device_id.is_valid = true;
    
    save_id_to_nvs();
    
    return true;
}

// =============================================================================
// Verification
// =============================================================================

bool device_id_create_verification(device_verification_t* verification) {
    if (!verification || !g_initialized) {
        return false;
    }
    
    memset(verification, 0, sizeof(device_verification_t));
    
    // Create data to sign: ID + timestamp + counter
#ifdef ESP32
    verification->timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
#else
    verification->timestamp = (uint32_t)time(NULL);
#endif
    verification->counter = ++g_verify_counter;
    
    // Create signature
    uint8_t data[DEVICE_ID_RAW_SIZE + 8];
    memcpy(data, g_device_id.raw, DEVICE_ID_RAW_SIZE);
    memcpy(data + DEVICE_ID_RAW_SIZE, &verification->timestamp, 4);
    memcpy(data + DEVICE_ID_RAW_SIZE + 4, &verification->counter, 4);
    
    compute_hmac_sha256(HMAC_SECRET_KEY, sizeof(HMAC_SECRET_KEY),
                        data, sizeof(data),
                        verification->signature);
    
    verification->is_verified = true;
    
    // Update counter in NVS
    save_id_to_nvs();
    
    return true;
}

bool device_id_verify(const device_verification_t* verification) {
    if (!verification || !g_initialized) {
        return false;
    }
    
    // Recreate signature
    uint8_t data[DEVICE_ID_RAW_SIZE + 8];
    memcpy(data, g_device_id.raw, DEVICE_ID_RAW_SIZE);
    memcpy(data + DEVICE_ID_RAW_SIZE, &verification->timestamp, 4);
    memcpy(data + DEVICE_ID_RAW_SIZE + 4, &verification->counter, 4);
    
    uint8_t expected[32];
    compute_hmac_sha256(HMAC_SECRET_KEY, sizeof(HMAC_SECRET_KEY),
                        data, sizeof(data),
                        expected);
    
    // Constant-time comparison
    int diff = 0;
    for (int i = 0; i < 32; i++) {
        diff |= verification->signature[i] ^ expected[i];
    }
    
    return diff == 0;
}

bool device_id_create_auth_token(char* buffer, size_t buffer_size, uint32_t timestamp) {
    if (!buffer || buffer_size < 80 || !g_initialized) {
        return false;
    }
    
    // Token format: ID.TIMESTAMP.SIGNATURE_HEX
    uint8_t data[DEVICE_ID_STRING_SIZE + 4];
    memcpy(data, g_device_id.string, DEVICE_ID_STRING_SIZE);
    memcpy(data + DEVICE_ID_STRING_SIZE, &timestamp, 4);
    
    uint8_t sig[32];
    compute_hmac_sha256(HMAC_SECRET_KEY, sizeof(HMAC_SECRET_KEY),
                        data, sizeof(data), sig);
    
    // Format token
    char sig_hex[17];
    for (int i = 0; i < 8; i++) {
        sprintf(sig_hex + (i * 2), "%02x", sig[i]);
    }
    sig_hex[16] = '\0';
    
    snprintf(buffer, buffer_size, "%s.%u.%s", 
             g_device_id.string, timestamp, sig_hex);
    
    return true;
}

bool device_id_verify_auth_token(const char* token, const char* expected_id, uint32_t max_age_seconds) {
    if (!token || !expected_id) {
        return false;
    }
    
    // Parse token
    char id[DEVICE_ID_STRING_SIZE + 1];
    uint32_t timestamp;
    char sig_hex[17];
    
    if (sscanf(token, "%8[0-9].%u.%16s", id, &timestamp, sig_hex) != 3) {
        return false;
    }
    
    // Check ID matches
    if (strcmp(id, expected_id) != 0) {
        return false;
    }
    
    // Check timestamp age
#ifdef ESP32
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000);
#else
    uint32_t now = (uint32_t)time(NULL);
#endif
    
    if (now > timestamp && (now - timestamp) > max_age_seconds) {
        return false;
    }
    
    // Verify signature
    uint8_t data[DEVICE_ID_STRING_SIZE + 4];
    memcpy(data, id, DEVICE_ID_STRING_SIZE);
    memcpy(data + DEVICE_ID_STRING_SIZE, &timestamp, 4);
    
    uint8_t expected_sig[32];
    compute_hmac_sha256(HMAC_SECRET_KEY, sizeof(HMAC_SECRET_KEY),
                        data, sizeof(data), expected_sig);
    
    // Compare first 8 bytes
    char expected_hex[17];
    for (int i = 0; i < 8; i++) {
        sprintf(expected_hex + (i * 2), "%02x", expected_sig[i]);
    }
    expected_hex[16] = '\0';
    
    return strcmp(sig_hex, expected_hex) == 0;
}

