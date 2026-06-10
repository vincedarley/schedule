#include "esphome.h"
#include "schedule_select.h"
#include "../schedule.h"

namespace esphome {
namespace schedule {

static const char *const TAG = "schedule.select";

void ScheduleSelect::dump_config() {
  ESP_LOGCONFIG(TAG, "Schedule Select '%s'", this->EntityBase::get_name().c_str());
  // Call base schedule configuration
  this->dump_config_base();
}

optional<std::string> ScheduleSelect::get_current_option_from_sensor_() {
  // Get the uint8_t data sensor (should be first and only one)
  if (this->data_sensors_.empty()) {
    ESP_LOGW(TAG, "'%s': No data sensor configured for option selection", this->EntityBase::get_name().c_str());
    return nullopt;
  }

  auto *sensor = this->data_sensors_[0];
  if (sensor->get_item_type() != 0) {  // Type 0 = uint8_t
    ESP_LOGE(TAG, "'%s': Data sensor '%s' is not uint8_t type (type=%u)", 
             this->EntityBase::get_name().c_str(), sensor->get_label().c_str(), sensor->get_item_type());
    return nullopt;
  }

  // Use current_event_index_ from Schedule base class
  if (this->current_event_index_ < 0) {
    ESP_LOGW(TAG, "'%s': No current event index available", this->EntityBase::get_name().c_str());
    return nullopt;
  }

  // Retrieve the stored index (uint8_t)
  float index_as_float = sensor->get_sensor_value(static_cast<size_t>(this->current_event_index_));
  uint8_t option_index = static_cast<uint8_t>(index_as_float);

  // Convert index back to string using select's options list (use const ref - can't copy FixedVector)
  const auto& options = this->traits.get_options();
  
  if (option_index >= options.size()) {
    ESP_LOGW(TAG, "'%s': Option index %u out of range (max: %u)", 
             this->EntityBase::get_name().c_str(), option_index, static_cast<unsigned>(options.size() - 1));
    return nullopt;
  }

  return options[option_index];
}

void ScheduleSelect::update_event_based_ui_() {
  // Build event text with preset option names instead of generic "EVENT"
  std::string current_text = "at " + this->format_event_time_(this->current_event_raw_ & TIME_MASK);
  std::string next_text = "at " + this->format_event_time_(this->next_event_raw_ & TIME_MASK);
  
  // Enhance event text with preset option if available
  if (!this->data_sensors_.empty()) {
    auto *sensor = this->data_sensors_[0];
    const auto& options = this->traits.get_options();
    
    // Add current event preset to text
    if (this->current_event_index_ >= 0) {
      float current_index_float = sensor->get_sensor_value(static_cast<size_t>(this->current_event_index_));
      uint8_t current_option_index = static_cast<uint8_t>(current_index_float);
      if (current_option_index < options.size()) {
        current_text = std::string(options[current_option_index]) + " at " + this->format_event_time_(this->current_event_raw_ & TIME_MASK);
      }
    }
    
    // Add next event preset to text
    if (this->next_event_index_ >= 0) {
      float next_index_float = sensor->get_sensor_value(static_cast<size_t>(this->next_event_index_));
      uint8_t next_option_index = static_cast<uint8_t>(next_index_float);
      if (next_option_index < options.size()) {
        next_text = std::string(options[next_option_index]) + " at " + this->format_event_time_(this->next_event_raw_ & TIME_MASK);
      }
    }
  }
  
  // Update current/next event text sensors
  this->display_current_next_events_(current_text, next_text);
  
  // Update data sensors with current event index
  this->set_data_sensors_(this->current_event_index_, true, false);
  
  // Publish current state to Home Assistant
  // This ensures the select entity shows the correct value in HA, not "unknown"
  auto current_option = this->get_current_option_from_sensor_();
  if (current_option.has_value()) {
    ESP_LOGI(TAG, "'%s': Publishing initial/updated state to HA: '%s'", 
             this->EntityBase::get_name().c_str(), current_option.value().c_str());
    this->publish_state(current_option.value());
  }
}

void ScheduleSelect::apply_scheduled_state(bool on) {
  if (!on) {
    // Event-based: only act when event triggers (on=true)
    return;
  }

  // Get current option from data sensor
  auto scheduled_option = this->get_current_option_from_sensor_();
  if (!scheduled_option.has_value()) {
    return;  // Error already logged by helper
  }
  
  // Option is valid - publish the state
  ESP_LOGI(TAG, "'%s': Scheduled event triggered, setting option to '%s'", 
           this->EntityBase::get_name().c_str(), scheduled_option.value().c_str());
  this->publish_state(scheduled_option.value());
}

void ScheduleSelect::control(const std::string &value) {
  // User manually changed the selection
  ESP_LOGI(TAG, "'%s': Manual selection changed to '%s'", this->EntityBase::get_name().c_str(), value.c_str());
  this->publish_state(value);
}

// Note: setup() is defined inline in the header
// Note: loop() is handled by EventBasedSchedulable base class

} // namespace schedule
} // namespace esphome
