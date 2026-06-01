#pragma once
#include "esphome/core/component.h"
#include "esphome/components/select/select.h"
#include "../event_based_schedulable.h"

namespace esphome {
namespace schedule {

// ScheduleSelect class - select that changes based on schedule events
class ScheduleSelect : public select::Select, public EventBasedSchedulable {
 public:
  // Sync entity info from Select's EntityBase to Schedule base class
  void setup() override {
    // Get object ID from EntityBase (via Select)
    char object_id_buf[128];
    auto object_id = this->EntityBase::get_object_id_to(object_id_buf);
    this->sync_from_entity(std::string(object_id.c_str()), this->EntityBase::get_name().c_str());
    
    // Set up string→index preprocessor for data sensor
    if (!this->data_sensors_.empty()) {
      auto *sensor = this->data_sensors_[0];
      
      // Capture 'this' to access select's options list
      sensor->set_value_preprocessor([this](const std::string &option_str) -> std::string {
        // Get the select's options list (use const reference - can't copy FixedVector)
        const auto& options = this->traits.get_options();
        
        // Find the index of the option string
        for (size_t i = 0; i < options.size(); i++) {
          if (options[i] == option_str) {
            // Return index as string (will be converted to uint8_t by DataSensor)
            return std::to_string(i);
          }
        }
        
        // Option not found - log warning and return "0" as fallback
        ESP_LOGW("schedule.select", "'%s': Unknown option '%s', defaulting to index 0", 
                 this->EntityBase::get_name().c_str(), option_str.c_str());
        return "0";
      });
    }
    
    // Call EventBasedSchedulable's setup
    EventBasedSchedulable::setup();
  }
  
  void dump_config() override;
  
 protected:
  // Platform-specific implementation of scheduled event application
  // For Select: read string option from data sensor and publish state
  void apply_scheduled_state(bool on) override;
  
  // Override to publish initial state to Home Assistant
  void update_event_based_ui_() override;
  
  // Handle manual selection changes
  void control(const std::string &value) override;
  
 private:
  // Helper to get current option string from data sensor
  optional<std::string> get_current_option_from_sensor_();
};

} // namespace schedule
} // namespace esphome
