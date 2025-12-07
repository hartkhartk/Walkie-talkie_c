/**
 * @file device_state.c
 * @brief מימוש מכונת המצבים של המכשיר
 */

#include "core/device_state.h"
#include "hal/buttons.h"
#include "hal/display.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// Platform-Specific Time
// =============================================================================

#ifdef ESP32
    #include "esp_timer.h"
    #include "esp_random.h"
    #define GET_MILLIS() (esp_timer_get_time() / 1000)
    #define GET_RANDOM() esp_random()
#else
    #include <time.h>
    static uint32_t sim_millis(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
    }
    #define GET_MILLIS() sim_millis()
    #define GET_RANDOM() ((uint32_t)rand())
#endif

// =============================================================================
// Forward Declarations
// =============================================================================

static void render_state(device_context_t* ctx);
static void handle_idle_button(device_context_t* ctx, button_id_t btn, button_event_t event);
static void handle_in_call_button(device_context_t* ctx, button_id_t btn, button_event_t event);
static void handle_in_frequency_button(device_context_t* ctx, button_id_t btn, button_event_t event);
static void handle_input_code_button(device_context_t* ctx, button_id_t btn, button_event_t event);
static void handle_scanning_button(device_context_t* ctx, button_id_t btn, button_event_t event);
static void handle_scan_results_button(device_context_t* ctx, button_id_t btn, button_event_t event);
static void handle_saved_list_button(device_context_t* ctx, button_id_t btn, button_event_t event);
static void handle_invite_menu_button(device_context_t* ctx, button_id_t btn, button_event_t event);
static void handle_freq_create_type_button(device_context_t* ctx, button_id_t btn, button_event_t event);
static void handle_freq_create_protect_button(device_context_t* ctx, button_id_t btn, button_event_t event);
static void handle_password_entry_button(device_context_t* ctx, button_id_t btn, button_event_t event);
static void handle_incoming_request_button(device_context_t* ctx, button_id_t btn, button_event_t event);

// =============================================================================
// ID Generation - מספרים בלבד (0-9)
// =============================================================================

/**
 * @brief יצירת מזהה מכשיר ייחודי (8 ספרות)
 * המזהה נשמר לצמיתות ולא משתנה
 */
static void generate_device_id(char* id_out) {
    // Generate 8 random digits (0-9 only)
    for (int i = 0; i < DEVICE_ID_LENGTH; i++) {
        id_out[i] = '0' + (GET_RANDOM() % 10);
    }
    id_out[DEVICE_ID_LENGTH] = '\0';
}

/**
 * @brief יצירת מזהה תדר (8 ספרות)
 * המזהה ייחודי רק בזמן שהתדר פעיל
 * לאחר סגירת התדר, הקוד חוזר להיות פנוי לשימוש חוזר
 */
static void generate_frequency_id(char* id_out) {
    // Generate 8 random digits (0-9 only)
    for (int i = 0; i < FREQUENCY_ID_LENGTH; i++) {
        id_out[i] = '0' + (GET_RANDOM() % 10);
    }
    id_out[FREQUENCY_ID_LENGTH] = '\0';
}

void device_init(device_context_t* ctx) {
    memset(ctx, 0, sizeof(device_context_t));
    
    // Generate unique device ID
    generate_device_id(ctx->device_id);
    
    // Initial state
    ctx->current_state = STATE_IDLE;
    ctx->previous_state = STATE_IDLE;
    
    // Default values
    ctx->battery_level = 100;
    ctx->signal_strength = 75;
    ctx->is_visible = true;
    ctx->is_connected = false;
    ctx->is_muted = false;
    ctx->is_recording = false;
    
    ctx->state_enter_time = GET_MILLIS();
    ctx->last_activity_time = GET_MILLIS();
}

// =============================================================================
// State Management
// =============================================================================

void device_set_state(device_context_t* ctx, device_state_t new_state) {
    ctx->previous_state = ctx->current_state;
    ctx->current_state = new_state;
    ctx->state_enter_time = GET_MILLIS();
    ctx->last_activity_time = GET_MILLIS();
    
    // Clear input on state change (except for specific transitions)
    if (new_state == STATE_INPUT_CODE || 
        new_state == STATE_FREQ_CREATE_PASSWORD ||
        new_state == STATE_PASSWORD_ENTRY) {
        device_clear_input(ctx);
    }
    
    // Initialize state-specific data
    switch (new_state) {
        case STATE_SCANNING:
            ctx->scan_result_count = 0;
            ctx->scan_selected_index = 0;
            break;
        case STATE_SCAN_RESULTS:
        case STATE_SAVED_LIST:
        case STATE_INVITE_MENU:
            ctx->scan_selected_index = 0;
            ctx->saved_selected_index = 0;
            break;
        default:
            break;
    }
    
    // Render the new state
    render_state(ctx);
}

