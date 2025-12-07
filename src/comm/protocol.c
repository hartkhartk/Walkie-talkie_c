/**
 * @file protocol.c
 * @brief מימוש פרוטוקול התקשורת בין מכשירים
 */

#include "comm/protocol.h"
#include "comm/radio.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

// =============================================================================
// Platform-Specific Includes
// =============================================================================

#ifdef ESP32
    #include "esp_timer.h"
    #include "esp_log.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/semphr.h"
    
    static const char* TAG = "PROTOCOL";
    #define LOG_INFO(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...) ESP_LOGD(TAG, fmt, ##__VA_ARGS__)
    #define GET_MILLIS() (esp_timer_get_time() / 1000)
#else
    #include <time.h>
    #define LOG_INFO(fmt, ...) printf("[PROTOCOL] " fmt "\n", ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) printf("[PROTOCOL ERROR] " fmt "\n", ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...)
    static uint32_t sim_millis(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
    }
    #define GET_MILLIS() sim_millis()
#endif

// =============================================================================
// Constants
// =============================================================================

#define PACKET_MAGIC_VALUE  0x5754  // "WT" in little-endian

// =============================================================================
// Internal State
// =============================================================================

static bool g_initialized = false;
static char g_local_device_id[DEVICE_ID_LENGTH + 1] = {0};
static protocol_callback_t g_callback = NULL;

static uint8_t g_tx_buffer[MAX_PACKET_SIZE];
static uint8_t g_rx_buffer[MAX_PACKET_SIZE];

static uint16_t g_voice_sequence = 0;

#ifdef ESP32
static SemaphoreHandle_t g_protocol_mutex = NULL;
#endif

// =============================================================================
// CRC16 Implementation (CCITT)
// =============================================================================

static const uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

uint16_t protocol_crc16(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    
    while (len--) {
        crc = (crc << 8) ^ crc16_table[((crc >> 8) ^ *data++) & 0xFF];
    }
    
    return crc;
}

// =============================================================================
// Radio Callbacks
// =============================================================================

static void on_radio_rx(const uint8_t* data, uint8_t length, int16_t rssi, int8_t snr) {
    (void)rssi;
    (void)snr;
    
    protocol_handle_received(data, length);
}

static void on_radio_tx(bool success) {
    if (!success) {
        LOG_ERROR("TX failed");
    }
}

// =============================================================================
// Initialization
// =============================================================================

void protocol_init(void) {
    if (g_initialized) return;
    
    LOG_INFO("Initializing protocol...");
    
#ifdef ESP32
    g_protocol_mutex = xSemaphoreCreateMutex();
    if (!g_protocol_mutex) {
        LOG_ERROR("Failed to create protocol mutex");
        return;
    }
#endif
    
    // Initialize radio
    if (!radio_init()) {
        LOG_ERROR("Failed to initialize radio");
        return;
    }
    
    // Set radio callbacks
    radio_set_rx_callback(on_radio_rx);
    radio_set_tx_callback(on_radio_tx);
    
    // Start listening
    radio_start_receive();
    
    g_voice_sequence = 0;
    g_initialized = true;
    
    LOG_INFO("Protocol initialized");
}

void protocol_set_callback(protocol_callback_t callback) {
    g_callback = callback;
}

// =============================================================================
// Packet Building
// =============================================================================

uint16_t protocol_build_packet(message_type_t msg_type, const char* src_id,
                               const void* payload, uint16_t payload_len,
                               uint8_t* out_buffer) {
    if (!out_buffer) return 0;
    if (payload_len > MAX_PACKET_SIZE - PACKET_HEADER_SIZE) {
        payload_len = MAX_PACKET_SIZE - PACKET_HEADER_SIZE;
    }
    
    // Build header
    packet_header_t* header = (packet_header_t*)out_buffer;
    header->magic = PACKET_MAGIC_VALUE;
    header->version = PROTOCOL_VERSION;
    header->msg_type = (uint8_t)msg_type;
    
    // Copy source ID
    if (src_id) {
        memcpy(header->src_id, src_id, DEVICE_ID_LENGTH);
    } else if (g_local_device_id[0]) {
        memcpy(header->src_id, g_local_device_id, DEVICE_ID_LENGTH);
    } else {
        memset(header->src_id, '0', DEVICE_ID_LENGTH);
    }
    
    header->payload_len = payload_len;
    
    // Copy payload
    if (payload && payload_len > 0) {
        memcpy(out_buffer + sizeof(packet_header_t), payload, payload_len);
    }
    
    // Calculate CRC (over header + payload, excluding CRC field itself)
    uint16_t packet_len = sizeof(packet_header_t) + payload_len;
    header->checksum = 0;  // Clear before CRC calculation
    header->checksum = protocol_crc16(out_buffer, packet_len);
    
    return packet_len;
}

