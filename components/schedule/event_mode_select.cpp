#include "event_mode_select.h"
#include "esphome/core/log.h"

namespace esphome {
namespace schedule {

static const char *const TAG = "schedule.event_mode_select";

// Mode string constants
static const char *MODE_DISABLED = "Disabled";
static const char *MODE_ENABLED = "Enabled";

void EventModeSelect::setup() {
  // Set all options FIRST
  this->traits.set_options({MODE_DISABLED, MODE_ENABLED});
  
  // Create custom preference with offset to avoid conflict with base class
  this->pref_ = global_preferences->make_preference<uint8_t>(this->get_object_id_hash() + 0x1000);
  
  // Load saved mode
  this->load_mode_from_pref();
}

void EventModeSelect::loop() {
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
      }
    }
  }
}

void EventModeSelect::control(const std::string &value) {
  // Check if selecting Enabled mode when schedule is empty (and not pending download)
  if (this->schedule_empty_ && !this->schedule_pending_) {
    if (strcmp(value.c_str(), MODE_ENABLED) == 0) {
      ESP_LOGW(TAG, "Cannot select Enabled - schedule is empty");
      
      if (this->schedule_) {
        std::string schedule_id = this->schedule_->get_object_id();
        std::string message = "Cannot select Enabled for schedule '" + schedule_id + "' - Schedule is empty. Reverting to Disabled.";
        this->schedule_->send_notification(message, "Schedule: " + schedule_id);
      }
      
      this->mode_correction_time_ = millis();
      this->pending_correction_mode_ = MODE_DISABLED;
    }
  }
  
  this->publish_state(value);
  
  if (this->schedulable_) {
    this->schedulable_->on_mode_changed(value);
  }
  
  this->save_mode_to_pref();
}

void EventModeSelect::load_mode_from_pref() {
  uint8_t index;
  if (this->pref_.load(&index)) {
    const auto &options = this->traits.get_options();
    if (index < options.size()) {
      select::Select::publish_state(options[index]);
      return;
    } else {
      ESP_LOGW(TAG, "Loaded index %d out of range (max %d), defaulting to Enabled", index, options.size());
    }
  }
  
  // Default to Enabled on first boot (no saved preference)
  select::Select::publish_state(MODE_ENABLED);
}

void EventModeSelect::save_mode_to_pref() {
  auto index_opt = this->active_index();
  if (index_opt.has_value()) {
    uint8_t index = static_cast<uint8_t>(index_opt.value());
    this->pref_.save(&index);
  }
}

void EventModeSelect::update_available_modes(bool schedule_valid, bool schedule_pending) {
  this->schedule_valid_ = schedule_valid;
  this->schedule_pending_ = schedule_pending;
  
  this->traits.set_options({MODE_DISABLED, MODE_ENABLED});
  
  // Don't auto-correct during initial boot - let the restored mode persist
  // Only auto-correct when user actively selects an invalid mode or schedule changes after boot
  if (this->initial_boot_) {
    ESP_LOGV(TAG, "Initial boot - skipping auto-correction, current mode: %s", this->current_option().c_str());
    return;
  }
  
  if (this->schedule_empty_ && !schedule_pending) {
    auto current = this->current_option();
    if (strcmp(current.c_str(), MODE_ENABLED) == 0) {
      ESP_LOGW(TAG, "Schedule empty - correcting Enabled to Disabled");
      
      if (this->schedule_) {
        std::string schedule_id = this->schedule_->get_object_id();
        std::string message = "Schedule '" + schedule_id + "' is empty while in Enabled mode. Reverting to Disabled.";
        this->schedule_->send_notification(message, "Schedule: " + schedule_id);
      }
      
      this->mode_correction_time_ = millis();
      this->pending_correction_mode_ = MODE_DISABLED;
    }
  }
}

} // namespace schedule
} // namespace esphome
