/**
 * @file main.c
 * @brief נקודת כניסה ראשית למכשיר הקשר
 * 
 * Advanced Walkie-Talkie Firmware
 * מכשיר קשר מתקדם
 */

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "hal/buttons.h"
#include "hal/display.h"
#include "hal/audio.h"
#include "core/device_state.h"
#include "core/dial_manager.h"
#include "core/audio_buffer.h"
#include "comm/protocol.h"
#include "comm/radio.h"

// =============================================================================
// Platform-Specific Includes
// =============================================================================

#ifdef ESP32
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "esp_log.h"
    #include "nvs_flash.h"
    
    static const char* TAG = "WT-MAIN";
    #define LOG_INFO(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...) ESP_LOGD(TAG, fmt, ##__VA_ARGS__)
    #define DELAY_MS(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#else
    // Simulator / PC build
    #include <unistd.h>
    #define LOG_INFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...)
    #define DELAY_MS(ms) usleep((ms) * 1000)
#endif

// =============================================================================
// Global State
// =============================================================================

static device_context_t g_device_ctx;
static dial_manager_t g_dial_manager;
static bool g_running = true;

// Audio buffers
static audio_ring_buffer_t g_record_buffer;
static audio_ring_buffer_t g_playback_buffer;

// Transmission state
static bool g_is_transmitting = false;

// =============================================================================
// Forward Declarations
// =============================================================================

static void init_system(void);
static void main_loop(void);
static void handle_audio_transmission(void);
static void handle_audio_playback(void);

// =============================================================================
// Audio Capture Callback
// =============================================================================

static void on_audio_captured(const int16_t* samples, uint16_t sample_count) {
    // Send audio over radio if transmitting
    if (g_is_transmitting && g_device_ctx.is_connected) {
        // Convert to bytes for protocol
        protocol_send_voice((const uint8_t*)samples, sample_count * sizeof(int16_t));
    }
}

// =============================================================================
// Button Callback
// =============================================================================

static void on_button_event(button_id_t button, button_event_t event) {
    // Pass event to device state machine
    device_handle_button(&g_device_ctx, button, event);
}

// =============================================================================
// Talk Mode Callback
// =============================================================================

static void on_talk_mode_change(void) {
    talk_mode_t mode = buttons_get_talk_mode();
    LOG_INFO("Talk mode changed: %d", mode);
    
    // Update mute state based on talk mode
    switch (mode) {
        case TALK_MODE_ALWAYS:
            // Always transmitting
            g_device_ctx.is_muted = false;
            if (g_device_ctx.is_connected && !audio_is_recording()) {
                audio_start_recording_callback(on_audio_captured);
            }
            break;
            
        case TALK_MODE_PTT:
            // Only when button pressed (handled in main loop)
            g_device_ctx.is_muted = false;
            break;
            
        case TALK_MODE_MUTED:
            // Always muted
            g_device_ctx.is_muted = true;
            if (audio_is_recording()) {
                audio_stop_recording();
            }
            break;
    }
}

// =============================================================================
// Visibility Callback
// =============================================================================

static void on_visibility_change(void) {
    visibility_mode_t vis = buttons_get_visibility_mode();
    g_device_ctx.is_visible = (vis == VISIBILITY_VISIBLE);
    LOG_INFO("Visibility changed: %s", g_device_ctx.is_visible ? "visible" : "hidden");
}

// =============================================================================
// Volume Callback
// =============================================================================

static void on_volume_change(int8_t delta) {
    (void)delta;
    rotary_state_t vol = buttons_get_volume();
    LOG_INFO("Volume: %d", vol.absolute);
    
    // Update audio volume
    audio_set_output_volume(vol.absolute);
}

// =============================================================================
// Protocol Message Callback
// =============================================================================

