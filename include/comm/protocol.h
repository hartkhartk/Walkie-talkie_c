/**
 * @file protocol.h
 * @brief פרוטוקול תקשורת בין מכשירים
 */

#ifndef COMM_PROTOCOL_H
#define COMM_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// =============================================================================
// Protocol Constants
// =============================================================================

#define PROTOCOL_VERSION        1
#define PACKET_MAGIC            0xWT    // Magic bytes for packet identification
#define MAX_PACKET_SIZE         256
#define PACKET_HEADER_SIZE      12

// =============================================================================
// ID Format (מספרים בלבד 0-9)
// =============================================================================
// Device ID:    8 ספרות - ייחודי לצמיתות, לא משתנה לעולם
// Frequency ID: 8 ספרות - ייחודי רק בזמן שהתדר פעיל
//               לאחר סגירת התדר, הקוד חוזר להיות פנוי לשימוש חוזר

// =============================================================================
// Message Types
// =============================================================================

typedef enum {
    // Discovery
    MSG_DISCOVER_REQUEST    = 0x01,     // בקשת גילוי (סריקה)
    MSG_DISCOVER_RESPONSE   = 0x02,     // תגובת גילוי (הכרזה)
    
    // Call (1-on-1)
    MSG_CALL_REQUEST        = 0x10,     // בקשת שיחה
    MSG_CALL_ACCEPT         = 0x11,     // קבלת שיחה
    MSG_CALL_REJECT         = 0x12,     // דחיית שיחה
    MSG_CALL_END            = 0x13,     // סיום שיחה
    
    // Frequency (Group)
    MSG_FREQ_ANNOUNCE       = 0x20,     // הכרזת תדר (לסריקה)
    MSG_FREQ_JOIN_REQUEST   = 0x21,     // בקשת הצטרפות לתדר
    MSG_FREQ_JOIN_ACCEPT    = 0x22,     // אישור הצטרפות
    MSG_FREQ_JOIN_REJECT    = 0x23,     // דחיית הצטרפות
    MSG_FREQ_LEAVE          = 0x24,     // עזיבת תדר
    MSG_FREQ_KICK           = 0x25,     // הסרת משתתף (מנהל)
    MSG_FREQ_CLOSE          = 0x26,     // סגירת תדר (מנהל)
    MSG_FREQ_INVITE         = 0x27,     // הזמנה לתדר
    
    // Audio
    MSG_VOICE_DATA          = 0x30,     // נתוני קול
    MSG_VOICE_START         = 0x31,     // התחלת שידור קול
    MSG_VOICE_END           = 0x32,     // סיום שידור קול
    
    // Control
    MSG_MUTE                = 0x40,     // הודעת השתקה
    MSG_UNMUTE              = 0x41,     // הודעת ביטול השתקה
    MSG_PING                = 0x42,     // בדיקת חיבור
    MSG_PONG                = 0x43,     // תגובה לפינג
    
    // Status
    MSG_STATUS_UPDATE       = 0x50,     // עדכון סטטוס
    MSG_MEMBER_LIST         = 0x51,     // רשימת חברים בתדר
    
} message_type_t;

// =============================================================================
// Packet Header
// =============================================================================

typedef struct __attribute__((packed)) {
    uint16_t magic;                     // Magic bytes (0x5754 = "WT")
    uint8_t  version;                   // Protocol version
    uint8_t  msg_type;                  // Message type
    char     src_id[DEVICE_ID_LENGTH];  // Source device ID
    uint16_t payload_len;               // Payload length
    uint16_t checksum;                  // CRC16 checksum
} packet_header_t;

// =============================================================================
// Message Payloads
// =============================================================================

// Discovery Request
typedef struct __attribute__((packed)) {
    bool include_frequencies;           // כלול תדרים בתשובה
    bool include_devices;               // כלול מכשירים בתשובה
} discover_request_t;

// Discovery Response (Device)
typedef struct __attribute__((packed)) {
    char device_id[DEVICE_ID_LENGTH];
    char device_name[16];
    int8_t signal_strength;
    bool is_available;                  // האם פנוי לשיחה
} discover_device_t;

// Discovery Response (Frequency)
typedef struct __attribute__((packed)) {
    char freq_id[FREQUENCY_ID_LENGTH];
    uint8_t freq_type;                  // frequency_type_t
    uint8_t protection;                 // frequency_protection_t
    uint8_t member_count;
    int8_t signal_strength;
} discover_frequency_t;

// Call Request
typedef struct __attribute__((packed)) {
    char target_id[DEVICE_ID_LENGTH];   // ID של המכשיר הנקרא
} call_request_t;

