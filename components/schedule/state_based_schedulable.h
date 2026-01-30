#pragma once

#include "schedule.h"

namespace esphome {
namespace schedule {

// Forward declaration
class StateModeSelect;

// State-based schedule states (complete state machine for Switch platform)
enum StateBasedScheduleState {
  // Error/Invalid states
  STATE_TIME_INVALID = 0,      // RTC time not synchronized
  STATE_SCHEDULE_INVALID = 1,  // Schedule data is invalid or not available
  STATE_SCHEDULE_EMPTY = 2,    // Schedule is valid but has no events
  
  // Initialization state
  STATE_INIT = 3,              // Initializing schedule operation
  
  // Manual override states
  STATE_MAN_OFF = 4,           // Manual override: forced off
  STATE_MAN_ON = 5,            // Manual override: forced on
  
  // Temporary mode states
  STATE_EARLY_OFF = 6,         // Early-off mode active (temporary override)
  STATE_BOOST_ON = 7,          // Boost mode active (temporary override)
  
  // Auto mode states
  STATE_AUTO_ON = 8,           // Auto mode: schedule indicates ON
  STATE_AUTO_OFF = 9           // Auto mode: schedule indicates OFF
};

// Schedule mode enum for state-based components
enum ScheduleMode {
  SCHEDULE_MODE_MANUAL_OFF = 0,
  SCHEDULE_MODE_EARLY_OFF = 1,
  SCHEDULE_MODE_AUTO = 2,
  SCHEDULE_MODE_MANUAL_ON = 3,
  SCHEDULE_MODE_BOOST_ON = 4
};

/**
 * StateBasedSchedulable - For components that maintain continuous state
 * 
 * This is essentially an alias/marker for the default Schedule behavior.
 * The Schedule base class already implements state-based storage by default.
 * 
 * Storage format: [ON_TIME, OFF_TIME] pairs
 * - Each entry uses 2 x uint16_t = 4 bytes
 * - ON_TIME:  bit 14 = 1, bits 0-13 = time in minutes
 * - OFF_TIME: bit 14 = 0, bits 0-13 = time in minutes
 * 
 * Examples: Switch, Climate, Light, Fan
 * 
 * Usage in YAML:
 *   switch:
 *     - platform: schedule
 *       max_schedule_entries: 50  # Needs 204 bytes (50 * 2 * 2 + 4)
 */
class StateBasedSchedulable : public Schedule {
 public:
  StateBasedSchedulable() = default;
  
  /** Returns STORAGE_TYPE_STATE_BASED (default) */
  ScheduleStorageType get_storage_type() const override {
    return STORAGE_TYPE_STATE_BASED;
  }
  
  /** State-based components use the full state machine with modes */
  void loop() override;
  
  //============================================================================
  // MODE MANAGEMENT (state-based only)
  //============================================================================
  void set_mode_select(StateModeSelect *mode_select);
  void set_mode_option(ScheduleMode mode);
  void on_mode_changed(const std::string &mode);
  
  // Logging - state-based format (ON/OFF pairs)
  void log_schedule_data() override;
   
 protected:
  //============================================================================
  // VIRTUAL METHOD OVERRIDES FROM BASE CLASS
  //============================================================================
  void advance_to_next_event_() override;
  void check_and_advance_events_() override;
  void initialize_schedule_operation_() override;
  
  //============================================================================
  // STATE MACHINE METHODS (state-based only)
  //============================================================================
  void handle_state_change_();  // Process state transitions and update outputs
  
  //============================================================================
  // STATE MACHINE HELPER METHODS (state-based only)
  //============================================================================
  void initialize_sensor_last_on_values_(int16_t current_event_index);
  std::string create_event_string_(uint16_t event_raw);
  int mode_to_state_(ScheduleMode mode, bool event_on);
  bool should_reset_to_auto_(int state, bool event_on);
  int get_state_after_mode_reset_(bool event_on);
  
  /** Force reinitialization after schedule update */
  void force_reinitialize() {
    ESP_LOGD("schedule.state_based", "Forcing reinitialization");
    this->current_state_ = STATE_INIT;
    this->processed_state_ = STATE_INIT;
  }
  
  /** Update mode select options when schedule empty state changes */
  void on_schedule_empty_changed(bool is_empty) override;
  
  //============================================================================
  // STATE-SPECIFIC MEMBER VARIABLES
  //============================================================================
  StateModeSelect *mode_select_{nullptr};
  ScheduleMode current_mode_{SCHEDULE_MODE_MANUAL_OFF};
  bool event_switch_state_{false};  // Current ON/OFF state from schedule
  
  // State machine
  int current_state_{STATE_INIT};
  int processed_state_{STATE_INIT};
};

} // namespace schedule
} // namespace esphome
