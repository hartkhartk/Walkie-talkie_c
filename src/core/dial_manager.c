/**
 * @file dial_manager.c
 * @brief מימוש ניהול גלגלת 15 המצבים עם threads
 */

#include "core/dial_manager.h"
#include "comm/protocol.h"
#include <string.h>
#include <stdio.h>

// =============================================================================
// Platform-Specific Includes
// =============================================================================

#ifdef ESP32
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/semphr.h"
    #include "nvs_flash.h"
    #include "nvs.h"
    #include "esp_log.h"
    
    static const char* TAG = "DIAL_MGR";
    #define LOG_INFO(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...) ESP_LOGD(TAG, fmt, ##__VA_ARGS__)
#else
    #include <pthread.h>
    #define LOG_INFO(fmt, ...) printf("[DIAL] " fmt "\n", ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) printf("[DIAL ERROR] " fmt "\n", ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...) 
#endif

// =============================================================================
// Forward Declarations
// =============================================================================

static void dial_connection_task(void* param);
static bool create_connection_thread(dial_manager_t* dm, uint8_t position);
static void destroy_connection_thread(dial_manager_t* dm, uint8_t position);

// =============================================================================
// Task Parameter Structure
// =============================================================================

typedef struct {
    dial_manager_t* dm;
    uint8_t position;
} dial_task_param_t;

// Static task parameters (one per slot)
static dial_task_param_t task_params[DIAL_POSITIONS];

// =============================================================================
// Initialization
// =============================================================================

void dial_manager_init(dial_manager_t* dm) {
    if (!dm) return;
    
    LOG_INFO("Initializing dial manager with %d positions", DIAL_POSITIONS);
    
    // Clear all slots
    memset(dm->slots, 0, sizeof(dm->slots));
    dm->current_position = 0;
    dm->active_threads = 0;
    
    // Initialize each slot
    for (int i = 0; i < DIAL_POSITIONS; i++) {
        dm->slots[i].is_configured = false;
        dm->slots[i].state = DIAL_SLOT_EMPTY;
        dm->slots[i].task_handle = NULL;
        
        // Initialize task parameters
        task_params[i].dm = dm;
        task_params[i].position = i;
    }
    
#ifdef ESP32
    // Create mutex for thread-safe access
    dm->mutex = xSemaphoreCreateMutex();
    if (!dm->mutex) {
        LOG_ERROR("Failed to create mutex");
    }
#endif
    
    // Try to load saved slots from NVS
    dial_manager_load_from_nvs(dm);
    
    LOG_INFO("Dial manager initialized");
}

// =============================================================================
// Position Management
// =============================================================================

bool dial_manager_set_position(dial_manager_t* dm, uint8_t position) {
    if (!dm || position >= DIAL_POSITIONS) return false;
    
#ifdef ESP32
    xSemaphoreTake(dm->mutex, portMAX_DELAY);
#endif
    
    uint8_t old_position = dm->current_position;
    dm->current_position = position;
    
    // Transfer active audio to new position if it's connected
    if (dm->slots[position].state == DIAL_SLOT_CONNECTED) {
        dial_manager_set_active_audio(dm, position);
    }
    
    LOG_DEBUG("Position changed: %d -> %d", old_position, position);
    
#ifdef ESP32
    xSemaphoreGive(dm->mutex);
#endif
    
    return true;
}

uint8_t dial_manager_rotate(dial_manager_t* dm, int8_t direction) {
    if (!dm) return 0;
    
    int8_t new_pos = (int8_t)dm->current_position + direction;
    
    // Wrap around
    if (new_pos < 0) new_pos = DIAL_POSITIONS - 1;
    if (new_pos >= DIAL_POSITIONS) new_pos = 0;
    
    dial_manager_set_position(dm, (uint8_t)new_pos);
    
    return dm->current_position;
}

// =============================================================================
// Slot Configuration
// =============================================================================