// =============================================================================
// Packet Parsing
// =============================================================================

bool protocol_parse_packet(const uint8_t* buffer, uint16_t buffer_len,
                          packet_header_t* out_header, const void** out_payload) {
    if (!buffer || buffer_len < sizeof(packet_header_t)) {
        return false;
    }
    
    const packet_header_t* header = (const packet_header_t*)buffer;
    
    // Check magic
    if (header->magic != PACKET_MAGIC_VALUE) {
        LOG_DEBUG("Invalid magic: 0x%04X", header->magic);
        return false;
    }
    
    // Check version
    if (header->version != PROTOCOL_VERSION) {
        LOG_DEBUG("Invalid version: %d", header->version);
        return false;
    }
    
    // Check length
    if (buffer_len < sizeof(packet_header_t) + header->payload_len) {
        LOG_DEBUG("Buffer too short");
        return false;
    }
    
    // Verify CRC
    uint16_t stored_crc = header->checksum;
    packet_header_t* mutable_header = (packet_header_t*)buffer;
    mutable_header->checksum = 0;
    uint16_t calc_crc = protocol_crc16(buffer, sizeof(packet_header_t) + header->payload_len);
    mutable_header->checksum = stored_crc;
    
    if (calc_crc != stored_crc) {
        LOG_DEBUG("CRC mismatch: calc=0x%04X, stored=0x%04X", calc_crc, stored_crc);
        return false;
    }
    
    // Output
    if (out_header) {
        memcpy(out_header, header, sizeof(packet_header_t));
    }
    
    if (out_payload) {
        *out_payload = buffer + sizeof(packet_header_t);
    }
    
    return true;
}

// =============================================================================
// Internal Send Helper
// =============================================================================

static bool send_packet(message_type_t msg_type, const void* payload, uint16_t payload_len) {
    uint16_t packet_len = protocol_build_packet(msg_type, g_local_device_id,
                                                 payload, payload_len, g_tx_buffer);
    
    if (packet_len == 0) {
        return false;
    }
    
    return radio_send(g_tx_buffer, packet_len);
}

// =============================================================================
// Public Send Functions
// =============================================================================

void protocol_send_discover(bool include_freq, bool include_devices) {
    LOG_DEBUG("Sending discover request");
    
    discover_request_t request = {
        .include_frequencies = include_freq,
        .include_devices = include_devices
    };
    
    send_packet(MSG_DISCOVER_REQUEST, &request, sizeof(request));
}

void protocol_send_call_request(const char* target_id) {
    if (!target_id) return;
    
    LOG_INFO("Sending call request to: %s", target_id);
    
    call_request_t request;
    strncpy(request.target_id, target_id, DEVICE_ID_LENGTH);
    
    send_packet(MSG_CALL_REQUEST, &request, sizeof(request));
}

void protocol_send_call_response(const char* target_id, bool accept) {
    if (!target_id) return;
    
    LOG_INFO("Sending call %s to: %s", accept ? "accept" : "reject", target_id);
    
    message_type_t msg_type = accept ? MSG_CALL_ACCEPT : MSG_CALL_REJECT;
    
    // Target ID as payload
    send_packet(msg_type, target_id, DEVICE_ID_LENGTH);
}

void protocol_send_freq_join_request(const char* freq_id, const char* password) {
    if (!freq_id) return;
    
    LOG_INFO("Sending freq join request: %s", freq_id);
    
    freq_join_request_t request;
    strncpy(request.freq_id, freq_id, FREQUENCY_ID_LENGTH);
    
    if (password) {
        strncpy(request.password, password, PASSWORD_MAX_LENGTH);
    } else {
        memset(request.password, 0, PASSWORD_MAX_LENGTH);
    }
    
    send_packet(MSG_FREQ_JOIN_REQUEST, &request, sizeof(request));
}