static void on_protocol_message(message_type_t type, const char* src_id,
                                const void* payload, uint16_t len) {
    switch (type) {
        case MSG_DISCOVER_RESPONSE:
            // Add to scan results
            LOG_INFO("Discovered device: %s", src_id);
            break;
            
        case MSG_CALL_REQUEST:
            // Incoming call
            LOG_INFO("Incoming call from: %s", src_id);
            strncpy(g_device_ctx.current_connection.device.id, src_id, DEVICE_ID_LENGTH);
            g_device_ctx.connected_to_frequency = false;
            strcpy(g_device_ctx.message_text, src_id);
            device_set_state(&g_device_ctx, STATE_INCOMING_REQUEST);
            
            // Beep to notify
            audio_beep();
            break;
            
        case MSG_CALL_ACCEPT:
            // Call accepted - start audio
            LOG_INFO("Call accepted by: %s", src_id);
            g_device_ctx.is_connected = true;
            device_set_state(&g_device_ctx, STATE_IN_CALL);
            
            // Start audio playback
            audio_start_playback(&g_playback_buffer);
            audio_beep();
            break;
            
        case MSG_CALL_REJECT:
            LOG_INFO("Call rejected by: %s", src_id);
            strcpy(g_device_ctx.message_title, "Rejected");
            strcpy(g_device_ctx.message_text, "Call declined");
            g_device_ctx.message_timeout = 2000;
            device_set_state(&g_device_ctx, STATE_MESSAGE);
            break;
            
        case MSG_FREQ_JOIN_ACCEPT:
            LOG_INFO("Joined frequency");
            g_device_ctx.is_connected = true;
            g_device_ctx.connected_to_frequency = true;
            device_set_state(&g_device_ctx, STATE_IN_FREQUENCY);
            
            // Start audio playback
            audio_start_playback(&g_playback_buffer);
            audio_beep();
            break;
            
        case MSG_FREQ_INVITE:
            if (len >= sizeof(freq_invite_t)) {
                const freq_invite_t* invite = (const freq_invite_t*)payload;
                LOG_INFO("Frequency invite from: %s", invite->inviter_id);
                strncpy(g_device_ctx.current_connection.frequency.id, 
                       invite->freq_id, FREQUENCY_ID_LENGTH);
                g_device_ctx.connected_to_frequency = true;
                snprintf(g_device_ctx.message_text, sizeof(g_device_ctx.message_text),
                        "Invite: %s", invite->inviter_name);
                device_set_state(&g_device_ctx, STATE_INCOMING_REQUEST);
                audio_beep();
            }
            break;
            
        case MSG_VOICE_DATA:
            // Handle incoming audio
            if (len >= sizeof(voice_data_t)) {
                const voice_data_t* voice = (const voice_data_t*)payload;
                // Add to playback buffer
                audio_buffer_write(&g_playback_buffer, 
                                  voice->audio_data, 
                                  voice->audio_len, 
                                  voice->timestamp);
            }
            break;
            
        case MSG_CALL_END:
        case MSG_FREQ_CLOSE:
        case MSG_FREQ_KICK:
            LOG_INFO("Disconnected");
            g_device_ctx.is_connected = false;
            
            // Stop audio
            audio_stop_recording();
            audio_stop_playback();
            
            device_set_state(&g_device_ctx, STATE_IDLE);
            break;
            
        default:
            break;
    }
}

// =============================================================================
// Initialization
// =============================================================================

static void init_system(void) {
    LOG_INFO("Initializing Walkie-Talkie v%s", FIRMWARE_VERSION);
    
#ifdef ESP32
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
#endif
    
    // Initialize HAL
    LOG_INFO("Initializing HAL...");
    buttons_init();
    display_init();
    
    // Initialize audio with default config
    audio_config_t audio_cfg;
    audio_get_default_config(&audio_cfg);
    if (!audio_init(&audio_cfg)) {
        LOG_ERROR("Failed to initialize audio!");
    }
    
    // Initialize audio buffers
    audio_buffer_init(&g_record_buffer);
    audio_buffer_init(&g_playback_buffer);
    audio_buffer_set_jitter_depth(&g_playback_buffer, 4);
    
    // Set callbacks
    buttons_set_callback(on_button_event);
    buttons_set_talk_mode_callback(on_talk_mode_change);
    buttons_set_visibility_callback(on_visibility_change);
    buttons_set_volume_callback(on_volume_change);
    
    // Initialize protocol (includes radio init)
    LOG_INFO("Initializing protocol...");
    protocol_init();
    protocol_set_callback(on_protocol_message);
    
    // Initialize device state
    device_init(&g_device_ctx);
    
    // Set device ID in protocol layer
    protocol_set_device_id(g_device_ctx.device_id);
    
    // Initialize dial manager (15 positions with thread management)
    dial_manager_init(&g_dial_manager);
    LOG_INFO("Dial manager ready: max %d concurrent connections", MAX_DIAL_THREADS);
    
    // Set initial volume from hardware
    rotary_state_t vol = buttons_get_volume();
    audio_set_output_volume(vol.absolute);
    
    // Play startup sound
    audio_beep();
    
    LOG_INFO("Device ID: %s", g_device_ctx.device_id);
    LOG_INFO("Initialization complete!");
}

