/**
 * @file protocol_v2.h
 * @brief פרוטוקול תקשורת משופר - גרסה 2
 * 
 * שיפורים על הגרסה הקודמת:
 * - Header מורחב עם flags, sequence numbers
 * - תמיכה ב-fragmentation
 * - Timestamp ב-header
 * - CRC32 חזק יותר
 * - הפרדה בין control ו-voice channels
 */

#ifndef COMM_PROTOCOL_V2_H
#define COMM_PROTOCOL_V2_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// =============================================================================
// Protocol Constants
// =============================================================================

#define PROTOCOL_V2_VERSION     2
#define PROTOCOL_V2_MAGIC       0x5754      // "WT" in little-endian
#define MAX_PACKET_SIZE_V2      512
#define MAX_PAYLOAD_SIZE_V2     480

// Channel IDs (for separating control and voice)
#define CHANNEL_CONTROL         0x00
#define CHANNEL_VOICE           0x01
#define CHANNEL_PRIORITY        0x02        // High-priority control messages

// =============================================================================
// Packet Flags
// =============================================================================

typedef enum {
    FLAG_NONE           = 0x00,
    FLAG_ENCRYPTED      = 0x01,     // הפאקט מוצפן
    FLAG_COMPRESSED     = 0x02,     // הנתונים דחוסים
    FLAG_FRAGMENTED     = 0x04,     // חלק מפאקט גדול יותר
    FLAG_LAST_FRAGMENT  = 0x08,     // Fragment אחרון
    FLAG_ACK_REQUIRED   = 0x10,     // דורש אישור קבלה
    FLAG_RETRANSMIT     = 0x20,     // שידור חוזר
    FLAG_PRIORITY       = 0x40,     // עדיפות גבוהה
    FLAG_BROADCAST      = 0x80,     // שידור לכולם
} packet_flags_t;

// =============================================================================
// Message Types (Enhanced)
// =============================================================================

typedef enum {
    // === Discovery (0x0X) ===
    MSG_V2_DISCOVER_REQUEST     = 0x01,
    MSG_V2_DISCOVER_RESPONSE    = 0x02,
    MSG_V2_HEARTBEAT            = 0x03,     // NEW: Keep-alive
    MSG_V2_GOODBYE              = 0x04,     // NEW: Graceful disconnect
    
    // === Call Control (0x1X) ===
    MSG_V2_CALL_REQUEST         = 0x10,
    MSG_V2_CALL_ACCEPT          = 0x11,
    MSG_V2_CALL_REJECT          = 0x12,
    MSG_V2_CALL_END             = 0x13,
    MSG_V2_CALL_HOLD            = 0x14,     // NEW: Hold call
    MSG_V2_CALL_RESUME          = 0x15,     // NEW: Resume call
    
    // === Frequency Control (0x2X) ===
    MSG_V2_FREQ_ANNOUNCE        = 0x20,
    MSG_V2_FREQ_JOIN_REQUEST    = 0x21,
    MSG_V2_FREQ_JOIN_ACCEPT     = 0x22,
    MSG_V2_FREQ_JOIN_REJECT     = 0x23,
    MSG_V2_FREQ_LEAVE           = 0x24,
    MSG_V2_FREQ_KICK            = 0x25,
    MSG_V2_FREQ_CLOSE           = 0x26,
    MSG_V2_FREQ_INVITE          = 0x27,
    MSG_V2_FREQ_UPDATE          = 0x28,     // NEW: Frequency info update
    MSG_V2_FREQ_MEMBER_LIST     = 0x29,     // NEW: Full member list
    
    // === Voice Data (0x3X) ===
    MSG_V2_VOICE_DATA           = 0x30,
    MSG_V2_VOICE_START          = 0x31,
    MSG_V2_VOICE_END            = 0x32,
    MSG_V2_VOICE_SILENCE        = 0x33,     // NEW: Comfort noise/silence
    MSG_V2_VOICE_DTX            = 0x34,     // NEW: Discontinuous TX indicator
    
    // === Control (0x4X) ===
    MSG_V2_MUTE                 = 0x40,
    MSG_V2_UNMUTE               = 0x41,
    MSG_V2_PING                 = 0x42,
    MSG_V2_PONG                 = 0x43,
    MSG_V2_ACK                  = 0x44,     // NEW: Acknowledgment
    MSG_V2_NACK                 = 0x45,     // NEW: Negative ack
    MSG_V2_RETRANSMIT_REQ       = 0x46,     // NEW: Request retransmit
    
    // === Status (0x5X) ===
    MSG_V2_STATUS_UPDATE        = 0x50,
    MSG_V2_QUALITY_REPORT       = 0x51,     // NEW: Network quality stats
    MSG_V2_ERROR                = 0x52,     // NEW: Error message
    
    // === Security (0x6X) ===
    MSG_V2_KEY_EXCHANGE         = 0x60,     // NEW: ECDH key exchange
    MSG_V2_KEY_CONFIRM          = 0x61,     // NEW: Key confirmation
    MSG_V2_REKEY                = 0x62,     // NEW: Request new keys
    
} message_type_v2_t;

// =============================================================================
// Packet Header V2 (Enhanced)
// =============================================================================

/**
 * @brief מבנה Header משופר
 * 
 * סה"כ 24 bytes (היה 12 bytes)
 * מוסיף: sequence, timestamp, flags, channel, fragment info
 */