bool dial_manager_save_slot(dial_manager_t* dm, uint8_t position,
                           dial_connection_type_t conn_type,
                           const char* code, const char* name) {
    if (!dm || position >= DIAL_POSITIONS || !code) return false;
    
#ifdef ESP32
    xSemaphoreTake(dm->mutex, portMAX_DELAY);
#endif
    
    dial_slot_t* slot = &dm->slots[position];
    
    // If slot has active connection, disconnect first
    if (slot->state == DIAL_SLOT_CONNECTED || slot->state == DIAL_SLOT_CONNECTING) {
        destroy_connection_thread(dm, position);
    }
    
    // Save configuration
    slot->is_configured = true;
    slot->conn_type = conn_type;
    strncpy(slot->code, code, DEVICE_ID_LENGTH);
    slot->code[DEVICE_ID_LENGTH] = '\0';
    
    if (name) {
        strncpy(slot->name, name, 15);
        slot->name[15] = '\0';
    } else {
        snprintf(slot->name, 16, "Slot %d", position + 1);
    }
    
    slot->state = DIAL_SLOT_SAVED;
    
    LOG_INFO("Saved slot %d: %s (%s)", position, slot->code, 
             conn_type == DIAL_CONN_FREQUENCY ? "freq" : "device");
    
#ifdef ESP32
    xSemaphoreGive(dm->mutex);
#endif
    
    // Save to persistent storage
    dial_manager_save_to_nvs(dm);
    
    return true;
}

bool dial_manager_clear_slot(dial_manager_t* dm, uint8_t position) {
    if (!dm || position >= DIAL_POSITIONS) return false;
    
#ifdef ESP32
    xSemaphoreTake(dm->mutex, portMAX_DELAY);
#endif
    
    dial_slot_t* slot = &dm->slots[position];
    
    // Disconnect if connected
    if (slot->task_handle) {
        destroy_connection_thread(dm, position);
    }
    
    // Clear slot
    memset(slot, 0, sizeof(dial_slot_t));
    slot->state = DIAL_SLOT_EMPTY;
    
    LOG_INFO("Cleared slot %d", position);
    
#ifdef ESP32
    xSemaphoreGive(dm->mutex);
#endif
    
    dial_manager_save_to_nvs(dm);
    
    return true;
}

// =============================================================================
// Connection Management (Thread Creation/Destruction)
// =============================================================================

bool dial_manager_connect(dial_manager_t* dm, uint8_t position) {
    if (!dm || position >= DIAL_POSITIONS) return false;
    
    dial_slot_t* slot = &dm->slots[position];
    
    // Check if slot is configured
    if (!slot->is_configured) {
        LOG_ERROR("Cannot connect: slot %d not configured", position);
        return false;
    }
    
    // Check if already connected
    if (slot->state == DIAL_SLOT_CONNECTED) {
        LOG_DEBUG("Slot %d already connected", position);
        return true;
    }
    
    // Check thread limit
    if (dm->active_threads >= MAX_DIAL_THREADS) {
        LOG_ERROR("Cannot connect: max threads (%d) reached", MAX_DIAL_THREADS);
        return false;
    }
    
    // Create connection thread
    return create_connection_thread(dm, position);
}

bool dial_manager_disconnect(dial_manager_t* dm, uint8_t position) {
    if (!dm || position >= DIAL_POSITIONS) return false;
    
    dial_slot_t* slot = &dm->slots[position];
    
    if (slot->task_handle == NULL) {
        return true;  // Already disconnected
    }
    
    destroy_connection_thread(dm, position);
    
    // Update state back to SAVED if was configured
    if (slot->is_configured) {
        slot->state = DIAL_SLOT_SAVED;
    } else {
        slot->state = DIAL_SLOT_EMPTY;
    }
    
    return true;
}

void dial_manager_disconnect_all(dial_manager_t* dm) {
    if (!dm) return;
    
    LOG_INFO("Disconnecting all slots");
    
    for (int i = 0; i < DIAL_POSITIONS; i++) {
        dial_manager_disconnect(dm, i);
    }
}

// =============================================================================
// Thread Creation/Destruction
// =============================================================================