void protocol_send_freq_invite(const char* target_id, const char* freq_id) {
    if (!target_id || !freq_id) return;
    
    LOG_INFO("Sending freq invite to %s for freq %s", target_id, freq_id);
    
    freq_invite_t invite;
    strncpy(invite.freq_id, freq_id, FREQUENCY_ID_LENGTH);
    strncpy(invite.inviter_id, g_local_device_id, DEVICE_ID_LENGTH);
    snprintf(invite.inviter_name, 16, "Device");  // TODO: use actual device name
    
    send_packet(MSG_FREQ_INVITE, &invite, sizeof(invite));
}

void protocol_send_voice(const uint8_t* audio_data, uint16_t audio_len) {
    if (!audio_data || audio_len == 0) return;
    
    voice_data_t voice;
    voice.timestamp = GET_MILLIS();
    voice.sequence = g_voice_sequence++;
    voice.audio_len = (audio_len > AUDIO_BUFFER_SIZE) ? AUDIO_BUFFER_SIZE : audio_len;
    memcpy(voice.audio_data, audio_data, voice.audio_len);
    
    // Voice packets are sent without waiting (best effort)
    uint16_t packet_len = protocol_build_packet(MSG_VOICE_DATA, g_local_device_id,
                                                 &voice, sizeof(voice), g_tx_buffer);
    
    if (packet_len > 0) {
        radio_send(g_tx_buffer, packet_len);
    }
}

void protocol_send_disconnect(void) {
    LOG_INFO("Sending disconnect");
    send_packet(MSG_CALL_END, NULL, 0);
}

// =============================================================================
// Packet Handling
// =============================================================================

void protocol_handle_received(const uint8_t* buffer, uint16_t len) {
    packet_header_t header;
    const void* payload = NULL;
    
    if (!protocol_parse_packet(buffer, len, &header, &payload)) {
        LOG_DEBUG("Failed to parse received packet");
        return;
    }
    
    // Extract source ID as null-terminated string
    char src_id[DEVICE_ID_LENGTH + 1];
    memcpy(src_id, header.src_id, DEVICE_ID_LENGTH);
    src_id[DEVICE_ID_LENGTH] = '\0';
    
    LOG_DEBUG("Received msg type 0x%02X from %s", header.msg_type, src_id);
    
    // Handle specific message types
    switch ((message_type_t)header.msg_type) {
        case MSG_DISCOVER_REQUEST:
            // TODO: Respond with our device info if visible
            LOG_DEBUG("Discover request received");
            break;
            
        case MSG_DISCOVER_RESPONSE:
            // Pass to callback for device list update
            break;
            
        case MSG_CALL_REQUEST:
            // Someone is calling us
            if (header.payload_len >= sizeof(call_request_t)) {
                const call_request_t* req = (const call_request_t*)payload;
                char target[DEVICE_ID_LENGTH + 1];
                memcpy(target, req->target_id, DEVICE_ID_LENGTH);
                target[DEVICE_ID_LENGTH] = '\0';
                
                // Check if we're the target
                if (strcmp(target, g_local_device_id) == 0) {
                    LOG_INFO("Incoming call from %s", src_id);
                }
            }
            break;
            
        case MSG_VOICE_DATA:
            // Pass audio data to playback
            if (header.payload_len >= sizeof(voice_data_t)) {
                const voice_data_t* voice = (const voice_data_t*)payload;
                LOG_DEBUG("Voice data: seq=%d, len=%d", voice->sequence, voice->audio_len);
                // TODO: Add to playback buffer
            }
            break;
            
        case MSG_PING:
            // Respond with pong
            send_packet(MSG_PONG, src_id, DEVICE_ID_LENGTH);
            break;
            
        default:
            break;
    }
    
    // Call user callback
    if (g_callback) {
        g_callback((message_type_t)header.msg_type, src_id, payload, header.payload_len);
    }
}

// =============================================================================
// Device ID Management
// =============================================================================

/**
 * @brief Set the local device ID (called during initialization)
 * @param device_id 8-digit device ID
 */
void protocol_set_device_id(const char* device_id) {
    if (!device_id) return;
    
    strncpy(g_local_device_id, device_id, DEVICE_ID_LENGTH);
    g_local_device_id[DEVICE_ID_LENGTH] = '\0';
    
    LOG_INFO("Device ID set: %s", g_local_device_id);
}

/**
 * @brief Get the local device ID
 * @return Device ID string
 */
const char* protocol_get_device_id(void) {
    return g_local_device_id;
}

