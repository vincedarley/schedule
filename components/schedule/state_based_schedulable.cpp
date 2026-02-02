#include "state_based_schedulable.h"
#include "state_mode_select.h"

namespace esphome {
namespace schedule {

static const char *const TAG = "schedule.state_based";
// Force rebuild after header change

//==============================================================================
// MAIN LOOP
//==============================================================================

void StateBasedSchedulable::loop() {
  uint32_t now = millis();
  
  // On first loop, sync the restored mode from mode select without triggering state changes
  bool mode_synced = false;
  if (this->first_loop_ && this->mode_select_ != nullptr && this->mode_select_->has_state()) {
    auto restored_mode_ref = this->mode_select_->current_option();
    std::string restored_mode = std::string(restored_mode_ref.c_str());
    if (!restored_mode.empty()) {
      // Update mode enum without calling on_mode_changed
      if (restored_mode == "Manual Off") {
        this->current_mode_ = SCHEDULE_MODE_MANUAL_OFF;
      } else if (restored_mode == "Early Off") {
        this->current_mode_ = SCHEDULE_MODE_EARLY_OFF;
      } else if (restored_mode == "Auto") {
        this->current_mode_ = SCHEDULE_MODE_AUTO;
      } else if (restored_mode == "Manual On") {
        this->current_mode_ = SCHEDULE_MODE_MANUAL_ON;
      } else if (restored_mode == "Boost On") {
        this->current_mode_ = SCHEDULE_MODE_BOOST_ON;
      }
      mode_synced = true;
    }
  }
  
  // Auto-request check and main loop throttle - runs once per second
  if (now - this->last_time_check_ >= 1000) {
    this->last_time_check_ = now;
    
    // Check prerequisites to handle pending auto-request execution
    // This MUST run even in manual modes to download schedules on boot
    this->check_prerequisites_();
    
    // Update HA connection status first if not connected
    if (!this->ha_connected_) {
      this->check_ha_connection_();
    }
  }
  
  // Manual modes operate independently of schedule state
  // Check BEFORE the 1-second throttle so it runs immediately on first loop
  if (this->current_mode_ == SCHEDULE_MODE_MANUAL_OFF || 
      this->current_mode_ == SCHEDULE_MODE_MANUAL_ON) {
    // Just ensure state matches mode
    int expected_state = (this->current_mode_ == SCHEDULE_MODE_MANUAL_ON) ? STATE_MAN_ON : STATE_MAN_OFF;
    // Force handle_state_change() on first boot, when processed_state_ is still -1, or when state needs to change
    if (this->current_state_ != expected_state || this->processed_state_ == -1 || this->first_loop_) {
      this->current_state_ = expected_state;
      // On first loop, force processed_state_ to differ so handle_state_change() doesn't skip the update
      if (this->first_loop_) {
        this->processed_state_ = -1;
      }
      this->handle_state_change_();
      this->first_loop_ = false;  // Clear flag AFTER updating
    }
    return;  // Skip all other checks for manual modes
  }
  
  // For non-manual modes, clear first_loop_ flag (but only if we've synced the mode or don't have a mode select)
  if (this->first_loop_ && (mode_synced || this->mode_select_ == nullptr)) {
    this->first_loop_ = false;
  }
  
  // Handle initialization state IMMEDIATELY (not throttled)
  // This ensures that when mode changes to Auto, initialization happens right away
  if (this->current_state_ == STATE_INIT) {
    this->initialize_schedule_operation_();
    this->initialized_since_boot_ = true;  // Mark as initialized
    // Force state change to be processed in next iteration
    this->handle_state_change_();
    return;  // Exit to allow next iteration to handle normal operation
  }
  
  // Periodic logging every 60 seconds
  if (now - this->last_state_log_time_ >= 60000) {
    this->last_state_log_time_ = now;
    this->log_state_flags_();
    ESP_LOGV(TAG, "Schedule loop state: %d", this->current_state_);
    ESP_LOGV(TAG, "Current mode: %d", this->current_mode_);
  }

  // Continue normal operation - check prerequisites and handle state
  // Only process if throttle check passed (last_time_check was updated this iteration)
  if (now == this->last_time_check_) {
    // Check all prerequisites (returns error code)
    auto prereq_error = this->check_prerequisites_();
    
    // Handle prerequisite errors with state transitions
    if (prereq_error != PREREQ_OK) {
      ESP_LOGD(TAG, "[LOOP] Prerequisites failed, error=%d", prereq_error);
      // Map prerequisite errors to states
      if (prereq_error == PREREQ_TIME_INVALID) {
        if (this->current_state_ != STATE_TIME_INVALID) {
          ESP_LOGW(TAG, "Time is not valid, schedule operations paused");
          this->current_state_ = STATE_TIME_INVALID;
        }
      } else if (prereq_error == PREREQ_SCHEDULE_INVALID) {
        if (this->current_state_ != STATE_SCHEDULE_INVALID) {
          ESP_LOGW(TAG, "Schedule is not valid and Home Assistant not connected");
          this->current_state_ = STATE_SCHEDULE_INVALID;
        }
      } else if (prereq_error == PREREQ_SCHEDULE_EMPTY) {
        if (this->current_state_ != STATE_SCHEDULE_EMPTY) {
          ESP_LOGI(TAG, "Schedule is empty, no events to process");
          this->current_state_ = STATE_SCHEDULE_EMPTY;
        }
      }
      // Handle state change for error states
      this->handle_state_change_();
      return;
    }
    
    // Force initialization on first boot when in AUTO state
    if ((this->current_state_ == STATE_AUTO_ON || this->current_state_ == STATE_AUTO_OFF) && 
        !this->initialized_since_boot_) {
      this->current_state_ = STATE_INIT;
      // Initialization will be handled in next loop iteration (outside throttle)
      return;
    }
    
    // Prerequisites OK - transition from error states to INIT if needed
    if ((this->current_state_ == STATE_TIME_INVALID || 
         this->current_state_ == STATE_SCHEDULE_INVALID || 
         this->current_state_ == STATE_SCHEDULE_EMPTY) && 
        prereq_error == PREREQ_OK) {
      this->current_state_ = STATE_INIT;
      ESP_LOGV(TAG, "Prerequisites met, transitioning to INIT state");
      // Initialization will be handled in next loop iteration (outside throttle)
      return;
    }
    
    // Skip if still in error states
    if (this->current_state_ == STATE_TIME_INVALID ||
        this->current_state_ == STATE_SCHEDULE_INVALID ||
        this->current_state_ == STATE_SCHEDULE_EMPTY) {
      return;
    }
    
    // Check and advance schedule events if time has reached next event
    // This will update event_switch_state_ and current_state_ as needed
    this->check_and_advance_events_();
  }
}

//==============================================================================
// MODE MANAGEMENT
//==============================================================================

void StateBasedSchedulable::set_mode_select(StateModeSelect *mode_select) {
  this->mode_select_ = mode_select;
  if (mode_select != nullptr) {
    mode_select->set_on_value_callback([this](const std::string &value) {
      this->on_mode_changed(value);
    });
  }
}

void StateBasedSchedulable::on_mode_changed(const std::string &mode) {
  ESP_LOGI(TAG, "Schedule mode changed to: %s", mode.c_str());
  
  // Update the current mode enum based on the selected mode string
  if (mode == "Manual Off") {
    this->current_mode_ = SCHEDULE_MODE_MANUAL_OFF;
  } else if (mode == "Early Off") {
    this->current_mode_ = SCHEDULE_MODE_EARLY_OFF;
  } else if (mode == "Auto") {
    this->current_mode_ = SCHEDULE_MODE_AUTO;
  } else if (mode == "Manual On") {
    this->current_mode_ = SCHEDULE_MODE_MANUAL_ON;
  } else if (mode == "Boost On") {
    this->current_mode_ = SCHEDULE_MODE_BOOST_ON;
  } else {
    ESP_LOGW(TAG, "Unknown mode: %s, defaulting to Manual Off", mode.c_str());
    this->current_mode_ = SCHEDULE_MODE_MANUAL_OFF;
  }
  
  ESP_LOGD(TAG, "Current mode enum set to: %d", this->current_mode_);
  
  // Don't set state during boot - let initialization handle it
  if (!this->initialized_since_boot_ && this->current_mode_ == SCHEDULE_MODE_AUTO) {
    ESP_LOGI(TAG, "Mode changed to Auto - triggering initialization (schedule valid=%d, empty=%d)", 
             this->schedule_valid_, this->schedule_empty_);
    this->current_state_ = STATE_INIT;
    return;
  }
  
  // Immediately update the state based on the new mode and trigger state change
  this->current_state_ = this->mode_to_state_(this->current_mode_, this->event_switch_state_);
  this->handle_state_change_();
}

void StateBasedSchedulable::on_schedule_empty_changed(bool is_empty, bool schedule_pending) {
  if (this->mode_select_ != nullptr) {
    // Update schedule empty state first so update_available_modes has correct info
    this->mode_select_->set_schedule_empty(is_empty);
    this->mode_select_->update_available_modes(!is_empty, schedule_pending);
    // Log status but don't restrict modes - mode select always advertises all options
    if (is_empty && schedule_pending) {
      ESP_LOGI(TAG, "Schedule empty but download pending");
    } else if (is_empty) {
      ESP_LOGI(TAG, "Schedule empty - schedule-dependent modes will behave as manual modes");
    } else {
      ESP_LOGI(TAG, "Schedule populated");
    }
  }
}

// Set the mode select option programmatically using enum
void StateBasedSchedulable::set_mode_option(ScheduleMode mode) {
  if (this->mode_select_ != nullptr) {
    std::string option;
    switch (mode) {
      case SCHEDULE_MODE_MANUAL_OFF:
        option = "Manual Off";
        break;
      case SCHEDULE_MODE_EARLY_OFF:
        option = "Early Off";
        break;
      case SCHEDULE_MODE_AUTO:
        option = "Auto";
        break;
      case SCHEDULE_MODE_MANUAL_ON:
        option = "Manual On";
        break;
      case SCHEDULE_MODE_BOOST_ON:
        option = "Boost On";
        break;
      default:
        option = "Manual Off";
        ESP_LOGW(TAG, "Unknown mode enum: %d, defaulting to Manual Off", mode);
        break;
    }
    this->mode_select_->publish_state(option);
    this->current_mode_ = mode;
    ESP_LOGD(TAG, "Mode set to: %s (enum: %d)", option.c_str(), mode);
  }
}

//==============================================================================
// STATE MACHINE HELPER METHODS
//==============================================================================

int StateBasedSchedulable::mode_to_state_(ScheduleMode mode, bool event_on) {
  switch(mode) {
    case SCHEDULE_MODE_MANUAL_OFF:
      return STATE_MAN_OFF;
    case SCHEDULE_MODE_MANUAL_ON:
      return STATE_MAN_ON;
    case SCHEDULE_MODE_EARLY_OFF:
      return STATE_EARLY_OFF;
    case SCHEDULE_MODE_BOOST_ON:
      return STATE_BOOST_ON;
    case SCHEDULE_MODE_AUTO:
      return event_on ? STATE_AUTO_ON : STATE_AUTO_OFF;
    default:
      ESP_LOGW(TAG, "Unknown schedule mode: %d", mode);
      return STATE_SCHEDULE_INVALID;
  }
}

// Check if temporary mode (early-off or boost) should reset to auto
bool StateBasedSchedulable::should_reset_to_auto_(int state, bool event_on) {
  // Early-off mode resets to auto on any schedule event (either ON or OFF)
  if (state == STATE_EARLY_OFF) {
    return true;
  }
  // Boost mode resets to auto on any schedule event (either ON or OFF)
  if (state == STATE_BOOST_ON) {
    return true;
  }
  return false;
}

// Get the state after resetting from temporary mode to auto
int StateBasedSchedulable::get_state_after_mode_reset_(bool event_on) {
  return event_on ? STATE_AUTO_ON : STATE_AUTO_OFF;
}

//==============================================================================
// OVERRIDDEN BASE CLASS METHODS
//==============================================================================

void StateBasedSchedulable::advance_to_next_event_() {
  // Call base class implementation to handle common event advancement
  Schedule::advance_to_next_event_();
  
  // Update event switch state based on current event
  this->event_switch_state_ = (this->current_event_raw_ & SWITCH_STATE_BIT) != 0;
}

void StateBasedSchedulable::check_and_advance_events_() {
  // Don't process before initialization completes
  if (this->current_event_index_ < 0) {
    return;
  }
  
  // Get current time in minutes
  auto now_time = this->time_->now();
  uint16_t current_time_minutes = this->time_to_minutes_(now_time);
  
  // Only process once per minute to prevent cycling
  if (current_time_minutes == this->last_processed_minute_) {
    return;  // Already processed this minute
  }
  this->last_processed_minute_ = current_time_minutes;
  
  // Check if we should advance to next event
  bool should_advance = this->should_advance_to_next_event_(current_time_minutes);
  
  // Log near event time
  uint16_t next_event_time = this->next_event_raw_ & TIME_MASK;
  if (current_time_minutes >= next_event_time - 2 && current_time_minutes <= next_event_time + 5) {
    ESP_LOGD(TAG, "[%s] At/near event: cur=%u, next=%u, should_advance=%s", 
             this->get_name().c_str(), current_time_minutes, next_event_time, should_advance ? "YES" : "NO");
  }
  
  if (!should_advance) {
    return;
  }
  
  ESP_LOGI(TAG, "[%s] ADVANCING EVENT at time %u", this->get_name().c_str(), current_time_minutes);
  
  // Advance to next event (updates event_switch_state_)
  this->advance_to_next_event_();
  
  // Handle mode transitions for temporary modes
  if (this->should_reset_to_auto_(this->current_state_, this->event_switch_state_)) {
    // Reset from temporary mode (early-off or boost) back to auto
    this->current_mode_ = SCHEDULE_MODE_AUTO;
    this->current_state_ = this->get_state_after_mode_reset_(this->event_switch_state_);
    this->set_mode_option(this->current_mode_);
    this->handle_state_change_();
    ESP_LOGD(TAG, "Temporary mode expired, reset to AUTO mode, state=%d", this->current_state_);
  }
  // Update state for auto mode based on new event
  else if (this->current_mode_ == SCHEDULE_MODE_AUTO) {
    this->current_state_ = this->event_switch_state_ ? STATE_AUTO_ON : STATE_AUTO_OFF;
    this->handle_state_change_();
  }
}

void StateBasedSchedulable::initialize_schedule_operation_() {
  // Call base class implementation for common initialization
  Schedule::initialize_schedule_operation_();
  
  // Determine if current event is an "on" or "off" event
  bool in_event = (this->current_event_raw_ & SWITCH_STATE_BIT) != 0;
  this->event_switch_state_ = in_event;
  
  // Initialize last_on_value_ for each data sensor by searching backwards for the most recent ON event
  this->initialize_sensor_last_on_values_(this->current_event_index_);
  
  // Set initial state based on current event and mode
  this->current_state_ = this->mode_to_state_(this->current_mode_, this->event_switch_state_);
  
  // Reset processed_state_ to force state change detection on next loop iteration
  this->processed_state_ = STATE_TIME_INVALID;
}

// Initialize last_on_value_ for each data sensor by finding the most recent ON event
void StateBasedSchedulable::initialize_sensor_last_on_values_(int16_t current_event_index) {
    ESP_LOGV(TAG, "Initializing sensor last_on_value_ from schedule history");
    
    // Search backwards from current event to find the most recent ON event
    // Start from current event and work backwards
    int16_t search_index = current_event_index;
    
    // If current event is an ON event, use it
    if ((this->schedule_times_in_minutes_[search_index] & SWITCH_STATE_BIT) != 0) {
        ESP_LOGV(TAG, "Current event is ON, using it for last_on_value_ initialization");
        uint16_t data_index = search_index / 2;
        for (auto *sensor : this->data_sensors_) {
            float value = sensor->get_sensor_value(data_index);
            sensor->set_last_on_value(value);
            ESP_LOGV(TAG, "Sensor '%s' last_on_value_ initialized to %.2f from current ON event at index %d",
                     sensor->get_label().c_str(), value, search_index);
        }
        return;
    }
    
    // Current event is OFF, search backwards for previous ON event
    search_index--;
    
    // Search backwards through this week's schedule
    while (search_index >= 0) {
        uint16_t event_raw = this->schedule_times_in_minutes_[search_index];
        
        // Check if this is an ON event
        if ((event_raw & SWITCH_STATE_BIT) != 0) {
            ESP_LOGV(TAG, "Found previous ON event at index %d", search_index);
            uint16_t data_index = search_index / 2;
            for (auto *sensor : this->data_sensors_) {
                float value = sensor->get_sensor_value(data_index);
                sensor->set_last_on_value(value);
                ESP_LOGV(TAG, "Sensor '%s' last_on_value_ initialized to %.2f from ON event at index %d",
                         sensor->get_label().c_str(), value, search_index);
            }
            return;
        }
        search_index--;
    }
    
    // No ON event found in current week, search from end of schedule backwards (previous week rollback)
    ESP_LOGV(TAG, "No ON event found in current week, searching from end of schedule");
    
    // Find the last valid entry in the schedule
    int16_t last_index = -1;
    for (size_t i = 0; i < this->schedule_times_in_minutes_.size(); i++) {
        if (this->schedule_times_in_minutes_[i] == 0xFFFF) {
            last_index = i - 1;
            break;
        }
    }
    
    if (last_index < 0) {
        ESP_LOGW(TAG, "Could not find end of schedule, cannot initialize last_on_value_");
        return;
    }
    
    // Search backwards from end of schedule
    search_index = last_index;
    while (search_index >= 0) {
        uint16_t event_raw = this->schedule_times_in_minutes_[search_index];
        
        // Check if this is an ON event
        if ((event_raw & SWITCH_STATE_BIT) != 0) {
            ESP_LOGV(TAG, "Found previous week's ON event at index %d", search_index);
            uint16_t data_index = search_index / 2;
            for (auto *sensor : this->data_sensors_) {
                float value = sensor->get_sensor_value(data_index);
                sensor->set_last_on_value(value);
                ESP_LOGV(TAG, "Sensor '%s' last_on_value_ initialized to %.2f from previous week ON event at index %d",
                         sensor->get_label().c_str(), value, search_index);
            }
            return;
        }
        search_index--;
    }
    
    // No ON event found in entire schedule
    ESP_LOGW(TAG, "No ON event found in entire schedule, last_on_value_ remains NaN");
}

// Create event string for state-based (ON/OFF format)
std::string StateBasedSchedulable::create_event_string_(uint16_t event_raw) {
	uint16_t event_time = event_raw & TIME_MASK;
	bool event_state = (event_raw & SWITCH_STATE_BIT) != 0;
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%s at %s", event_state ? "ON" : "OFF", this->format_event_time_(event_time).c_str());
	return std::string(buffer);
}

//==============================================================================
// STATE MACHINE METHODS
//==============================================================================

// Handle state transitions and perform associated actions
void StateBasedSchedulable::handle_state_change_() {
  // Only process if state actually changed
  if (this->current_state_ == this->processed_state_) {
    return;
  }
  
  this->processed_state_ = this->current_state_;
  ESP_LOGV(TAG, "Schedule state changed to: %d", this->current_state_);
  
  // Perform actions based on new state
  switch(this->current_state_) {
    case STATE_TIME_INVALID:
    case STATE_SCHEDULE_INVALID:
      // Invalid states - turn off and display error
      this->apply_scheduled_state(false);
      this->update_switch_indicator(false);
      this->display_current_next_events_(
        this->current_state_ == STATE_TIME_INVALID ? "Time Invalid" : "Schedule Invalid",
        this->current_state_ == STATE_TIME_INVALID ? "Time Invalid" : "Schedule Invalid"
      );
      break;
      
    case STATE_SCHEDULE_EMPTY:
      // Schedule valid but empty - turn off and inform
      this->apply_scheduled_state(false);
      this->update_switch_indicator(false);
      this->display_current_next_events_("Schedule Empty", "Schedule Empty");
      break;
      
    case STATE_INIT:
      // Initialization state - turn off and display message
      this->apply_scheduled_state(false);
      this->update_switch_indicator(false);
      this->display_current_next_events_("Initializing", "Initializing");
      break;
      
    case STATE_MAN_OFF:
      // Manual off mode - force off
      this->apply_scheduled_state(false);
      this->update_switch_indicator(false);
      this->display_current_next_events_("Manual Off", "");
      this->set_data_sensors_(this->current_event_index_, false, true);
      break;
      
    case STATE_MAN_ON:
      // Manual on mode - force on
      this->apply_scheduled_state(true);
      this->update_switch_indicator(true);
      this->display_current_next_events_("Manual On", "");
      this->set_data_sensors_(this->current_event_index_, true, true);
      break;
      
    case STATE_EARLY_OFF:
      // Early-off mode - turn off until next schedule event
      this->apply_scheduled_state(false);
      this->update_switch_indicator(false);
      this->display_current_next_events_("Early Off", this->create_event_string_(this->next_event_raw_));
      this->set_data_sensors_(this->current_event_index_, false, false);
      break;
      
    case STATE_BOOST_ON:
      // Boost mode - turn on until next schedule event
      this->apply_scheduled_state(true);
      this->update_switch_indicator(true);
      this->display_current_next_events_("Boost On", this->create_event_string_(this->next_event_raw_));
      this->set_data_sensors_(this->current_event_index_, true, false);
      break;
      
    case STATE_AUTO_ON:
      // Auto mode - schedule indicates ON
      this->set_data_sensors_(this->current_event_index_, true, false);
      this->apply_scheduled_state(true);
      this->update_switch_indicator(true);
      // Check if schedule is empty (waiting for download)
      if (this->schedule_empty_) {
        this->display_current_next_events_("Waiting for schedule", "Download pending");
      } else {
        this->display_current_next_events_(
          this->create_event_string_(this->current_event_raw_), 
          this->create_event_string_(this->next_event_raw_)
        );
      }
      break;
      
    case STATE_AUTO_OFF:
      // Auto mode - schedule indicates OFF
      this->set_data_sensors_(this->current_event_index_, false, false);
      this->apply_scheduled_state(false);
      this->update_switch_indicator(false);
      // Check if schedule is empty (waiting for download)
      if (this->schedule_empty_) {
        this->display_current_next_events_("Waiting for schedule", "Download pending");
      } else {
        this->display_current_next_events_(
          this->create_event_string_(this->current_event_raw_), 
          this->create_event_string_(this->next_event_raw_)
        );
      }
      break;
      
    default:
      ESP_LOGW(TAG, "Unknown schedule state in handle_state_change_: %d", this->current_state_);
      this->current_state_ = STATE_SCHEDULE_INVALID;
      break;
  }
}

//==============================================================================
// LOGGING
//==============================================================================

void StateBasedSchedulable::log_schedule_data() {
  // Use base class implementation - it's already in state-based format (ON/OFF pairs)
  Schedule::log_schedule_data();
}

} // namespace schedule
} // namespace esphome