static bool create_connection_thread(dial_manager_t* dm, uint8_t position) {
    dial_slot_t* slot = &dm->slots[position];
    
#ifdef ESP32
    xSemaphoreTake(dm->mutex, portMAX_DELAY);
#endif
    
    slot->state = DIAL_SLOT_CONNECTING;
    
    LOG_INFO("Creating connection thread for slot %d", position);
    
#ifdef ESP32
    BaseType_t result = xTaskCreate(
        dial_connection_task,
        "dial_conn",
        DIAL_TASK_STACK_SIZE,
        &task_params[position],
        DIAL_TASK_PRIORITY,
        &slot->task_handle
    );
    
    if (result != pdPASS) {
        LOG_ERROR("Failed to create task for slot %d", position);
        slot->state = DIAL_SLOT_ERROR;
        xSemaphoreGive(dm->mutex);
        return false;
    }
#else
    // Simulator: just mark as connected
    slot->task_handle = (void*)1;  // Non-null to indicate "running"
    slot->state = DIAL_SLOT_CONNECTED;
#endif
    
    dm->active_threads++;
    
    LOG_INFO("Thread created for slot %d (total active: %d)", 
             position, dm->active_threads);
    
#ifdef ESP32
    xSemaphoreGive(dm->mutex);
#endif
    
    return true;
}

static void destroy_connection_thread(dial_manager_t* dm, uint8_t position) {
    dial_slot_t* slot = &dm->slots[position];
    
    if (!slot->task_handle) return;
    
    LOG_INFO("Destroying connection thread for slot %d", position);
    
#ifdef ESP32
    // Send disconnect message first
    protocol_send_disconnect();
    
    // Delete the task
    vTaskDelete(slot->task_handle);
#endif
    
    slot->task_handle = NULL;
    slot->is_active_audio = false;
    
    if (dm->active_threads > 0) {
        dm->active_threads--;
    }
    
    LOG_INFO("Thread destroyed for slot %d (total active: %d)", 
             position, dm->active_threads);
}

// =============================================================================
// Connection Task (runs in separate thread for each slot)
// =============================================================================

