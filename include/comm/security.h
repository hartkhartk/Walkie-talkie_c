/**
 * @file security.h
 * @brief מודול אבטחה - הצפנה והחלפת מפתחות
 * 
 * תומך ב:
 * - AES-128-GCM להצפנת נתונים
 * - ECDH (X25519) להחלפת מפתחות
 * - HMAC-SHA256 לאימות
 */

#ifndef COMM_SECURITY_H
#define COMM_SECURITY_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// =============================================================================
// Security Constants
// =============================================================================

#define SECURITY_KEY_SIZE       16      // AES-128
#define SECURITY_NONCE_SIZE     12      // GCM nonce
#define SECURITY_TAG_SIZE       16      // GCM auth tag
#define SECURITY_ECDH_KEY_SIZE  32      // X25519 key size
#define SECURITY_HASH_SIZE      32      // SHA256 output

// Maximum encrypted payload (original + tag)
#define SECURITY_MAX_ENCRYPTED_SIZE (MAX_PAYLOAD_SIZE_V2 + SECURITY_TAG_SIZE)

// =============================================================================
// Security Context
// =============================================================================

/**
 * @brief מצב הצפנה לחיבור
 */
typedef struct {
    uint8_t  session_key[SECURITY_KEY_SIZE];    // מפתח הצפנה נוכחי
    uint8_t  nonce_counter[SECURITY_NONCE_SIZE]; // מונה nonce
    uint32_t key_id;                             // מזהה מפתח
    uint32_t packets_encrypted;                  // מונה חבילות
    uint32_t key_created_time;                   // זמן יצירת המפתח
    bool     is_initialized;                     // האם אותחל
    bool     key_agreed;                         // האם הוסכם מפתח
} security_context_t;

/**
 * @brief מידע ECDH זמני
 */
typedef struct {
    uint8_t  private_key[SECURITY_ECDH_KEY_SIZE];
    uint8_t  public_key[SECURITY_ECDH_KEY_SIZE];
    uint8_t  peer_public_key[SECURITY_ECDH_KEY_SIZE];
    uint8_t  shared_secret[SECURITY_ECDH_KEY_SIZE];
    bool     key_generated;
    bool     secret_derived;
} ecdh_context_t;

// =============================================================================
// Error Codes
// =============================================================================

typedef enum {
    SECURITY_OK                 = 0,
    SECURITY_ERROR_INVALID_KEY  = -1,
    SECURITY_ERROR_ENCRYPT      = -2,
    SECURITY_ERROR_DECRYPT      = -3,
    SECURITY_ERROR_AUTH_FAILED  = -4,
    SECURITY_ERROR_NONCE        = -5,
    SECURITY_ERROR_KEY_EXPIRED  = -6,
    SECURITY_ERROR_NOT_INIT     = -7,
    SECURITY_ERROR_BUFFER_SIZE  = -8,
} security_error_t;

// =============================================================================
// Initialization
// =============================================================================

/**
 * @brief אתחול מודול האבטחה
 */
void security_init(void);

/**
 * @brief אתחול context אבטחה לחיבור
 * @param ctx מצביע ל-context
 */
void security_context_init(security_context_t* ctx);

/**
 * @brief ניקוי context (מחיקת מפתחות)
 * @param ctx מצביע ל-context
 */
void security_context_clear(security_context_t* ctx);

// =============================================================================
// Key Exchange (ECDH)
// =============================================================================

/**
 * @brief יצירת זוג מפתחות ECDH
 * @param ecdh מצביע ל-context ECDH
 * @return SECURITY_OK בהצלחה
 */
security_error_t security_ecdh_generate_keypair(ecdh_context_t* ecdh);

/**
 * @brief קבלת המפתח הציבורי
 * @param ecdh מצביע ל-context
 * @param public_key באפר פלט (32 bytes)
 * @return SECURITY_OK בהצלחה
 */
security_error_t security_ecdh_get_public_key(
    const ecdh_context_t* ecdh,
    uint8_t* public_key
);

/**
 * @brief חישוב הסוד המשותף
 * @param ecdh מצביע ל-context
 * @param peer_public_key המפתח הציבורי של הצד השני
 * @return SECURITY_OK בהצלחה
 */
security_error_t security_ecdh_compute_shared_secret(
    ecdh_context_t* ecdh,
    const uint8_t* peer_public_key
);

/**
 * @brief גזירת מפתח הצפנה מהסוד המשותף
 * @param ecdh מצביע ל-context ECDH
 * @param ctx מצביע ל-context אבטחה (פלט)
 * @param salt מלח (אופציונלי, יכול להיות NULL)
 * @param salt_len אורך המלח
 * @return SECURITY_OK בהצלחה
 */
security_error_t security_derive_session_key(
    const ecdh_context_t* ecdh,
    security_context_t* ctx,
    const uint8_t* salt,
    uint16_t salt_len
);

// =============================================================================
// Pre-Shared Key (PSK)
// =============================================================================