// Frequency Join Request
typedef struct __attribute__((packed)) {
    char freq_id[FREQUENCY_ID_LENGTH];
    char password[PASSWORD_MAX_LENGTH]; // ריק אם אין סיסמה
} freq_join_request_t;

// Frequency Join Response
typedef struct __attribute__((packed)) {
    char freq_id[FREQUENCY_ID_LENGTH];
    bool accepted;
    uint8_t member_count;
    char admin_id[DEVICE_ID_LENGTH];
} freq_join_response_t;

// Frequency Invite
typedef struct __attribute__((packed)) {
    char freq_id[FREQUENCY_ID_LENGTH];
    char inviter_id[DEVICE_ID_LENGTH];
    char inviter_name[16];
} freq_invite_t;

// Voice Data
typedef struct __attribute__((packed)) {
    uint32_t timestamp;                 // חותמת זמן
    uint16_t sequence;                  // מספר רצף
    uint16_t audio_len;                 // אורך הנתונים
    uint8_t  audio_data[AUDIO_BUFFER_SIZE]; // נתוני אודיו
} voice_data_t;

// Member Info (for member list)
typedef struct __attribute__((packed)) {
    char device_id[DEVICE_ID_LENGTH];
    char device_name[16];
    bool is_admin;
    bool is_muted;
    int8_t signal_strength;
} member_info_t;

// Member List
typedef struct __attribute__((packed)) {
    uint8_t member_count;
    member_info_t members[MAX_FREQ_MEMBERS];
} member_list_t;

// =============================================================================
// API Functions
// =============================================================================

/**
 * @brief אתחול מודול התקשורת
 */
void protocol_init(void);

/**
 * @brief בניית חבילה עם header
 * @param msg_type סוג ההודעה
 * @param src_id ID המקור
 * @param payload נתוני ההודעה
 * @param payload_len אורך הנתונים
 * @param out_buffer באפר פלט
 * @return אורך החבילה הכולל
 */
uint16_t protocol_build_packet(message_type_t msg_type, const char* src_id,
                               const void* payload, uint16_t payload_len,
                               uint8_t* out_buffer);

/**
 * @brief פירוק חבילה
 * @param buffer באפר קלט
 * @param buffer_len אורך הבאפר
 * @param out_header header שפוענח
 * @param out_payload מצביע לנתונים
 * @return true אם החבילה תקינה
 */
bool protocol_parse_packet(const uint8_t* buffer, uint16_t buffer_len,
                          packet_header_t* out_header, const void** out_payload);

/**
 * @brief שליחת בקשת גילוי
 */
void protocol_send_discover(bool include_freq, bool include_devices);

/**
 * @brief שליחת בקשת שיחה
 * @param target_id ID המכשיר הנקרא
 */
void protocol_send_call_request(const char* target_id);

/**
 * @brief שליחת תגובה לבקשת שיחה
 * @param target_id ID המכשיר ששלח את הבקשה
 * @param accept true לקבלה, false לדחייה
 */
void protocol_send_call_response(const char* target_id, bool accept);

/**
 * @brief שליחת בקשת הצטרפות לתדר
 * @param freq_id ID התדר
 * @param password סיסמה (או NULL)
 */
void protocol_send_freq_join_request(const char* freq_id, const char* password);

/**
 * @brief שליחת הזמנה לתדר
 * @param target_id ID המכשיר המוזמן
 * @param freq_id ID התדר
 */
void protocol_send_freq_invite(const char* target_id, const char* freq_id);

/**
 * @brief שליחת נתוני קול
 * @param audio_data נתוני אודיו
 * @param audio_len אורך הנתונים
 */
void protocol_send_voice(const uint8_t* audio_data, uint16_t audio_len);

/**
 * @brief שליחת הודעת סיום שיחה/תדר
 */
void protocol_send_disconnect(void);

/**
 * @brief חישוב CRC16
 * @param data נתונים
 * @param len אורך
 * @return ערך CRC16
 */
uint16_t protocol_crc16(const uint8_t* data, uint16_t len);

/**
 * @brief טיפול בחבילה שהתקבלה
 * @param buffer באפר החבילה
 * @param len אורך החבילה
 */
void protocol_handle_received(const uint8_t* buffer, uint16_t len);

/**
 * @brief רישום callback לקבלת הודעות
 */
typedef void (*protocol_callback_t)(message_type_t type, const char* src_id, 
                                    const void* payload, uint16_t len);
void protocol_set_callback(protocol_callback_t callback);

/**
 * @brief הגדרת מזהה המכשיר המקומי
 * @param device_id מזהה בן 8 ספרות
 */
void protocol_set_device_id(const char* device_id);

/**
 * @brief קבלת מזהה המכשיר המקומי
 * @return מזהה המכשיר
 */
const char* protocol_get_device_id(void);

#endif // COMM_PROTOCOL_H

