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

void ScheduleSelect::apply_scheduled_state(bool on) {
  if (!on) {
    // Event-based: only act when event triggers (on=true)
    return;
  }

  // Get the uint8_t data sensor (should be first and only one)
  if (this->data_sensors_.empty()) {
    ESP_LOGW(TAG, "'%s': No data sensor configured for option selection", this->EntityBase::get_name().c_str());
    return;
  }

  auto *sensor = this->data_sensors_[0];
  if (sensor->get_item_type() != 0) {  // Type 0 = uint8_t
    ESP_LOGE(TAG, "'%s': Data sensor '%s' is not uint8_t type (type=%u)", 
             this->EntityBase::get_name().c_str(), sensor->get_label().c_str(), sensor->get_item_type());
    return;
  }

  // Use current_event_index_ from Schedule base class
  if (this->current_event_index_ < 0) {
    ESP_LOGW(TAG, "'%s': No current event index available", this->EntityBase::get_name().c_str());
    return;
  }

  // Retrieve the stored index (uint8_t)
  float index_as_float = sensor->get_sensor_value(static_cast<size_t>(this->current_event_index_));
  uint8_t option_index = static_cast<uint8_t>(index_as_float);

  // Convert index back to string using select's options list (use const ref - can't copy FixedVector)
  const auto& options = this->traits.get_options();
  
  if (option_index >= options.size()) {
    ESP_LOGW(TAG, "'%s': Option index %u out of range (max: %u)", 
             this->EntityBase::get_name().c_str(), option_index, static_cast<unsigned>(options.size() - 1));
    return;
  }

  std::string scheduled_option = options[option_index];
  
  // Option is valid - publish the state
  ESP_LOGI(TAG, "'%s': Scheduled event triggered, setting option to '%s' (index %u)", 
           this->EntityBase::get_name().c_str(), scheduled_option.c_str(), option_index);
  this->publish_state(scheduled_option);
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