// =============================================================================
// Audio Transmission Handling
// =============================================================================

static void handle_audio_transmission(void) {
    if (!g_device_ctx.is_connected || g_device_ctx.is_muted) {
        // Not connected or muted - stop transmitting
        if (g_is_transmitting) {
            g_is_transmitting = false;
            if (audio_is_recording()) {
                audio_stop_recording();
            }
            LOG_DEBUG("Stopped transmitting");
        }
        return;
    }
    
    // Check if we should be transmitting based on PTT state
    bool should_transmit = buttons_is_transmitting();
    
    if (should_transmit && !g_is_transmitting) {
        // Start transmitting
        g_is_transmitting = true;
        audio_start_recording_callback(on_audio_captured);
        LOG_DEBUG("Started transmitting");
    } else if (!should_transmit && g_is_transmitting) {
        // Stop transmitting (only in PTT mode)
        talk_mode_t mode = buttons_get_talk_mode();
        if (mode == TALK_MODE_PTT) {
            g_is_transmitting = false;
            audio_stop_recording();
            LOG_DEBUG("Stopped transmitting (PTT released)");
        }
    }
}

// =============================================================================
// Audio Playback Handling
// =============================================================================

static void handle_audio_playback(void) {
    if (!g_device_ctx.is_connected) {
        return;
    }
    
    // Start playback if not already playing and we're connected
    if (!audio_is_playing() && audio_buffer_jitter_ready(&g_playback_buffer)) {
        audio_start_playback(&g_playback_buffer);
    }
}

// =============================================================================
// Main Loop
// =============================================================================

static void main_loop(void) {
    LOG_INFO("Entering main loop");
    
    while (g_running) {
        // Update buttons (poll hardware)
        buttons_update();
        
        // Update device state machine
        device_update(&g_device_ctx);
        
        // Update radio (check for incoming packets)
        radio_update();
        
        // Update audio system
        audio_update();
        
        // Handle recording (for playback recording, not transmission)
        if (g_device_ctx.is_recording) {
            // Recording is handled by audio system when started
        }
        
        // Handle audio transmission (PTT-based)
        handle_audio_transmission();
        
        // Handle audio playback
        handle_audio_playback();
        
        // Small delay to prevent CPU hogging
        DELAY_MS(10);
    }
    
    // Cleanup
    LOG_INFO("Shutting down...");
    audio_stop_recording();
    audio_stop_playback();
    audio_deinit();
}

// =============================================================================
// Entry Point
// =============================================================================

#ifdef ESP32
void app_main(void) {
    init_system();
    main_loop();
}
#else
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    printf("=================================================\n");
    printf("  Advanced Walkie-Talkie - Console Mode\n");
    printf("  מכשיר קשר מתקדם\n");
    printf("=================================================\n\n");
    printf("Note: For full simulation with GUI, run:\n");
    printf("  cd simulator && python main.py\n\n");
    
    init_system();
    
    // In console mode, just show device ID and exit
    printf("Device initialized with ID: %s\n", g_device_ctx.device_id);
    printf("Current state: %s\n", device_state_name(g_device_ctx.current_state));
    printf("\nRadio: %s\n", radio_is_ready() ? "Ready" : "Not initialized");
    printf("Audio: %s\n", audio_is_initialized() ? "Ready" : "Not initialized");
    
    return 0;
}
#endif
