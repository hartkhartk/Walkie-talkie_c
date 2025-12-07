/**
 * @file device_id.h
 * @brief מודול זיהוי מכשיר ייחודי
 * 
 * יוצר מזהה ייחודי וקבוע למכשיר המבוסס על:
 * - ESP32: כתובת MAC של WiFi/BT או eFuse UID
 * - ESP32-S3: eFuse UID
 * - אחר: מזהה אקראי שנשמר ב-NVS
 */

#ifndef CORE_DEVICE_ID_H
#define CORE_DEVICE_ID_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// Constants
// =============================================================================

#define DEVICE_ID_RAW_SIZE      16      // גודל מזהה גולמי (bytes)
#define DEVICE_ID_STRING_SIZE   8       // גודל מזהה כמחרוזת ספרות
#define DEVICE_ID_HEX_SIZE      32      // גודל מזהה כ-hex string

// =============================================================================
// ID Source Types
// =============================================================================

typedef enum {
    ID_SOURCE_UNKNOWN = 0,
    ID_SOURCE_MAC_WIFI,         // כתובת MAC של WiFi
    ID_SOURCE_MAC_BT,           // כתובת MAC של Bluetooth
    ID_SOURCE_EFUSE,            // eFuse UID (ESP32-S3)
    ID_SOURCE_FLASH,            // מזהה של ה-Flash
    ID_SOURCE_NVS_RANDOM,       // מזהה אקראי שנשמר ב-NVS
    ID_SOURCE_CUSTOM            // מזהה מותאם אישית
} device_id_source_t;

// =============================================================================
// Device ID Structure
// =============================================================================

typedef struct {
    uint8_t raw[DEVICE_ID_RAW_SIZE];    // מזהה גולמי
    char    string[DEVICE_ID_STRING_SIZE + 1];  // מזהה כ-8 ספרות
    char    hex[DEVICE_ID_HEX_SIZE + 1];        // מזהה כ-hex
    device_id_source_t source;          // מקור המזהה
    bool    is_valid;                   // האם תקף
} device_id_t;

// =============================================================================
// Verification Structure
// =============================================================================

typedef struct {
    uint8_t signature[32];      // חתימה SHA-256
    uint32_t timestamp;         // זמן יצירה
    uint32_t counter;           // מונה אימותים
    bool is_verified;           // האם אומת
} device_verification_t;

// =============================================================================
// API Functions
// =============================================================================

/**
 * @brief אתחול מודול זיהוי המכשיר
 * @return true בהצלחה
 */
bool device_id_init(void);

/**
 * @brief קבלת מזהה המכשיר
 * @param id מבנה פלט
 * @return true בהצלחה
 */
bool device_id_get(device_id_t* id);

/**
 * @brief קבלת מזהה כמחרוזת 8 ספרות
 * @param buffer באפר פלט
 * @param buffer_size גודל הבאפר
 * @return true בהצלחה
 */
bool device_id_get_string(char* buffer, size_t buffer_size);

/**
 * @brief קבלת מזהה כמחרוזת hex
 * @param buffer באפר פלט
 * @param buffer_size גודל הבאפר
 * @return true בהצלחה
 */
bool device_id_get_hex(char* buffer, size_t buffer_size);

/**
 * @brief קבלת מזהה גולמי
 * @param buffer באפר פלט (צריך להיות DEVICE_ID_RAW_SIZE)
 * @return true בהצלחה
 */
bool device_id_get_raw(uint8_t* buffer);

/**
 * @brief קבלת מקור המזהה
 */
device_id_source_t device_id_get_source(void);

/**
 * @brief בדיקה אם המזהה תקף
 */
bool device_id_is_valid(void);

// =============================================================================
// ID Generation
// =============================================================================

/**
 * @brief יצירת מזהה חדש (נדרש רק אם אין מזהה)
 * 
 * שים לב: לרוב המזהה נוצר אוטומטית מה-MAC
 * 
 * @param force_regenerate האם לייצר מחדש גם אם קיים
 * @return true בהצלחה
 */
bool device_id_generate(bool force_regenerate);

/**
 * @brief הגדרת מזהה מותאם אישית
 * @param id מזהה חדש (8 ספרות)
 * @return true בהצלחה
 */
bool device_id_set_custom(const char* id);

// =============================================================================
// Verification
// =============================================================================

/**
 * @brief יצירת חתימת אימות
 * 
 * משתמש ב-HMAC-SHA256 עם מפתח סודי
 * 
 * @param verification מבנה פלט
 * @return true בהצלחה
 */
bool device_id_create_verification(device_verification_t* verification);

/**
 * @brief אימות חתימה
 * @param verification החתימה לאימות
 * @return true אם האימות הצליח
 */
bool device_id_verify(const device_verification_t* verification);

/**
 * @brief יצירת token אימות לשרת
 * @param buffer באפר פלט
 * @param buffer_size גודל הבאפר
 * @param timestamp זמן נוכחי
 * @return true בהצלחה
 */
bool device_id_create_auth_token(char* buffer, size_t buffer_size, uint32_t timestamp);

/**
 * @brief אימות token
 * @param token ה-token לאימות
 * @param expected_id המזהה הצפוי
 * @param max_age_seconds גיל מקסימלי (שניות)
 * @return true אם תקף
 */
bool device_id_verify_auth_token(const char* token, const char* expected_id, uint32_t max_age_seconds);

// =============================================================================
// Hardware Info
// =============================================================================

/**
 * @brief קבלת כתובת MAC של WiFi
 * @param mac באפר פלט (6 bytes)
 * @return true בהצלחה
 */
bool device_get_wifi_mac(uint8_t* mac);

/**
 * @brief קבלת כתובת MAC של Bluetooth
 * @param mac באפר פלט (6 bytes)
 * @return true בהצלחה
 */
bool device_get_bt_mac(uint8_t* mac);

/**
 * @brief קבלת מזהה eFuse
 * @param uid באפר פלט (8 bytes)
 * @return true בהצלחה
 */
bool device_get_efuse_uid(uint8_t* uid);

/**
 * @brief קבלת מזהה Flash
 * @param flash_id באפר פלט (8 bytes)
 * @return true בהצלחה
 */
bool device_get_flash_id(uint8_t* flash_id);

// =============================================================================
// Utility
// =============================================================================

/**
 * @brief המרת מזהה גולמי ל-8 ספרות
 * @param raw מזהה גולמי
 * @param raw_size גודל
 * @param output מחרוזת פלט (9 bytes לפחות)
 */
void device_id_raw_to_string(const uint8_t* raw, size_t raw_size, char* output);

/**
 * @brief המרת מזהה גולמי ל-hex
 * @param raw מזהה גולמי
 * @param raw_size גודל
 * @param output מחרוזת פלט (raw_size * 2 + 1)
 */
void device_id_raw_to_hex(const uint8_t* raw, size_t raw_size, char* output);

/**
 * @brief בדיקה אם מזהה תקין (8 ספרות)
 * @param id מזהה לבדיקה
 * @return true אם תקין
 */
bool device_id_validate_format(const char* id);

#endif // CORE_DEVICE_ID_H

