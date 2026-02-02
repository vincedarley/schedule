#include "state_mode_select.h"
#include "esphome/core/log.h"
#include "esphome/components/api/api_server.h"

namespace esphome {
namespace schedule {

static const char *const TAG = "schedule.state_mode_select";

// Mode string constants
static const char *MODE_MANUAL_OFF = "Manual Off";
static const char *MODE_EARLY_OFF = "Early Off";
static const char *MODE_AUTO = "Auto";
static const char *MODE_MANUAL_ON = "Manual On";
static const char *MODE_BOOST_ON = "Boost On";

void StateModeSelect::setup() {
  // Set all options FIRST
  this->traits.set_options({MODE_MANUAL_OFF, MODE_EARLY_OFF, MODE_AUTO, MODE_MANUAL_ON, MODE_BOOST_ON});
  
  // Create custom preference with offset to avoid conflict with base class
  this->pref_ = global_preferences->make_preference<uint8_t>(this->get_object_id_hash() + 0x1000);
  
  // Load saved mode
  this->load_mode_from_pref();
}

void StateModeSelect::loop() {
  // Clear initial boot flag after 10 seconds to allow normal auto-correction
  static bool boot_flag_cleared = false;
  if (this->initial_boot_ && !boot_flag_cleared && millis() > 10000) {
    this->initial_boot_ = false;
    boot_flag_cleared = true;
    ESP_LOGV(TAG, "Initial boot period complete - auto-correction now enabled");
  }
  
  // Check if we need to apply a mode correction after 2 seconds
  if (this->mode_correction_time_ > 0) {
    uint32_t now = millis();
    if (now - this->mode_correction_time_ >= 2000) {  // 2 seconds elapsed
      this->mode_correction_time_ = 0;  // Reset timer
      
      if (!this->pending_correction_mode_.empty()) {
        ESP_LOGI(TAG, "Applying mode correction to: %s", this->pending_correction_mode_.c_str());
        this->publish_state(this->pending_correction_mode_);
        this->pending_correction_mode_.clear();
        
        // Notify schedulable of the corrected mode
        if (this->schedulable_) {
          auto current = this->current_option();
          this->schedulable_->on_mode_changed(current.c_str());
        }
      }
    }
  }
}

void StateModeSelect::control(const std::string &value) {
  // Check if selecting schedule-dependent mode when schedule is empty (and not pending download)
  if (this->schedule_empty_ && !this->schedule_pending_) {
    if (strcmp(value.c_str(), MODE_EARLY_OFF) == 0 || 
        strcmp(value.c_str(), MODE_AUTO) == 0 || 
        strcmp(value.c_str(), MODE_BOOST_ON) == 0) {
      ESP_LOGW(TAG, "Cannot select %s - schedule is empty", value.c_str());
      
      if (this->schedule_) {
        std::string schedule_id = this->schedule_->get_object_id();
        std::string message = "Cannot select " + value + " for schedule '" + schedule_id + "' - Schedule is empty. Reverting to Manual Off.";
        this->schedule_->send_notification(message, "Schedule: " + schedule_id);
      }
      
      this->mode_correction_time_ = millis();
      this->pending_correction_mode_ = MODE_MANUAL_OFF;
    }
  }
  
  this->publish_state(value);
  
  if (this->schedulable_) {
    this->schedulable_->on_mode_changed(value);
  }
  
  this->save_mode_to_pref();
}

void StateModeSelect::load_mode_from_pref() {
  uint8_t index;
  if (this->pref_.load(&index)) {
    const auto &options = this->traits.get_options();
    if (index < options.size()) {
      select::Select::publish_state(options[index]);
      return;
    } else {
      ESP_LOGW(TAG, "Loaded index %d out of range (max %d), defaulting to Manual Off", index, options.size());
    }
  }
  
  select::Select::publish_state(MODE_MANUAL_OFF);
}

void StateModeSelect::save_mode_to_pref() {
  auto index_opt = this->active_index();
  if (index_opt.has_value()) {
    uint8_t index = static_cast<uint8_t>(index_opt.value());
    this->pref_.save(&index);
  }
}

void StateModeSelect::update_available_modes(bool schedule_valid, bool schedule_pending) {
  this->schedule_valid_ = schedule_valid;
  this->schedule_pending_ = schedule_pending;
  
  this->traits.set_options({MODE_MANUAL_OFF, MODE_EARLY_OFF, MODE_AUTO, MODE_MANUAL_ON, MODE_BOOST_ON});
  
  // Don't auto-correct during initial boot - let the restored mode persist
  // Only auto-correct when user actively selects an invalid mode or schedule changes after boot
  if (this->initial_boot_) {
    ESP_LOGV(TAG, "Initial boot - skipping auto-correction, current mode: %s", this->current_option().c_str());
    return;
  }
  
  if (this->schedule_empty_ && !schedule_pending) {
    auto current = this->current_option();
    if (strcmp(current.c_str(), MODE_EARLY_OFF) == 0 || 
        strcmp(current.c_str(), MODE_AUTO) == 0 || 
        strcmp(current.c_str(), MODE_BOOST_ON) == 0) {
      ESP_LOGW(TAG, "Schedule empty - correcting %s to Manual Off", current.c_str());
      
      if (this->schedule_) {
        std::string schedule_id = this->schedule_->get_object_id();
        std::string message = "Schedule '" + schedule_id + "' is empty while in " + current + " mode. Reverting to Manual Off.";
        this->schedule_->send_notification(message, "Schedule: " + schedule_id);
      }
      
      this->mode_correction_time_ = millis();
      this->pending_correction_mode_ = MODE_MANUAL_OFF;
    }
  }
}

} // namespace schedule
} // namespace esphome
