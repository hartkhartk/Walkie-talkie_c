/**
 * @file buttons.c
 * @brief מימוש HAL עבור כפתורים ומתגים
 */

#include "hal/buttons.h"
#include "config.h"
#include <string.h>

// Platform-specific includes
#ifdef ESP32
    #include "driver/gpio.h"
    #include "driver/adc.h"
    #include "esp_timer.h"
    #define GET_MILLIS() (esp_timer_get_time() / 1000)
#else
    // Simulator or other platform
    #include <time.h>
    static uint32_t sim_millis(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
    }
    #define GET_MILLIS() sim_millis()
#endif

// =============================================================================
// Internal State
// =============================================================================

static button_state_t button_states[BTN_COUNT];
static button_event_t pending_events[BTN_COUNT];
static button_callback_t button_callback = NULL;
static switch_callback_t talk_mode_callback = NULL;
static switch_callback_t visibility_callback = NULL;
static rotary_callback_t volume_callback = NULL;

static talk_mode_t current_talk_mode = TALK_MODE_PTT;
static visibility_mode_t current_visibility = VISIBILITY_VISIBLE;
static uint8_t current_volume = 50;
static uint8_t current_mode_dial = 0;

static int8_t last_digit_input = -1;

// Keypad layout
static const button_id_t keypad_map[4][3] = {
    {BTN_1, BTN_2, BTN_3},
    {BTN_4, BTN_5, BTN_6},
    {BTN_7, BTN_8, BTN_9},
    {BTN_ABOVE_RED, BTN_0, BTN_ABOVE_GREEN}
};

// =============================================================================
// Platform-Specific GPIO Reading
// =============================================================================

#ifdef ESP32
static bool read_gpio(uint8_t pin) {
    return gpio_get_level(pin) == 0; // Active low
}

static void init_gpio_input(uint8_t pin) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

static void init_gpio_output(uint8_t pin) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}
#else
// Simulator stubs - will be replaced by simulator implementation
static bool sim_button_state[BTN_COUNT] = {false};

static bool read_gpio(uint8_t pin) {
    (void)pin;
    return false;
}

void sim_set_button(button_id_t btn, bool pressed) {
    if (btn < BTN_COUNT) {
        sim_button_state[btn] = pressed;
    }
}

static void init_gpio_input(uint8_t pin) { (void)pin; }
static void init_gpio_output(uint8_t pin) { (void)pin; }
#endif

// =============================================================================
// Keypad Scanning
// =============================================================================

static void scan_keypad(void) {
#ifdef ESP32
    // Scan 4x3 matrix keypad
    const uint8_t row_pins[] = {PIN_KEYPAD_ROW_0, PIN_KEYPAD_ROW_1, 
                                 PIN_KEYPAD_ROW_2, PIN_KEYPAD_ROW_3};
    const uint8_t col_pins[] = {PIN_KEYPAD_COL_0, PIN_KEYPAD_COL_1, PIN_KEYPAD_COL_2};
    
    for (int row = 0; row < 4; row++) {
        // Set current row low
        gpio_set_level(row_pins[row], 0);
        
        for (int col = 0; col < 3; col++) {
            button_id_t btn = keypad_map[row][col];
            bool pressed = (gpio_get_level(col_pins[col]) == 0);
            
            // Update button state
            if (pressed && !button_states[btn].is_pressed) {
                button_states[btn].is_pressed = true;
                button_states[btn].press_start_time = GET_MILLIS();
                button_states[btn].long_press_triggered = false;
                pending_events[btn] = BTN_EVENT_PRESS;
                
                // Record digit input
                if (btn >= BTN_0 && btn <= BTN_9) {
                    last_digit_input = btn - BTN_0;
                }
            } else if (!pressed && button_states[btn].is_pressed) {
                button_states[btn].is_pressed = false;
                pending_events[btn] = BTN_EVENT_RELEASE;
            }
        }
        
        // Set row back to high
        gpio_set_level(row_pins[row], 1);
    }
#else
    // Simulator: use sim_button_state array
    for (int i = BTN_0; i <= BTN_9; i++) {
        button_id_t btn = (button_id_t)i;
        bool pressed = sim_button_state[btn];
        
        if (pressed && !button_states[btn].is_pressed) {
            button_states[btn].is_pressed = true;
            button_states[btn].press_start_time = GET_MILLIS();
            button_states[btn].long_press_triggered = false;
            pending_events[btn] = BTN_EVENT_PRESS;
            last_digit_input = btn - BTN_0;
        } else if (!pressed && button_states[btn].is_pressed) {
            button_states[btn].is_pressed = false;
            pending_events[btn] = BTN_EVENT_RELEASE;
        }
    }
#endif
}

// =============================================================================
// Function Button Reading
// =============================================================================