typedef struct __attribute__((packed)) {
    // === Identification (4 bytes) ===
    uint16_t magic;                         // Magic bytes (0x5754)
    uint8_t  version;                       // Protocol version (2)
    uint8_t  channel;                       // Channel (control/voice)
    
    // === Message Info (4 bytes) ===
    uint8_t  msg_type;                      // Message type
    uint8_t  flags;                         // Packet flags
    uint16_t sequence;                      // Sequence number
    
    // === Addressing (8 bytes) ===
    char     src_id[DEVICE_ID_LENGTH];      // Source device ID
    
    // === Payload Info (4 bytes) ===
    uint16_t payload_len;                   // Payload length
    uint8_t  fragment_id;                   // Fragment ID (if fragmented)
    uint8_t  fragment_count;                // Total fragments
    
    // === Timing & Integrity (4 bytes) ===
    uint32_t timestamp;                     // Timestamp (ms, lower 32 bits)
    
    // === Checksum (4 bytes) ===
    uint32_t crc32;                         // CRC32 of entire packet
    
} packet_header_v2_t;

// Header size: 24 bytes
#define PACKET_HEADER_V2_SIZE   sizeof(packet_header_v2_t)

// =============================================================================
// Payload Structures
// =============================================================================

/**
 * @brief מבנה נתוני קול משופר
 */
typedef struct __attribute__((packed)) {
    uint32_t capture_timestamp;             // זמן לכידה
    uint16_t sequence;                      // מספר רצף frame
    uint8_t  codec;                         // קודק (0=PCM, 1=Opus)
    uint8_t  frame_duration_ms;             // משך frame ב-ms
    uint16_t audio_len;                     // אורך נתוני אודיו
    uint8_t  audio_data[];                  // נתוני אודיו (flexible array)
} voice_data_v2_t;

/**
 * @brief דיווח איכות רשת
 */
typedef struct __attribute__((packed)) {
    uint16_t packets_sent;                  // חבילות שנשלחו
    uint16_t packets_received;              // חבילות שהתקבלו
    uint16_t packets_lost;                  // חבילות שאבדו
    uint16_t avg_latency_ms;                // עיכוב ממוצע
    uint16_t jitter_ms;                     // רעידות
    int8_t   rssi;                          // עוצמת אות
    uint8_t  link_quality;                  // איכות קישור (0-100)
} quality_report_t;

/**
 * @brief בקשת החלפת מפתחות (ECDH)
 */
typedef struct __attribute__((packed)) {
    uint8_t  public_key[32];                // ECDH public key (X25519)
    uint8_t  nonce[12];                     // Random nonce
    uint32_t key_id;                        // Key identifier
} key_exchange_t;

/**
 * @brief מידע שגיאה
 */
typedef struct __attribute__((packed)) {
    uint16_t error_code;                    // קוד שגיאה
    uint16_t related_sequence;              // sequence קשור
    char     message[64];                   // הודעת שגיאה
} error_info_t;

// =============================================================================
// Error Codes
// =============================================================================

typedef enum {
    ERR_NONE                = 0x0000,
    ERR_UNKNOWN             = 0x0001,
    ERR_INVALID_PACKET      = 0x0002,
    ERR_CRC_MISMATCH        = 0x0003,
    ERR_UNSUPPORTED_VERSION = 0x0004,
    ERR_AUTHENTICATION      = 0x0010,
    ERR_ENCRYPTION          = 0x0011,
    ERR_KEY_EXPIRED         = 0x0012,
    ERR_PERMISSION_DENIED   = 0x0020,
    ERR_FREQUENCY_FULL      = 0x0021,
    ERR_FREQUENCY_CLOSED    = 0x0022,
    ERR_WRONG_PASSWORD      = 0x0023,
    ERR_TIMEOUT             = 0x0030,
    ERR_BUFFER_OVERFLOW     = 0x0031,
} error_code_t;

// =============================================================================
// Codec Types
// =============================================================================

typedef enum {
    CODEC_PCM_16KHZ     = 0x00,     // PCM 16-bit @ 16kHz
    CODEC_PCM_8KHZ      = 0x01,     // PCM 16-bit @ 8kHz
    CODEC_OPUS          = 0x10,     // Opus codec
    CODEC_OPUS_DTX      = 0x11,     // Opus with DTX
} audio_codec_t;

// =============================================================================
// API Functions
// =============================================================================

/**
 * @brief אתחול פרוטוקול V2
 */
void protocol_v2_init(void);

/**
 * @brief בניית header
 */
void protocol_v2_build_header(
    packet_header_v2_t* header,
    uint8_t msg_type,
    uint8_t channel,
    uint8_t flags,
    const char* src_id,
    uint16_t payload_len
);

/**
 * @brief בניית חבילה מלאה
 */
uint16_t protocol_v2_build_packet(
    message_type_v2_t msg_type,
    uint8_t channel,
    uint8_t flags,
    const char* src_id,
    const void* payload,
    uint16_t payload_len,
    uint8_t* out_buffer
);

/**
 * @brief פירוק חבילה
 */
bool protocol_v2_parse_packet(
    const uint8_t* buffer,
    uint16_t buffer_len,
    packet_header_v2_t* out_header,
    const void** out_payload
);

/**
 * @brief וידוא CRC
 */
bool protocol_v2_verify_crc(const uint8_t* packet, uint16_t len);

/**
 * @brief חישוב CRC32
 */
uint32_t protocol_v2_crc32(const uint8_t* data, uint16_t len);

/**
 * @brief קבלת sequence number הבא
 */
uint16_t protocol_v2_next_sequence(void);

/**
 * @brief איפוס sequence counter
 */
void protocol_v2_reset_sequence(void);

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * @brief האם ההודעה דורשת אישור?
 */
bool protocol_v2_requires_ack(message_type_v2_t msg_type);

/**
 * @brief האם ההודעה היא voice?
 */
bool protocol_v2_is_voice(message_type_v2_t msg_type);

/**
 * @brief קבלת שם סוג ההודעה
 */
const char* protocol_v2_msg_name(message_type_v2_t msg_type);

#endif // COMM_PROTOCOL_V2_H

