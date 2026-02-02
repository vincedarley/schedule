#pragma once

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/core/log.h"
#include "state_based_schedulable.h"  // For ScheduleMode enum

namespace esphome {
namespace schedule {

// Forward declaration
class Schedule;

class StateModeSelect : public select::Select, public Component {
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
      // No saved preference, default to "Manual Off"
      this->publish_state(SCHEDULE_MODE_MANUAL_OFF);
    } else {
      const auto &options = this->traits.get_options();
      if (index < options.size()) {
        this->publish_state(index);
      } else {
        this->publish_state(SCHEDULE_MODE_MANUAL_OFF);  // Default to Manual Off
      }
    }
    // Notify via callback with the published state
    if (this->on_value_callback_ && this->has_state()) {
      this->on_value_callback_(this->current_option());
    }
  }
  
  void loop() override {
    // If a correction was made and it's been more than 10 seconds, force Manual Off
    if (this->correction_time_ > 0) {
      uint32_t now = millis();
      if (now - this->correction_time_ >= 1000) {  // 10 seconds
        this->correction_time_ = 0;  // Reset timer
        
        // Get current index
        size_t current_index = this->index_of(this->current_option()).value_or(SCHEDULE_MODE_MANUAL_OFF);
        
        // If still in a schedule-dependent mode while schedule is empty, force to Manual Off
        if (this->schedule_is_empty_ && 
            (current_index == SCHEDULE_MODE_EARLY_OFF || 
             current_index == SCHEDULE_MODE_AUTO || 
             current_index == SCHEDULE_MODE_BOOST_ON)) {
          
          ESP_LOGI("schedule.mode_select", "Forcing mode to Manual Off after 10s delay (schedule is empty)");
          this->publish_state(SCHEDULE_MODE_MANUAL_OFF);
          
          // Trigger callback with Manual Off
          if (this->on_value_callback_) {
            this->on_value_callback_(this->option_at(SCHEDULE_MODE_MANUAL_OFF));
          }
          
          // Save preference
          uint8_t index_u8 = static_cast<uint8_t>(SCHEDULE_MODE_MANUAL_OFF);
          this->pref_.save(&index_u8);
        }
      }
    }
  }
  

  // Set manual-only mode (schedule is empty)
  void set_manual_only_mode(bool manual_only) {
    this->schedule_is_empty_ = manual_only;
    
    // Don't dynamically change traits - keep all 5 options available
    // Just switch to appropriate state based on schedule status
    
    size_t current_index = this->index_of(this->current_option()).value_or(SCHEDULE_MODE_MANUAL_OFF);
    size_t new_index;
    
    if (manual_only) {
      // Schedule is empty - if in schedule-dependent mode, switch to Manual Off
      // Schedule-dependent modes: Early Off, Auto, Boost On
      if (current_index == SCHEDULE_MODE_EARLY_OFF || 
          current_index == SCHEDULE_MODE_AUTO || 
          current_index == SCHEDULE_MODE_BOOST_ON || 
          !this->has_state()) {
        new_index = SCHEDULE_MODE_MANUAL_OFF;
      } else {
        // Keep Manual Off or Manual On
        new_index = current_index;
      }
    } else {
      // Schedule has entries - keep current selection
      new_index = this->has_state() ? current_index : SCHEDULE_MODE_MANUAL_OFF;
    }
    
    // Publish the state
    this->publish_state(new_index);
    
    if (this->on_value_callback_) {
      this->on_value_callback_(this->option_at(new_index));
    }
  }
  
 
 protected:
  void control(size_t index) override {
    // Check if user is trying to select a schedule-dependent mode when schedule is empty
    // Schedule-dependent modes: Early Off, Auto, Boost On
    // Manual modes: Manual Off, Manual On
    if (this->schedule_is_empty_ && 
        (index == SCHEDULE_MODE_EARLY_OFF || 
         index == SCHEDULE_MODE_AUTO || 
         index == SCHEDULE_MODE_BOOST_ON)) {

        // Publish the rejected value first, then the correct value
        // This forces HA to see a state change
        this->publish_state(index);
        // Get the string value only for logging/notification
        const std::string value = this->option_at(index);
        ESP_LOGW("schedule.mode_select", "Cannot select '%s': schedule is empty", value.c_str());
   
        // Send notification to Home Assistant
        if (this->schedule_ != nullptr) {
            this->schedule_->send_notification(
            "Cannot select " + value + " mode",
            std::string("Schedule '") + this->schedule_->get_name() + "' is empty - only Manual Off and Manual On are available"
            );
        }
      
        // Trigger callback with Manual Off
        if (this->on_value_callback_) {
            this->on_value_callback_(this->option_at(SCHEDULE_MODE_MANUAL_OFF));
        }
        //this->publish_state(SCHEDULE_MODE_MANUAL_OFF);
        
        // Start a 1-second timer to force correction of the mode displayed by HA
        this->correction_time_ = millis();
        
        return;
    }
    
    // Save the selected option index (cast to uint8_t to match preference type)
    uint8_t index_u8 = static_cast<uint8_t>(index);
    this->pref_.save(&index_u8);
    
    // Publish the selected state
    this->publish_state(index);
    
    // Notify via callback
    if (this->on_value_callback_) {
      this->on_value_callback_(this->option_at(index));
    }
  }

  Schedule *schedule_{nullptr};
  ESPPreferenceObject pref_;
  std::function<void(const std::string&)> on_value_callback_;
  uint32_t correction_time_{0};  // Time when mode correction was made
  bool schedule_is_empty_{true};
};;

}  // namespace schedule
}  // namespace esphome