static void scan_function_buttons(void) {
    struct {
        button_id_t btn;
        uint8_t pin;
    } buttons[] = {
        {BTN_GREEN, PIN_BTN_GREEN},
        {BTN_RED, PIN_BTN_RED},
        {BTN_ABOVE_GREEN, PIN_BTN_ABOVE_GREEN},
        {BTN_ABOVE_RED, PIN_BTN_ABOVE_RED},
        {BTN_MULTI, PIN_BTN_MULTI},
        {BTN_RECORD, PIN_BTN_RECORD},
        {BTN_PTT, PIN_BTN_PTT}
    };
    
    for (size_t i = 0; i < sizeof(buttons)/sizeof(buttons[0]); i++) {
        button_id_t btn = buttons[i].btn;
        
#ifdef ESP32
        bool pressed = read_gpio(buttons[i].pin);
#else
        bool pressed = sim_button_state[btn];
#endif
        
        if (pressed && !button_states[btn].is_pressed) {
            // Button just pressed
            button_states[btn].is_pressed = true;
            button_states[btn].press_start_time = GET_MILLIS();
            button_states[btn].long_press_triggered = false;
            pending_events[btn] = BTN_EVENT_PRESS;
            
            if (button_callback) {
                button_callback(btn, BTN_EVENT_PRESS);
            }
        } else if (!pressed && button_states[btn].is_pressed) {
            // Button released
            button_states[btn].is_pressed = false;
            pending_events[btn] = BTN_EVENT_RELEASE;
            
            if (button_callback) {
                button_callback(btn, BTN_EVENT_RELEASE);
            }
        } else if (pressed && button_states[btn].is_pressed) {
            // Button still held - check for long press
            uint32_t held_time = GET_MILLIS() - button_states[btn].press_start_time;
            if (held_time >= LONG_PRESS_DURATION && !button_states[btn].long_press_triggered) {
                button_states[btn].long_press_triggered = true;
                pending_events[btn] = BTN_EVENT_LONG_PRESS;
                
                if (button_callback) {
                    button_callback(btn, BTN_EVENT_LONG_PRESS);
                }
            }
        }
    }
}

// =============================================================================
// Integrated PTT Button + Slide Switch Reading
// =============================================================================
//
// הכפתור המשולב:
// - PIN_PTT_BUTTON: לחיצת הכפתור עצמו
// - PIN_PTT_SLIDE_A + PIN_PTT_SLIDE_B: מתג ההזזה בתוך הכפתור
//
// מצבי המתג:
// A=1, B=0 -> ALWAYS (למעלה)
// A=0, B=0 -> PTT (אמצע) 
// A=0, B=1 -> MUTED (למטה)

static void scan_switches(void) {
#ifdef ESP32
    // Read integrated PTT slide switch (3 positions via 2 pins inside PTT button)
    bool sw_a = read_gpio(PIN_PTT_SLIDE_A);
    bool sw_b = read_gpio(PIN_PTT_SLIDE_B);
    
    talk_mode_t new_talk_mode;
    if (sw_a && !sw_b) {
        new_talk_mode = TALK_MODE_ALWAYS;   // מתג למעלה
    } else if (!sw_a && !sw_b) {
        new_talk_mode = TALK_MODE_PTT;      // מתג אמצע
    } else {
        new_talk_mode = TALK_MODE_MUTED;    // מתג למטה
    }
    
    if (new_talk_mode != current_talk_mode) {
        current_talk_mode = new_talk_mode;
        if (talk_mode_callback) {
            talk_mode_callback();
        }
    }
    
    // Read visibility switch (separate switch)
    visibility_mode_t new_visibility = read_gpio(PIN_SW_VISIBILITY) ? 
                                        VISIBILITY_HIDDEN : VISIBILITY_VISIBLE;
    if (new_visibility != current_visibility) {
        current_visibility = new_visibility;
        if (visibility_callback) {
            visibility_callback();
        }
    }
#endif
    // Simulator handles switches via direct API calls
}

// =============================================================================
// Rotary Encoder Reading
// =============================================================================

static int8_t last_volume_state = 0;

static void scan_rotary(void) {
#ifdef ESP32
    // Volume encoder
    bool a = read_gpio(PIN_VOLUME_A);
    bool b = read_gpio(PIN_VOLUME_B);
    int8_t state = (a ? 1 : 0) | (b ? 2 : 0);
    
    // Gray code state machine
    static const int8_t encoder_table[] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    int8_t delta = encoder_table[(last_volume_state << 2) | state];
    
    if (delta != 0) {
        int new_vol = current_volume + delta * 5;
        if (new_vol < 0) new_vol = 0;
        if (new_vol > 100) new_vol = 100;
        current_volume = new_vol;
        
        if (volume_callback) {
            volume_callback(delta);
        }
    }
    
    last_volume_state = state;
    
    // Mode dial (15 positions via ADC)
    int adc_value = adc1_get_raw(ADC1_CHANNEL_7); // PIN_MODE_DIAL
    current_mode_dial = (adc_value * 15) / 4096;
#endif
}

// =============================================================================
// Public API Implementation
// =============================================================================