static void dial_connection_task(void* param) {
    dial_task_param_t* task_param = (dial_task_param_t*)param;
    dial_manager_t* dm = task_param->dm;
    uint8_t position = task_param->position;
    dial_slot_t* slot = &dm->slots[position];
    
    LOG_INFO("Connection task started for slot %d", position);
    
#ifdef ESP32
    // Send connection request based on type
    if (slot->conn_type == DIAL_CONN_FREQUENCY) {
        protocol_send_freq_join_request(slot->code, slot->password);
    } else {
        protocol_send_call_request(slot->code);
    }
    
    // Wait for connection response (with timeout)
    // This would normally be handled by callbacks from protocol layer
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // For now, assume connection succeeded
    xSemaphoreTake(dm->mutex, portMAX_DELAY);
    slot->state = DIAL_SLOT_CONNECTED;
    slot->connect_time = xTaskGetTickCount();
    xSemaphoreGive(dm->mutex);
    
    LOG_INFO("Slot %d connected", position);
    
    // Main connection loop
    while (slot->state == DIAL_SLOT_CONNECTED) {
        // Handle incoming audio if this is the active audio slot
        if (slot->is_active_audio) {
            // TODO: Process and play received audio
        }
        
        // Handle outgoing audio if active and not muted
        if (slot->is_active_audio && !slot->is_muted) {
            // TODO: Capture and send audio
        }
        
        // Small delay to prevent CPU hogging
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    LOG_INFO("Connection task ending for slot %d", position);
    
    // Clean up
    xSemaphoreTake(dm->mutex, portMAX_DELAY);
    slot->task_handle = NULL;
    if (dm->active_threads > 0) {
        dm->active_threads--;
    }
    xSemaphoreGive(dm->mutex);
    
    // Task deletes itself
    vTaskDelete(NULL);
#else
    // Simulator: nothing to do
    (void)dm;
    (void)position;
    (void)slot;
#endif
}

// =============================================================================
// Audio Management
// =============================================================================

bool dial_manager_set_active_audio(dial_manager_t* dm, uint8_t position) {
    if (!dm || position >= DIAL_POSITIONS) return false;
    
#ifdef ESP32
    xSemaphoreTake(dm->mutex, portMAX_DELAY);
#endif
    
    // Deactivate audio on all other slots
    for (int i = 0; i < DIAL_POSITIONS; i++) {
        dm->slots[i].is_active_audio = (i == position);
    }
    
    LOG_DEBUG("Active audio set to slot %d", position);
    
#ifdef ESP32
    xSemaphoreGive(dm->mutex);
#endif
    
    return true;
}

void dial_manager_set_muted(dial_manager_t* dm, uint8_t position, bool muted) {
    if (!dm || position >= DIAL_POSITIONS) return;
    
    dm->slots[position].is_muted = muted;
    LOG_DEBUG("Slot %d muted: %d", position, muted);
}

// =============================================================================
// Getters
// =============================================================================

uint8_t dial_manager_get_active_count(dial_manager_t* dm) {
    if (!dm) return 0;
    return dm->active_threads;
}

const dial_slot_t* dial_manager_get_slot(dial_manager_t* dm, uint8_t position) {
    if (!dm || position >= DIAL_POSITIONS) return NULL;
    return &dm->slots[position];
}

uint8_t dial_manager_get_position(dial_manager_t* dm) {
    if (!dm) return 0;
    return dm->current_position;
}

// =============================================================================
// NVS Persistence
// =============================================================================

#define NVS_NAMESPACE "dial_slots"
#define NVS_KEY_PREFIX "slot_"

bool dial_manager_save_to_nvs(dial_manager_t* dm) {
    if (!dm) return false;
    
#ifdef ESP32
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        LOG_ERROR("Failed to open NVS: %d", err);
        return false;
    }
    
    for (int i = 0; i < DIAL_POSITIONS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, i);
        
        dial_slot_t* slot = &dm->slots[i];
        
        if (slot->is_configured) {
            // Create a saveable structure (without runtime state)
            struct {
                uint8_t conn_type;
                char code[DEVICE_ID_LENGTH + 1];
                char name[16];
                char password[PASSWORD_MAX_LENGTH + 1];
            } save_data;
            
            save_data.conn_type = slot->conn_type;
            strncpy(save_data.code, slot->code, DEVICE_ID_LENGTH + 1);
            strncpy(save_data.name, slot->name, 16);
            strncpy(save_data.password, slot->password, PASSWORD_MAX_LENGTH + 1);
            
            err = nvs_set_blob(handle, key, &save_data, sizeof(save_data));
        } else {
            // Erase key if slot is empty
            nvs_erase_key(handle, key);
        }
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        LOG_ERROR("Failed to commit NVS: %d", err);
        return false;
    }
    
    LOG_INFO("Dial slots saved to NVS");
    return true;
#else
    // Simulator: no persistence
    return true;
#endif
}

bool dial_manager_load_from_nvs(dial_manager_t* dm) {
    if (!dm) return false;
    
#ifdef ESP32
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        LOG_DEBUG("No saved dial slots found");
        return false;
    }
    
    int loaded = 0;
    
    for (int i = 0; i < DIAL_POSITIONS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, i);
        
        struct {
            uint8_t conn_type;
            char code[DEVICE_ID_LENGTH + 1];
            char name[16];
            char password[PASSWORD_MAX_LENGTH + 1];
        } save_data;
        
        size_t size = sizeof(save_data);
        err = nvs_get_blob(handle, key, &save_data, &size);
        
        if (err == ESP_OK) {
            dial_slot_t* slot = &dm->slots[i];
            slot->is_configured = true;
            slot->conn_type = save_data.conn_type;
            strncpy(slot->code, save_data.code, DEVICE_ID_LENGTH + 1);
            strncpy(slot->name, save_data.name, 16);
            strncpy(slot->password, save_data.password, PASSWORD_MAX_LENGTH + 1);
            slot->state = DIAL_SLOT_SAVED;
            loaded++;
        }
    }
    
    nvs_close(handle);
    
    LOG_INFO("Loaded %d dial slots from NVS", loaded);
    return loaded > 0;
#else
    // Simulator: no persistence
    return false;
#endif
}