void device_go_back(device_context_t* ctx) {
    device_set_state(ctx, ctx->previous_state);
}

const char* device_state_name(device_state_t state) {
    switch (state) {
        case STATE_IDLE: return "IDLE";
        case STATE_IN_CALL: return "IN_CALL";
        case STATE_IN_FREQUENCY: return "IN_FREQ";
        case STATE_INPUT_CODE: return "INPUT";
        case STATE_SCANNING: return "SCANNING";
        case STATE_SCAN_RESULTS: return "RESULTS";
        case STATE_SAVED_LIST: return "SAVED";
        case STATE_INVITE_MENU: return "INVITE";
        case STATE_FREQ_CREATE_TYPE: return "FREQ_TYPE";
        case STATE_FREQ_CREATE_PROTECT: return "FREQ_PROT";
        case STATE_FREQ_CREATE_PASSWORD: return "FREQ_PASS";
        case STATE_WAITING_RESPONSE: return "WAITING";
        case STATE_INCOMING_REQUEST: return "INCOMING";
        case STATE_PASSWORD_ENTRY: return "PASSWORD";
        case STATE_MESSAGE: return "MESSAGE";
        case STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// Input Handling
// =============================================================================

void device_input_digit(device_context_t* ctx, uint8_t digit) {
    if (digit > 9) return;
    
    size_t max_len = FREQUENCY_ID_LENGTH;
    if (ctx->current_state == STATE_FREQ_CREATE_PASSWORD ||
        ctx->current_state == STATE_PASSWORD_ENTRY) {
        max_len = PASSWORD_MAX_LENGTH;
    }
    
    if (ctx->input_cursor < max_len) {
        ctx->input_buffer[ctx->input_cursor++] = '0' + digit;
        ctx->input_buffer[ctx->input_cursor] = '\0';
        ctx->last_activity_time = GET_MILLIS();
        render_state(ctx);
    }
}

void device_clear_input(device_context_t* ctx) {
    memset(ctx->input_buffer, 0, sizeof(ctx->input_buffer));
    ctx->input_cursor = 0;
}

// =============================================================================
// Saved Codes Management
// =============================================================================

bool device_save_code(device_context_t* ctx, bool is_frequency, 
                      const char* code, const char* name) {
    if (ctx->saved_code_count >= MAX_SAVED_CODES) {
        return false;
    }
    
    // Check if already saved
    for (uint8_t i = 0; i < ctx->saved_code_count; i++) {
        if (strcmp(ctx->saved_codes[i].code, code) == 0) {
            return false; // Already exists
        }
    }
    
    saved_code_t* entry = &ctx->saved_codes[ctx->saved_code_count++];
    entry->is_frequency = is_frequency;
    strncpy(entry->code, code, FREQUENCY_ID_LENGTH);
    entry->code[FREQUENCY_ID_LENGTH] = '\0';
    
    if (name && name[0]) {
        strncpy(entry->name, name, 15);
        entry->name[15] = '\0';
    } else {
        snprintf(entry->name, 16, "%s", is_frequency ? "Freq" : "Device");
    }
    
    return true;
}

bool device_delete_saved_code(device_context_t* ctx, uint8_t index) {
    if (index >= ctx->saved_code_count) {
        return false;
    }
    
    // Shift remaining entries
    for (uint8_t i = index; i < ctx->saved_code_count - 1; i++) {
        ctx->saved_codes[i] = ctx->saved_codes[i + 1];
    }
    ctx->saved_code_count--;
    
    return true;
}

// =============================================================================
// Rendering Functions
// =============================================================================

static void render_idle(device_context_t* ctx) {
    display_clear();
    
    // Status bar
    display_status_bar(ctx->battery_level, ctx->signal_strength,
                       ctx->is_recording, ctx->is_visible);
    
    // Device ID
    char id_str[24];
    snprintf(id_str, sizeof(id_str), "ID: %s", ctx->device_id);
    display_print_aligned(12, id_str, FONT_SMALL, ALIGN_CENTER);
    
    // Instructions
    display_print_aligned(24, "Enter code + GREEN", FONT_SMALL, ALIGN_CENTER);
    display_print_aligned(32, "MULTI: Scan", FONT_SMALL, ALIGN_CENTER);
    display_print_aligned(40, "MULTI long: New Freq", FONT_SMALL, ALIGN_CENTER);
    
    // Input field
    display_input_field("", ctx->input_buffer, ctx->input_cursor, FREQUENCY_ID_LENGTH);
    
    display_update();
}

static void render_input_code(device_context_t* ctx) {
    display_clear();
    display_status_bar(ctx->battery_level, ctx->signal_strength,
                       ctx->is_recording, ctx->is_visible);
    
    display_print_aligned(12, "Enter Code:", FONT_MEDIUM, ALIGN_CENTER);
    display_input_field("", ctx->input_buffer, ctx->input_cursor, FREQUENCY_ID_LENGTH);
    
    display_print_aligned(48, "GREEN=Connect RED=Back", FONT_SMALL, ALIGN_CENTER);
    
    display_update();
}

static void render_in_call(device_context_t* ctx) {
    display_clear();
    display_status_bar(ctx->battery_level, ctx->signal_strength,
                       ctx->is_recording, ctx->is_visible);
    
    display_icon(4, 16, ICON_CALL);
    display_print(16, 16, "CALL", FONT_MEDIUM);
    
    char id_str[20];
    snprintf(id_str, sizeof(id_str), "-> %s", ctx->current_connection.device.id);
    display_print_aligned(28, id_str, FONT_SMALL, ALIGN_CENTER);
    
    // Mute indicator
    if (ctx->is_muted) {
        display_icon(DISPLAY_WIDTH/2 - 4, 38, ICON_MICROPHONE_MUTED);
        display_print_aligned(48, "MUTED", FONT_SMALL, ALIGN_CENTER);
    }
    
    // Controls hint
    display_print_aligned(56, "GRN=Mute RED=End", FONT_SMALL, ALIGN_CENTER);
    
    display_update();
}

static void render_in_frequency(device_context_t* ctx) {
    display_clear();
    display_status_bar(ctx->battery_level, ctx->signal_strength,
                       ctx->is_recording, ctx->is_visible);
    
    display_icon(4, 16, ICON_FREQUENCY);
    display_print(16, 16, "FREQ", FONT_MEDIUM);
    
    char id_str[20];
    snprintf(id_str, sizeof(id_str), "[%s]", ctx->current_connection.frequency.id);
    display_print_aligned(28, id_str, FONT_SMALL, ALIGN_CENTER);
    
    // Member count
    char members[16];
    snprintf(members, sizeof(members), "Members: %d", 
             ctx->current_connection.frequency.member_count);
    display_print_aligned(38, members, FONT_SMALL, ALIGN_CENTER);
    
    // Admin indicator
    if (ctx->current_connection.frequency.is_admin) {
        display_print(0, 48, "*ADMIN*", FONT_SMALL);
    }
    
    // Mute indicator
    if (ctx->is_muted) {
        display_icon(DISPLAY_WIDTH - 16, 48, ICON_MICROPHONE_MUTED);
    }
    
    display_update();
}

static void render_scanning(device_context_t* ctx) {
    display_clear();
    display_status_bar(ctx->battery_level, ctx->signal_strength,
                       ctx->is_recording, ctx->is_visible);
    
    display_print_aligned(20, "Scanning...", FONT_MEDIUM, ALIGN_CENTER);
    
    // Progress animation based on time
    uint32_t elapsed = GET_MILLIS() - ctx->state_enter_time;
    uint8_t progress = (elapsed * 100) / SCAN_TIMEOUT;
    if (progress > 100) progress = 100;
    
    display_progress_bar(16, 35, 96, progress);
    
    char found[20];
    snprintf(found, sizeof(found), "Found: %d", ctx->scan_result_count);
    display_print_aligned(48, found, FONT_SMALL, ALIGN_CENTER);
    
    display_update();
}

static void render_scan_results(device_context_t* ctx) {
    display_clear();
    display_status_bar(ctx->battery_level, ctx->signal_strength,
                       ctx->is_recording, ctx->is_visible);
    
    if (ctx->scan_result_count == 0) {
        display_print_aligned(28, "No results", FONT_MEDIUM, ALIGN_CENTER);
        display_print_aligned(42, "RED=Back MULTI=Rescan", FONT_SMALL, ALIGN_CENTER);
    } else {
        // Create list items
        static char list_items[MAX_SCAN_RESULTS][24];
        static const char* list_ptrs[MAX_SCAN_RESULTS];
        
        for (uint8_t i = 0; i < ctx->scan_result_count; i++) {
            scan_result_t* r = &ctx->scan_results[i];
            if (r->is_frequency) {
                snprintf(list_items[i], 24, "F:%s [%d]", 
                         r->info.frequency.id,
                         r->info.frequency.member_count);
            } else {
                snprintf(list_items[i], 24, "D:%s", r->info.device.id);
            }
            list_ptrs[i] = list_items[i];
        }
        
        uint8_t scroll = 0;
        if (ctx->scan_selected_index >= 5) {
            scroll = ctx->scan_selected_index - 4;
        }
        
        display_list(list_ptrs, ctx->scan_result_count, 
                     ctx->scan_selected_index, scroll);
    }
    
    display_update();
}

static void render_saved_list(device_context_t* ctx) {
    display_clear();
    display_status_bar(ctx->battery_level, ctx->signal_strength,
                       ctx->is_recording, ctx->is_visible);
    
    display_print_aligned(0, "Saved Codes", FONT_SMALL, ALIGN_CENTER);
    
    if (ctx->saved_code_count == 0) {
        display_print_aligned(28, "No saved codes", FONT_SMALL, ALIGN_CENTER);
    } else {
        static char list_items[MAX_SAVED_CODES][24];
        static const char* list_ptrs[MAX_SAVED_CODES];
        
        for (uint8_t i = 0; i < ctx->saved_code_count; i++) {
            saved_code_t* s = &ctx->saved_codes[i];
            snprintf(list_items[i], 24, "%c %s %s",
                     s->is_frequency ? 'F' : 'D',
                     s->code, s->name);
            list_ptrs[i] = list_items[i];
        }
        
        uint8_t scroll = 0;
        if (ctx->saved_selected_index >= 5) {
            scroll = ctx->saved_selected_index - 4;
        }
        
        display_list(list_ptrs, ctx->saved_code_count,
                     ctx->saved_selected_index, scroll);
    }
    
    display_update();
}

static void render_freq_create_type(device_context_t* ctx) {
    display_clear();
    display_status_bar(ctx->battery_level, ctx->signal_strength,
                       ctx->is_recording, ctx->is_visible);
    
    display_print_aligned(12, "Create Frequency", FONT_MEDIUM, ALIGN_CENTER);
    display_print_aligned(28, "1 = Visible", FONT_SMALL, ALIGN_CENTER);
    display_print_aligned(38, "2 = Hidden", FONT_SMALL, ALIGN_CENTER);
    display_print_aligned(52, "RED = Cancel", FONT_SMALL, ALIGN_CENTER);
    
    display_update();
}

static void render_freq_create_protect(device_context_t* ctx) {
    display_clear();
    display_status_bar(ctx->battery_level, ctx->signal_strength,
                       ctx->is_recording, ctx->is_visible);
    
    display_print_aligned(8, "Protection:", FONT_MEDIUM, ALIGN_CENTER);
    display_print_aligned(20, "1 = None", FONT_SMALL, ALIGN_CENTER);
    display_print_aligned(28, "2 = Password", FONT_SMALL, ALIGN_CENTER);
    display_print_aligned(36, "3 = Approval", FONT_SMALL, ALIGN_CENTER);
    display_print_aligned(44, "4 = Both", FONT_SMALL, ALIGN_CENTER);
    display_print_aligned(56, "RED = Cancel", FONT_SMALL, ALIGN_CENTER);
    
    display_update();
}

static void render_password_entry(device_context_t* ctx) {
    display_clear();
    display_status_bar(ctx->battery_level, ctx->signal_strength,
                       ctx->is_recording, ctx->is_visible);
    
    display_print_aligned(12, "Enter Password:", FONT_MEDIUM, ALIGN_CENTER);
    display_input_field("", ctx->input_buffer, ctx->input_cursor, PASSWORD_MAX_LENGTH);
    display_print_aligned(48, "GREEN=OK RED=Back", FONT_SMALL, ALIGN_CENTER);
    
    display_update();
}

static void render_waiting_response(device_context_t* ctx) {
    display_clear();
    display_status_bar(ctx->battery_level, ctx->signal_strength,
                       ctx->is_recording, ctx->is_visible);
    
    display_print_aligned(20, "Waiting...", FONT_MEDIUM, ALIGN_CENTER);
    
    uint32_t elapsed = GET_MILLIS() - ctx->state_enter_time;
    uint8_t progress = (elapsed * 100) / CALL_TIMEOUT;
    if (progress > 100) progress = 100;
    display_progress_bar(16, 35, 96, progress);
    
    display_print_aligned(52, "RED = Cancel", FONT_SMALL, ALIGN_CENTER);
    
    display_update();
}

static void render_incoming_request(device_context_t* ctx) {
    display_clear();
    display_status_bar(ctx->battery_level, ctx->signal_strength,
                       ctx->is_recording, ctx->is_visible);
    
    display_print_aligned(16, "Incoming", FONT_MEDIUM, ALIGN_CENTER);
    display_print_aligned(28, ctx->message_text, FONT_SMALL, ALIGN_CENTER);
    display_confirm_dialog("", "", "Accept", "Reject");
    
    display_update();
}

static void render_message(device_context_t* ctx) {
    display_message(ctx->message_title, ctx->message_text);
}

static void render_invite_menu(device_context_t* ctx) {
    display_clear();
    display_status_bar(ctx->battery_level, ctx->signal_strength,
                       ctx->is_recording, ctx->is_visible);
    
    display_print_aligned(0, "Invite Device", FONT_SMALL, ALIGN_CENTER);
    
    // First option: manual entry
    display_print_line(1, "> Enter code manually", ctx->scan_selected_index == 0);
    
    // Then saved devices
    for (uint8_t i = 0; i < ctx->saved_code_count && i < 4; i++) {
        if (!ctx->saved_codes[i].is_frequency) {
            display_print_line(2 + i, ctx->saved_codes[i].name, 
                              ctx->scan_selected_index == i + 1);
        }
    }
    
    display_update();
}

static void render_state(device_context_t* ctx) {
    switch (ctx->current_state) {
        case STATE_IDLE:
            render_idle(ctx);
            break;
        case STATE_INPUT_CODE:
            render_input_code(ctx);
            break;
        case STATE_IN_CALL:
            render_in_call(ctx);
            break;
        case STATE_IN_FREQUENCY:
            render_in_frequency(ctx);
            break;
        case STATE_SCANNING:
            render_scanning(ctx);
            break;
        case STATE_SCAN_RESULTS:
            render_scan_results(ctx);
            break;
        case STATE_SAVED_LIST:
            render_saved_list(ctx);
            break;
        case STATE_FREQ_CREATE_TYPE:
            render_freq_create_type(ctx);
            break;
        case STATE_FREQ_CREATE_PROTECT:
            render_freq_create_protect(ctx);
            break;
        case STATE_FREQ_CREATE_PASSWORD:
        case STATE_PASSWORD_ENTRY:
            render_password_entry(ctx);
            break;
        case STATE_WAITING_RESPONSE:
            render_waiting_response(ctx);
            break;
        case STATE_INCOMING_REQUEST:
            render_incoming_request(ctx);
            break;
        case STATE_INVITE_MENU:
            render_invite_menu(ctx);
            break;
        case STATE_MESSAGE:
        case STATE_ERROR:
            render_message(ctx);
            break;
    }
}

// =============================================================================
// Button Handlers
// =============================================================================

static void handle_idle_button(device_context_t* ctx, button_id_t btn, button_event_t event) {
    if (event == BTN_EVENT_PRESS) {
        // Digit input
        if (btn >= BTN_0 && btn <= BTN_9) {
            device_input_digit(ctx, btn - BTN_0);
            return;
        }
        
        switch (btn) {
            case BTN_GREEN:
                // Connect using input code
                if (ctx->input_cursor > 0) {
                    // TODO: Send connection request
                    device_set_state(ctx, STATE_WAITING_RESPONSE);
                }
                break;
                
            case BTN_RED:
                // Clear input
                device_clear_input(ctx);
                render_state(ctx);
                break;
                
            case BTN_ABOVE_GREEN:
                // Show saved list
                device_set_state(ctx, STATE_SAVED_LIST);
                break;
                
            case BTN_MULTI:
                // Start scanning
                device_set_state(ctx, STATE_SCANNING);
                break;
                
            default:
                break;
        }
    } else if (event == BTN_EVENT_LONG_PRESS) {
        if (btn == BTN_MULTI) {
            // Create new frequency
            device_set_state(ctx, STATE_FREQ_CREATE_TYPE);
        }
    }
}

static void handle_in_call_button(device_context_t* ctx, button_id_t btn, button_event_t event) {
    if (event != BTN_EVENT_PRESS) return;
    
    switch (btn) {
        case BTN_GREEN:
            // Toggle mute
            ctx->is_muted = !ctx->is_muted;
            render_state(ctx);
            break;
            
        case BTN_RED:
            // End call
            ctx->is_connected = false;
            device_set_state(ctx, STATE_IDLE);
            break;
            
        case BTN_ABOVE_GREEN:
            // Save this device
            device_save_code(ctx, false, 
                           ctx->current_connection.device.id, NULL);
            strcpy(ctx->message_title, "Saved");
            strcpy(ctx->message_text, "Code saved!");
            ctx->message_timeout = 1500;
            device_set_state(ctx, STATE_MESSAGE);
            break;
            
        case BTN_MULTI:
            // Invite menu (can invite to create a frequency with this person)
            device_set_state(ctx, STATE_INVITE_MENU);
            break;
            
        default:
            break;
    }
}

static void handle_in_frequency_button(device_context_t* ctx, button_id_t btn, button_event_t event) {
    if (event != BTN_EVENT_PRESS) return;
    
    switch (btn) {
        case BTN_GREEN:
            // Toggle mute
            ctx->is_muted = !ctx->is_muted;
            render_state(ctx);
            break;
            
        case BTN_RED:
            // Leave frequency (or delete if admin)
            if (ctx->current_connection.frequency.is_admin) {
                // TODO: Delete frequency and disconnect all
            }
            ctx->is_connected = false;
            device_set_state(ctx, STATE_IDLE);
            break;
            
        case BTN_ABOVE_GREEN:
            // Save this frequency
            device_save_code(ctx, true,
                           ctx->current_connection.frequency.id, NULL);
            strcpy(ctx->message_title, "Saved");
            strcpy(ctx->message_text, "Frequency saved!");
            ctx->message_timeout = 1500;
            device_set_state(ctx, STATE_MESSAGE);
            break;
            
        case BTN_MULTI:
            // Invite devices
            if (ctx->current_connection.frequency.is_admin) {
                device_set_state(ctx, STATE_INVITE_MENU);
            }
            break;
            
        default:
            break;
    }
}

static void handle_scanning_button(device_context_t* ctx, button_id_t btn, button_event_t event) {
    if (event != BTN_EVENT_PRESS) return;
    
    if (btn == BTN_RED) {
        // Cancel scanning
        device_set_state(ctx, STATE_IDLE);
    }
}

static void handle_scan_results_button(device_context_t* ctx, button_id_t btn, button_event_t event) {
    if (event != BTN_EVENT_PRESS) return;
    
    switch (btn) {
        case BTN_ABOVE_GREEN:
            // Navigate up
            if (ctx->scan_selected_index > 0) {
                ctx->scan_selected_index--;
                render_state(ctx);
            }
            break;
            
        case BTN_ABOVE_RED:
            // Navigate down
            if (ctx->scan_selected_index < ctx->scan_result_count - 1) {
                ctx->scan_selected_index++;
                render_state(ctx);
            }
            break;
            
        case BTN_GREEN:
            // Connect to selected
            if (ctx->scan_result_count > 0) {
                scan_result_t* selected = &ctx->scan_results[ctx->scan_selected_index];
                if (selected->is_frequency) {
                    // Copy frequency info
                    ctx->connected_to_frequency = true;
                    ctx->current_connection.frequency = selected->info.frequency;
                    
                    // Check if password needed
                    if (selected->info.frequency.protection == FREQ_PROTECT_PASSWORD ||
                        selected->info.frequency.protection == FREQ_PROTECT_BOTH) {
                        device_set_state(ctx, STATE_PASSWORD_ENTRY);
                    } else {
                        device_set_state(ctx, STATE_WAITING_RESPONSE);
                    }
                } else {
                    // Start call
                    ctx->connected_to_frequency = false;
                    ctx->current_connection.device = selected->info.device;
                    device_set_state(ctx, STATE_WAITING_RESPONSE);
                }
            }
            break;
            
        case BTN_RED:
            // Back to idle
            device_set_state(ctx, STATE_IDLE);
            break;
            
        case BTN_MULTI:
            // Rescan
            device_set_state(ctx, STATE_SCANNING);
            break;
            
        default:
            break;
    }
}

static void handle_saved_list_button(device_context_t* ctx, button_id_t btn, button_event_t event) {
    if (event != BTN_EVENT_PRESS) return;
    
    switch (btn) {
        case BTN_ABOVE_GREEN:
            // Navigate up
            if (ctx->saved_selected_index > 0) {
                ctx->saved_selected_index--;
                render_state(ctx);
            }
            break;
            
        case BTN_ABOVE_RED:
            // Navigate down
            if (ctx->saved_selected_index < ctx->saved_code_count - 1) {
                ctx->saved_selected_index++;
                render_state(ctx);
            }
            break;
            
        case BTN_GREEN:
            // Connect to selected
            if (ctx->saved_code_count > 0) {
                saved_code_t* selected = &ctx->saved_codes[ctx->saved_selected_index];
                strcpy(ctx->input_buffer, selected->code);
                ctx->input_cursor = strlen(selected->code);
                device_set_state(ctx, STATE_WAITING_RESPONSE);
            }
            break;
            
        case BTN_RED:
            // Back
            device_go_back(ctx);
            break;
            
        default:
            break;
    }
}

static void handle_freq_create_type_button(device_context_t* ctx, button_id_t btn, button_event_t event) {
    if (event != BTN_EVENT_PRESS) return;
    
    switch (btn) {
        case BTN_1:
            ctx->new_freq_type = FREQ_TYPE_VISIBLE;
            device_set_state(ctx, STATE_FREQ_CREATE_PROTECT);
            break;
            
        case BTN_2:
            ctx->new_freq_type = FREQ_TYPE_HIDDEN;
            device_set_state(ctx, STATE_FREQ_CREATE_PROTECT);
            break;
            
        case BTN_RED:
            device_set_state(ctx, STATE_IDLE);
            break;
            
        default:
            break;
    }
}

static void handle_freq_create_protect_button(device_context_t* ctx, button_id_t btn, button_event_t event) {
    if (event != BTN_EVENT_PRESS) return;
    
    switch (btn) {
        case BTN_1:
            ctx->new_freq_protection = FREQ_PROTECT_NONE;
            // Create frequency immediately
            // Create frequency with no protection
            ctx->is_connected = true;
            ctx->connected_to_frequency = true;
            ctx->current_connection.frequency.is_admin = true;
            ctx->current_connection.frequency.type = ctx->new_freq_type;
            ctx->current_connection.frequency.protection = ctx->new_freq_protection;
            ctx->current_connection.frequency.member_count = 1;
            generate_frequency_id(ctx->current_connection.frequency.id);
            device_set_state(ctx, STATE_IN_FREQUENCY);
            break;
            
        case BTN_2:
            ctx->new_freq_protection = FREQ_PROTECT_PASSWORD;
            device_set_state(ctx, STATE_FREQ_CREATE_PASSWORD);
            break;
            
        case BTN_3:
            ctx->new_freq_protection = FREQ_PROTECT_APPROVAL;
            // Create frequency with approval required
            ctx->is_connected = true;
            ctx->connected_to_frequency = true;
            ctx->current_connection.frequency.is_admin = true;
            ctx->current_connection.frequency.type = ctx->new_freq_type;
            ctx->current_connection.frequency.protection = ctx->new_freq_protection;
            ctx->current_connection.frequency.member_count = 1;
            generate_frequency_id(ctx->current_connection.frequency.id);
            device_set_state(ctx, STATE_IN_FREQUENCY);
            break;
            
        case BTN_4:
            ctx->new_freq_protection = FREQ_PROTECT_BOTH;
            device_set_state(ctx, STATE_FREQ_CREATE_PASSWORD);
            break;
            
        case BTN_RED:
            device_set_state(ctx, STATE_IDLE);
            break;
            
        default:
            break;
    }
}

static void handle_password_entry_button(device_context_t* ctx, button_id_t btn, button_event_t event) {
    if (event != BTN_EVENT_PRESS) return;
    
    if (btn >= BTN_0 && btn <= BTN_9) {
        device_input_digit(ctx, btn - BTN_0);
        return;
    }
    
    switch (btn) {
        case BTN_GREEN:
            if (ctx->input_cursor > 0) {
                if (ctx->previous_state == STATE_FREQ_CREATE_PROTECT) {
                    // Creating frequency with password
                    strcpy(ctx->temp_password, ctx->input_buffer);
                    ctx->is_connected = true;
                    ctx->connected_to_frequency = true;
                    ctx->current_connection.frequency.is_admin = true;
                    ctx->current_connection.frequency.type = ctx->new_freq_type;
                    ctx->current_connection.frequency.protection = ctx->new_freq_protection;
                    ctx->current_connection.frequency.member_count = 1;
                    generate_frequency_id(ctx->current_connection.frequency.id);
                    device_set_state(ctx, STATE_IN_FREQUENCY);
                } else {
                    // Joining frequency with password
                    strcpy(ctx->temp_password, ctx->input_buffer);
                    device_set_state(ctx, STATE_WAITING_RESPONSE);
                }
            }
            break;
            
        case BTN_RED:
            device_go_back(ctx);
            break;
            
        default:
            break;
    }
}

static void handle_incoming_request_button(device_context_t* ctx, button_id_t btn, button_event_t event) {
    if (event != BTN_EVENT_PRESS) return;
    
    switch (btn) {
        case BTN_GREEN:
            // Accept
            // TODO: Send accept and connect
            ctx->is_connected = true;
            if (ctx->connected_to_frequency) {
                device_set_state(ctx, STATE_IN_FREQUENCY);
            } else {
                device_set_state(ctx, STATE_IN_CALL);
            }
            break;
            
        case BTN_RED:
            // Reject
            device_set_state(ctx, STATE_IDLE);
            break;
            
        default:
            break;
    }
}

static void handle_invite_menu_button(device_context_t* ctx, button_id_t btn, button_event_t event) {
    if (event != BTN_EVENT_PRESS) return;
    
    switch (btn) {
        case BTN_ABOVE_GREEN:
            if (ctx->scan_selected_index > 0) {
                ctx->scan_selected_index--;
                render_state(ctx);
            }
            break;
            
        case BTN_ABOVE_RED:
            ctx->scan_selected_index++;
            render_state(ctx);
            break;
            
        case BTN_GREEN:
            if (ctx->scan_selected_index == 0) {
                // Manual entry
                device_set_state(ctx, STATE_INPUT_CODE);
            } else {
                // TODO: Send invite to selected saved device
            }
            break;
            
        case BTN_RED:
            device_go_back(ctx);
            break;
            
        default:
            break;
    }
}

// =============================================================================
// Main Button Handler
// =============================================================================

void device_handle_button(device_context_t* ctx, button_id_t button, button_event_t event) {
    ctx->last_activity_time = GET_MILLIS();
    
    switch (ctx->current_state) {
        case STATE_IDLE:
            handle_idle_button(ctx, button, event);
            break;
        case STATE_IN_CALL:
            handle_in_call_button(ctx, button, event);
            break;
        case STATE_IN_FREQUENCY:
            handle_in_frequency_button(ctx, button, event);
            break;
        case STATE_INPUT_CODE:
            handle_input_code_button(ctx, button, event);
            break;
        case STATE_SCANNING:
            handle_scanning_button(ctx, button, event);
            break;
        case STATE_SCAN_RESULTS:
            handle_scan_results_button(ctx, button, event);
            break;
        case STATE_SAVED_LIST:
            handle_saved_list_button(ctx, button, event);
            break;
        case STATE_INVITE_MENU:
            handle_invite_menu_button(ctx, button, event);
            break;
        case STATE_FREQ_CREATE_TYPE:
            handle_freq_create_type_button(ctx, button, event);
            break;
        case STATE_FREQ_CREATE_PROTECT:
            handle_freq_create_protect_button(ctx, button, event);
            break;
        case STATE_FREQ_CREATE_PASSWORD:
        case STATE_PASSWORD_ENTRY:
            handle_password_entry_button(ctx, button, event);
            break;
        case STATE_INCOMING_REQUEST:
            handle_incoming_request_button(ctx, button, event);
            break;
        case STATE_MESSAGE:
        case STATE_ERROR:
            // Any button dismisses
            if (event == BTN_EVENT_PRESS) {
                device_go_back(ctx);
            }
            break;
        default:
            break;
    }
}

static void handle_input_code_button(device_context_t* ctx, button_id_t btn, button_event_t event) {
    if (event != BTN_EVENT_PRESS) return;
    
    if (btn >= BTN_0 && btn <= BTN_9) {
        device_input_digit(ctx, btn - BTN_0);
        return;
    }
    
    switch (btn) {
        case BTN_GREEN:
            if (ctx->input_cursor > 0) {
                device_set_state(ctx, STATE_WAITING_RESPONSE);
            }
            break;
        case BTN_RED:
            device_go_back(ctx);
            break;
        default:
            break;
    }
}

// =============================================================================
// Update Loop
// =============================================================================

void device_update(device_context_t* ctx) {
    uint32_t now = GET_MILLIS();
    
    // State-specific timeouts and updates
    switch (ctx->current_state) {
        case STATE_SCANNING:
            // Update progress display
            render_state(ctx);
            
            // Check timeout
            if (now - ctx->state_enter_time >= SCAN_TIMEOUT) {
                device_set_state(ctx, STATE_SCAN_RESULTS);
            }
            break;
            
        case STATE_WAITING_RESPONSE:
            // Update progress
            render_state(ctx);
            
            // Check timeout
            if (now - ctx->state_enter_time >= CALL_TIMEOUT) {
                strcpy(ctx->message_title, "Timeout");
                strcpy(ctx->message_text, "No response");
                ctx->message_timeout = 2000;
                device_set_state(ctx, STATE_MESSAGE);
            }
            break;
            
        case STATE_MESSAGE:
            // Auto-dismiss after timeout
            if (ctx->message_timeout > 0 &&
                now - ctx->state_enter_time >= ctx->message_timeout) {
                device_go_back(ctx);
            }
            break;
            
        default:
            break;
    }
    
    // Update visibility from hardware switch
    ctx->is_visible = (buttons_get_visibility_mode() == VISIBILITY_VISIBLE);
}