void buttons_init(void) {
    memset(button_states, 0, sizeof(button_states));
    memset(pending_events, 0, sizeof(pending_events));
    
#ifdef ESP32
    // Initialize keypad row pins as outputs
    init_gpio_output(PIN_KEYPAD_ROW_0);
    init_gpio_output(PIN_KEYPAD_ROW_1);
    init_gpio_output(PIN_KEYPAD_ROW_2);
    init_gpio_output(PIN_KEYPAD_ROW_3);
    
    // Set all rows high initially
    gpio_set_level(PIN_KEYPAD_ROW_0, 1);
    gpio_set_level(PIN_KEYPAD_ROW_1, 1);
    gpio_set_level(PIN_KEYPAD_ROW_2, 1);
    gpio_set_level(PIN_KEYPAD_ROW_3, 1);
    
    // Initialize keypad column pins as inputs with pullup
    init_gpio_input(PIN_KEYPAD_COL_0);
    init_gpio_input(PIN_KEYPAD_COL_1);
    init_gpio_input(PIN_KEYPAD_COL_2);
    
    // Initialize function button pins
    init_gpio_input(PIN_BTN_GREEN);
    init_gpio_input(PIN_BTN_RED);
    init_gpio_input(PIN_BTN_ABOVE_GREEN);
    init_gpio_input(PIN_BTN_ABOVE_RED);
    init_gpio_input(PIN_BTN_MULTI);
    init_gpio_input(PIN_BTN_RECORD);
    
    // Initialize integrated PTT button + slide switch
    init_gpio_input(PIN_PTT_BUTTON);      // הכפתור עצמו
    init_gpio_input(PIN_PTT_SLIDE_A);     // מתג הזזה bit A
    init_gpio_input(PIN_PTT_SLIDE_B);     // מתג הזזה bit B
    
    // Initialize visibility switch
    init_gpio_input(PIN_SW_VISIBILITY);
    
    // Initialize rotary encoder pins
    init_gpio_input(PIN_VOLUME_A);
    init_gpio_input(PIN_VOLUME_B);
    
    // Initialize mode dial ADC
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11);
#endif
}

void buttons_update(void) {
    scan_keypad();
    scan_function_buttons();
    scan_switches();
    scan_rotary();
}

bool buttons_is_pressed(button_id_t button) {
    if (button >= BTN_COUNT) return false;
    return button_states[button].is_pressed;
}

button_event_t buttons_get_event(button_id_t button) {
    if (button >= BTN_COUNT) return BTN_EVENT_NONE;
    button_event_t event = pending_events[button];
    pending_events[button] = BTN_EVENT_NONE;
    return event;
}

void buttons_set_callback(button_callback_t callback) {
    button_callback = callback;
}

talk_mode_t buttons_get_talk_mode(void) {
    return current_talk_mode;
}

bool buttons_is_transmitting(void) {
    // בודק את מצב הכפתור המשולב (מתג + לחיצה)
    switch (current_talk_mode) {
        case TALK_MODE_ALWAYS:
            // מתג למעלה - תמיד משדר
            return true;
            
        case TALK_MODE_PTT:
            // מתג באמצע - משדר רק כשלוחצים על הכפתור
            return buttons_is_pressed(BTN_PTT);
            
        case TALK_MODE_MUTED:
            // מתג למטה - לעולם לא משדר
            return false;
            
        default:
            return false;
    }
}

visibility_mode_t buttons_get_visibility_mode(void) {
    return current_visibility;
}

void buttons_set_talk_mode_callback(switch_callback_t callback) {
    talk_mode_callback = callback;
}

void buttons_set_visibility_callback(switch_callback_t callback) {
    visibility_callback = callback;
}

rotary_state_t buttons_get_volume(void) {
    rotary_state_t state = {0, current_volume};
    return state;
}

uint8_t buttons_get_mode_dial(void) {
    return current_mode_dial;
}

void buttons_set_volume_callback(rotary_callback_t callback) {
    volume_callback = callback;
}

int8_t buttons_get_digit_input(void) {
    int8_t digit = last_digit_input;
    last_digit_input = -1;
    return digit;
}

void buttons_clear_events(void) {
    memset(pending_events, 0, sizeof(pending_events));
    last_digit_input = -1;
}

// =============================================================================
// Simulator-Specific Functions
// =============================================================================

#ifndef ESP32
void sim_set_talk_mode(talk_mode_t mode) {
    if (mode != current_talk_mode) {
        current_talk_mode = mode;
        if (talk_mode_callback) {
            talk_mode_callback();
        }
    }
}

void sim_set_visibility(visibility_mode_t mode) {
    if (mode != current_visibility) {
        current_visibility = mode;
        if (visibility_callback) {
            visibility_callback();
        }
    }
}

void sim_set_volume(uint8_t vol) {
    if (vol != current_volume) {
        int8_t delta = (vol > current_volume) ? 1 : -1;
        current_volume = vol;
        if (volume_callback) {
            volume_callback(delta);
        }
    }
}

void sim_set_mode_dial(uint8_t mode) {
    if (mode < 15) {
        current_mode_dial = mode;
    }
}
#endif

