#include "event_based_schedulable.h"
#include "event_mode_select.h"
#include "esphome/core/log.h"

namespace esphome {
namespace schedule {

static const char *const TAG = "schedule.event_based";

//==============================================================================
// SETUP AND CONFIGURATION
//==============================================================================

void EventBasedSchedulable::set_mode_select(EventModeSelect *mode_select) {
  this->mode_select_ = mode_select;
}

void EventBasedSchedulable::on_mode_changed(const std::string &mode) {
  ESP_LOGI(TAG, "Schedule mode changed to: %s", mode.c_str());
  
  if (mode == "Disabled") {
    this->current_state_ = STATE_EVENT_DISABLED;
    ESP_LOGI(TAG, "Schedule disabled by mode select");
    this->display_current_next_events_("N/A - Disabled", "N/A - Disabled");
  } else if (mode == "Enabled") {
    // Re-initialize when enabled
    this->current_state_ = STATE_EVENT_INIT;
    ESP_LOGI(TAG, "Schedule enabled by mode select");
  }
}

//==============================================================================
// MAIN LOOP
//==============================================================================

void EventBasedSchedulable::loop() {
  uint32_t now = millis();
  
  // Periodic logging every 60 seconds
  if (now - this->last_state_log_time_ >= 60000) {
    this->last_state_log_time_ = now;
    this->log_state_flags_();
    ESP_LOGV(TAG, "Event-based loop state: %d", this->current_state_);
  }
  
  // Run every second
  if (now - this->last_time_check_ >= 1000) {
    this->last_time_check_ = now;
    
    // Check prerequisites FIRST (includes HA connection and auto-schedule download)
    // MUST run even when disabled to allow first-boot schedule download
    if (!this->handle_prerequisites_()) {
      return;  // Prerequisites failed, error messages already displayed
    }
    
    // Handle disabled state - skip schedule checks if disabled
    if (this->current_state_ == STATE_EVENT_DISABLED) {
      this->display_current_next_events_("N/A - Disabled", "N/A - Disabled");
      return;
    }
    
    // After prerequisites check, if user set mode to disabled, respect it
    if (this->is_mode_disabled_()) {
      if (this->current_state_ != STATE_EVENT_DISABLED) {
        this->current_state_ = STATE_EVENT_DISABLED;
        ESP_LOGI(TAG, "Schedule disabled by mode select");
      }
      this->display_current_next_events_("N/A - Disabled", "N/A - Disabled");
      return;
    }
    
    // Handle initialization state
    if (this->handle_init_state_()) {
      return;  // Initialization complete or deferred
    }
    
    // Handle ready state - process events
    this->handle_ready_state_();
  }
}

//==============================================================================
// VIRTUAL METHOD OVERRIDES FROM BASE CLASS
//==============================================================================

void EventBasedSchedulable::on_schedule_empty_changed(bool is_empty, bool schedule_pending) {
  if (this->mode_select_ != nullptr) {
    // Update schedule empty state first so update_available_modes has correct info
    this->mode_select_->set_schedule_empty(is_empty);
    this->mode_select_->update_available_modes(!is_empty, schedule_pending);
    if (is_empty && schedule_pending) {
      ESP_LOGI(TAG, "Schedule empty but download pending - Enabled mode available for initialization");
    } else if (is_empty) {
      ESP_LOGI(TAG, "Schedule empty - restricting to disabled mode only");
    } else {
      ESP_LOGI(TAG, "Schedule populated - both modes available");
    }
  }
}

void EventBasedSchedulable::advance_to_next_event_() {
  // Call base class implementation to handle common event advancement
  Schedule::advance_to_next_event_();
  
  // Event-based has no additional state to track (no ON/OFF)
  // Just trigger the event when it occurs
}

void EventBasedSchedulable::check_and_advance_events_() {
  // Don't process before initialization completes
  if (this->current_event_index_ < 0) {
    return;
  }
  
  auto now_time = this->time_->now();
  uint16_t current_time_minutes = this->time_to_minutes_(now_time);
  
  // Only process once per minute to prevent cycling
  if (current_time_minutes == this->last_processed_minute_) {
    return;  // Already processed this minute
  }
  this->last_processed_minute_ = current_time_minutes;
  
  // Check if it's time to advance to the next event
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
  
  // Trigger the current event before advancing
  ESP_LOGI(TAG, "[%s] EVENT TRIGGERED at index %d, time=%u", 
           this->get_name().c_str(), this->current_event_index_, current_time_minutes);
  this->apply_scheduled_state(true);
  
  // Advance to next event
  this->advance_to_next_event_();
}

void EventBasedSchedulable::initialize_schedule_operation_() {
  // Call base class implementation to find current/next events
  Schedule::initialize_schedule_operation_();
  
  // Event-based doesn't need to initialize ON/OFF state or search for last ON values
  // Just ready to trigger events as they occur
  
  ESP_LOGD(TAG, "Event-based initialization complete, state: %d", this->current_state_);
}

//==============================================================================
// EVENT-BASED HELPER METHODS
//==============================================================================

void EventBasedSchedulable::update_event_based_ui_() {
  // Display current and next event times
  std::string current_text = "EVENT at " + this->format_event_time_(this->current_event_raw_ & TIME_MASK);
  std::string next_text = "EVENT at " + this->format_event_time_(this->next_event_raw_ & TIME_MASK);
  
  this->display_current_next_events_(current_text, next_text);
  
  // Update data sensors with current event index
  this->set_data_sensors_(this->current_event_index_, true, false);
}

//==============================================================================
// LOGGING
//==============================================================================

void EventBasedSchedulable::log_schedule_data() {
  ESP_LOGI(TAG, "Event-Based Schedule Data:");
  ESP_LOGI(TAG, "Max Entries: %u", this->schedule_max_entries_);

  uint16_t *schedule_data = this->schedule_times_in_minutes_.data();
  size_t index = 0;
  uint16_t entry_count = 0;

  // Process entries until terminator (0xFFFF) or end of data
  while (index < this->schedule_times_in_minutes_.size() && schedule_data[index] != 0xFFFF) {
    uint16_t event_time = schedule_data[index];
    
    // Extract event time (mask off state bit)
    uint16_t time_minutes = event_time & TIME_MASK;
    uint16_t day = time_minutes / 1440;
    uint16_t hour = (time_minutes % 1440) / 60;
    uint16_t minute = time_minutes % 60;

    const char *day_names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    ESP_LOGI(TAG, "  Entry %u: EVENT at %s:%02u:%02u (raw: 0x%04X)", 
             entry_count, day_names[day], hour, minute, event_time);

    index++;
    entry_count++;
  }

  ESP_LOGI(TAG, "Total Entries: %u", entry_count);
}

//==============================================================================
// HELPER METHODS
//==============================================================================

bool EventBasedSchedulable::is_mode_disabled_() const {
  return this->mode_select_ != nullptr && this->mode_select_->current_option() == "Disabled";
}

bool EventBasedSchedulable::handle_disabled_state_() {
  if (this->current_state_ == STATE_EVENT_DISABLED) {
    this->display_current_next_events_("N/A - Disabled", "N/A - Disabled");
    return true;
  }
  return false;
}

bool EventBasedSchedulable::handle_init_state_() {
  if (this->current_state_ != STATE_EVENT_INIT) {
    return false;
  }
  
  // If mode is disabled, transition to disabled state instead of initializing
  if (this->is_mode_disabled_()) {
    this->current_state_ = STATE_EVENT_DISABLED;
    this->display_current_next_events_("N/A - Disabled", "N/A - Disabled");
    return true;
  }
  
  // Initialize schedule and transition to ready state
  this->initialize_schedule_operation_();
  this->current_state_ = STATE_EVENT_READY;
  this->needs_initial_ui_update_ = true;
  return true;
}

void EventBasedSchedulable::handle_ready_state_() {
  if (this->current_state_ != STATE_EVENT_READY) {
    return;
  }
  
  // Force UI update on first loop after initialization
  if (this->needs_initial_ui_update_) {
    this->update_event_based_ui_();
    this->needs_initial_ui_update_ = false;
  }
  
  // Check if we should trigger the event and update UI if events changed
  int old_index = this->current_event_index_;
  this->check_and_advance_events_();
  if (old_index != this->current_event_index_) {
    this->update_event_based_ui_();
  }
}

bool EventBasedSchedulable::handle_prerequisites_() {
  // Update HA connection status first if not connected
  if (!this->ha_connected_) {
    this->check_ha_connection_();
  }
  
  auto prereq_error = this->check_prerequisites_();
  
  // Handle prerequisite errors
  if (prereq_error == PREREQ_TIME_INVALID) {
    if (this->current_state_ != STATE_EVENT_TIME_INVALID) {
      ESP_LOGW(TAG, "Time is not valid, schedule operations paused");
      this->current_state_ = STATE_EVENT_TIME_INVALID;
    }
    this->display_current_next_events_("Time Invalid", "Time Invalid");
    return false;
  }
  
  if (prereq_error == PREREQ_SCHEDULE_INVALID) {
    if (this->current_state_ != STATE_EVENT_SCHEDULE_INVALID) {
      ESP_LOGW(TAG, "Schedule is not valid");
      this->current_state_ = STATE_EVENT_SCHEDULE_INVALID;
    }
    this->display_current_next_events_("Schedule Invalid", "Schedule Invalid");
    return false;
  }
  
  if (prereq_error == PREREQ_SCHEDULE_EMPTY) {
    if (this->current_state_ != STATE_EVENT_SCHEDULE_EMPTY) {
      ESP_LOGI(TAG, "Schedule is empty, no events to process");
      this->current_state_ = STATE_EVENT_SCHEDULE_EMPTY;
    }
    this->display_current_next_events_("Schedule Empty", "Schedule Empty");
    return false;
  }
  
  // Prerequisites OK - transition from error states to INIT if needed
  if (this->current_state_ == STATE_EVENT_TIME_INVALID || 
      this->current_state_ == STATE_EVENT_SCHEDULE_INVALID || 
      this->current_state_ == STATE_EVENT_SCHEDULE_EMPTY) {
    this->current_state_ = STATE_EVENT_INIT;
    ESP_LOGV(TAG, "Prerequisites met, transitioning to INIT state");
  }
  
  return true;
}

} // namespace schedule
} // namespace esphome
