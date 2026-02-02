#pragma once

#include "event_based_schedulable.h"
#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/core/log.h"

namespace esphome {
namespace schedule {

// Forward declaration
class Schedule;

// EventModeSelect - Simplified mode select for event-based components
// Only supports Disabled/Enabled (no state-based modes like Manual On/Off)
class EventModeSelect : public select::Select, public Component {
 public:
  void set_on_value_callback(std::function<void(const std::string&)> &&callback) {
    this->on_value_callback_ = callback;
  }
  
  void set_schedule(Schedule *schedule) {
    this->schedule_ = schedule;
  }

  void setup() override {
    this->pref_ = global_preferences->make_preference<uint8_t>(this->get_object_id_hash());
    uint8_t index;
    if (!this->pref_.load(&index)) {
      // No saved preference, default to "Disabled" and mark as first boot
      this->had_saved_preference_ = false;
      this->publish_state(SCHEDULE_EVENT_MODE_DISABLED);
    } else {
      // Preference was loaded from storage
      this->had_saved_preference_ = true;
      const auto &options = this->traits.get_options();
      if (index < options.size()) {
        this->publish_state(index);
      } else {
        this->publish_state(SCHEDULE_EVENT_MODE_DISABLED);
      }
    }
    // Notify via callback with the published state
    if (this->on_value_callback_ && this->has_state()) {
      this->on_value_callback_(this->current_option());
    }
  }
  
  void loop() override {
    // If a correction was made and it's been more than 10 seconds, force Disabled
    if (this->correction_time_ > 0) {
      uint32_t now = millis();
      if (now - this->correction_time_ >= 1000) {  // 1 second
        this->correction_time_ = 0;  // Reset timer
        
        // Get current index
        size_t current_index = this->index_of(this->current_option()).value_or(SCHEDULE_EVENT_MODE_DISABLED);
        
        // If still Enabled while schedule is empty, force to Disabled
        if (this->schedule_is_empty_ && current_index == SCHEDULE_EVENT_MODE_ENABLED) {
          
          ESP_LOGI("schedule.mode_select", "Forcing mode to Disabled after 10s delay (schedule is empty)");
          this->publish_state(SCHEDULE_EVENT_MODE_DISABLED);
          
          // Trigger callback with Disabled
          if (this->on_value_callback_) {
            this->on_value_callback_(this->option_at(SCHEDULE_EVENT_MODE_DISABLED));
          }
          
          // Save preference
          uint8_t index_u8 = static_cast<uint8_t>(SCHEDULE_EVENT_MODE_DISABLED);
          this->pref_.save(&index_u8);
        }
      }
    }
  }

  // Set disabled-only mode (schedule is empty)
  void set_disabled_only_mode(bool disabled_only) {
    this->schedule_is_empty_ = disabled_only;
    
    // Don't dynamically change traits - keep both options available
    // Just switch to appropriate state based on schedule status
    
    size_t current_index = this->index_of(this->current_option()).value_or(SCHEDULE_EVENT_MODE_DISABLED);
    size_t new_index;
    
    if (disabled_only) {
      // Schedule is empty, force to Disabled
      new_index = SCHEDULE_EVENT_MODE_DISABLED;
    } else {
      // Schedule has entries
      if (current_index == SCHEDULE_EVENT_MODE_DISABLED || !this->has_state()) {
        // Only auto-enable if a preference was previously saved (not first boot)
        // On first boot with no saved preference, stay disabled until user enables
        if (this->had_saved_preference_) {
          new_index = SCHEDULE_EVENT_MODE_ENABLED;
        } else {
          new_index = SCHEDULE_EVENT_MODE_DISABLED;
        }
      } else {
        // Keep current selection if valid
        new_index = current_index;
      }
    }
    
    // Publish the state
    this->publish_state(new_index);
    
    if (this->on_value_callback_) {
      this->on_value_callback_(this->option_at(new_index));
    }
  }
  
 protected:
  void control(size_t index) override {
    // Check if user is trying to enable when schedule is empty
    if (this->schedule_is_empty_ && index == SCHEDULE_EVENT_MODE_ENABLED) {
      // Get the string value only for logging/notification
      const std::string value = this->option_at(index);
      ESP_LOGW("schedule.mode_select", "Cannot enable: schedule is empty");
      
      // Publish the rejected value first, then the correct value
      // This forces HA to see a state change
      this->publish_state(index);

      
      // Send notification to Home Assistant
      if (this->schedule_ != nullptr) {
        this->schedule_->send_notification("Cannot enable schedule mode", 
          std::string("Schedule '") + this->schedule_->get_name() + "' is empty - no events to trigger");
      }
      
      // Trigger callback with Disabled to ensure internal consistency
      if (this->on_value_callback_) {
        this->on_value_callback_(this->option_at(SCHEDULE_EVENT_MODE_DISABLED));
      }
     // this->publish_state(SCHEDULE_EVENT_MODE_DISABLED);
      
      // Start a 1 second timer to force correction if HA doesn't update
      this->correction_time_ = millis();
      
      return;
            
    }
    
    // Save the selected option index (cast to uint8_t to match preference type)
    uint8_t index_u8 = static_cast<uint8_t>(index);
    this->pref_.save(&index_u8);
    
    // Publish the selected state
    this->publish_state(index);
    
    // Trigger callback
    if (this->on_value_callback_) {
      this->on_value_callback_(this->option_at(index));
    }
  }

 private:
  Schedule *schedule_{nullptr};
  ESPPreferenceObject pref_;
  std::function<void(const std::string&)> on_value_callback_;
  bool schedule_is_empty_{true};
  bool had_saved_preference_{false};  // Track if preference existed at boot
  uint32_t correction_time_{0};  // Time when mode correction was made
};

} // namespace schedule
} // namespace esphome