/**
 * @brief הגדרת מפתח משותף מראש
 * 
 * לשימוש בפיתוח ובמקרים שלא צריך ECDH
 * 
 * @param ctx מצביע ל-context
 * @param key מפתח (16 bytes)
 * @return SECURITY_OK בהצלחה
 */
security_error_t security_set_psk(
    security_context_t* ctx,
    const uint8_t* key
);

/**
 * @brief גזירת מפתח מסיסמה
 * @param ctx מצביע ל-context
 * @param password סיסמה
 * @param password_len אורך הסיסמה
 * @param salt מלח
 * @param salt_len אורך המלח
 * @return SECURITY_OK בהצלחה
 */
security_error_t security_derive_key_from_password(
    security_context_t* ctx,
    const char* password,
    uint16_t password_len,
    const uint8_t* salt,
    uint16_t salt_len
);

// =============================================================================
// Encryption (AES-GCM)
// =============================================================================

/**
 * @brief הצפנת נתונים עם AES-128-GCM
 * 
 * @param ctx מצביע ל-context
 * @param plaintext נתונים להצפנה
 * @param plaintext_len אורך הנתונים
 * @param aad נתונים נוספים לאימות (header)
 * @param aad_len אורך AAD
 * @param ciphertext באפר פלט (צריך להיות בגודל plaintext_len + TAG_SIZE)
 * @param ciphertext_len אורך הפלט (פלט)
 * @return SECURITY_OK בהצלחה
 */
security_error_t security_encrypt(
    security_context_t* ctx,
    const uint8_t* plaintext,
    uint16_t plaintext_len,
    const uint8_t* aad,
    uint16_t aad_len,
    uint8_t* ciphertext,
    uint16_t* ciphertext_len
);

/**
 * @brief פענוח נתונים עם AES-128-GCM
 * 
 * @param ctx מצביע ל-context
 * @param ciphertext נתונים מוצפנים
 * @param ciphertext_len אורך (כולל tag)
 * @param aad נתונים נוספים לאימות
 * @param aad_len אורך AAD
 * @param plaintext באפר פלט
 * @param plaintext_len אורך הפלט (פלט)
 * @return SECURITY_OK בהצלחה, SECURITY_ERROR_AUTH_FAILED אם האימות נכשל
 */
security_error_t security_decrypt(
    security_context_t* ctx,
    const uint8_t* ciphertext,
    uint16_t ciphertext_len,
    const uint8_t* aad,
    uint16_t aad_len,
    uint8_t* plaintext,
    uint16_t* plaintext_len
);

// =============================================================================
// Nonce Management
// =============================================================================

/**
 * @brief קבלת nonce חדש
 * @param ctx מצביע ל-context
 * @param nonce באפר פלט (12 bytes)
 */
void security_get_nonce(security_context_t* ctx, uint8_t* nonce);

/**
 * @brief בדיקה והגדרת nonce שהתקבל
 * 
 * מונע replay attacks
 * 
 * @param ctx מצביע ל-context
 * @param nonce ה-nonce שהתקבל
 * @return true אם ה-nonce תקין (חדש יותר מהאחרון)
 */
bool security_verify_nonce(security_context_t* ctx, const uint8_t* nonce);

// =============================================================================
// Key Lifecycle
// =============================================================================

/**
 * @brief בדיקה האם המפתח צריך רענון
 * 
 * מפתח צריך רענון אחרי:
 * - X חבילות
 * - Y שניות
 * 
 * @param ctx מצביע ל-context
 * @return true אם צריך החלפת מפתח
 */
bool security_key_needs_refresh(const security_context_t* ctx);

/**
 * @brief קבלת סטטיסטיקות אבטחה
 */
typedef struct {
    uint32_t packets_encrypted;
    uint32_t packets_decrypted;
    uint32_t auth_failures;
    uint32_t key_refreshes;
    uint32_t key_age_seconds;
} security_stats_t;

void security_get_stats(const security_context_t* ctx, security_stats_t* stats);

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief יצירת מספר אקראי
 * @param buffer באפר פלט
 * @param len אורך
 */
void security_random_bytes(uint8_t* buffer, uint16_t len);

/**
 * @brief חישוב SHA256
 * @param data נתונים
 * @param len אורך
 * @param hash פלט (32 bytes)
 */
void security_sha256(const uint8_t* data, uint16_t len, uint8_t* hash);

/**
 * @brief חישוב HMAC-SHA256
 * @param key מפתח
 * @param key_len אורך מפתח
 * @param data נתונים
 * @param data_len אורך נתונים
 * @param hmac פלט (32 bytes)
 */
void security_hmac_sha256(
    const uint8_t* key, uint16_t key_len,
    const uint8_t* data, uint16_t data_len,
    uint8_t* hmac
);

/**
 * @brief השוואה בטוחה (constant-time)
 * @param a באפר ראשון
 * @param b באפר שני
 * @param len אורך
 * @return true אם שווים
 */
bool security_constant_compare(const uint8_t* a, const uint8_t* b, uint16_t len);

#endif // COMM_SECURITY_H

