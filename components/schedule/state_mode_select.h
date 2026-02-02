#pragma once

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/core/log.h"
#include "state_based_schedulable.h"  // For ScheduleMode enum

namespace esphome {
namespace schedule {

// Forward declarations
class Schedule;
class StateBasedSchedulable;

class StateModeSelect : public select::Select, public Component {
 public:
  void set_on_value_callback(std::function<void(const std::string&)> &&callback) {
    this->on_value_callback_ = callback;
  }
  
  void set_schedule(Schedule *schedule) {
    this->schedule_ = schedule;
  }
  
  void setup() override;
  void loop() override;
  

  // Set manual-only mode (schedule is empty) - DEPRECATED, use update_available_modes
  void set_manual_only_mode(bool manual_only);

  // Update available modes based on schedule validity and pending download
  void update_available_modes(bool schedule_valid, bool schedule_pending);
  
  // Set schedule empty state
  void set_schedule_empty(bool is_empty) { this->schedule_empty_ = is_empty; }
  
  void control(const std::string &value) override;

  void set_schedulable(StateBasedSchedulable *schedulable) { this->schedulable_ = schedulable; }

 private:
  Schedule *schedule_{nullptr};
  StateBasedSchedulable *schedulable_{nullptr};
  ESPPreferenceObject pref_;
  std::function<void(const std::string&)> on_value_callback_;
  uint32_t correction_time_{0};  // Time when mode correction was made
  uint32_t mode_correction_time_{0};  // Time when mode correction was made (new implementation)
  std::string pending_correction_mode_;  // Mode to correct to
  bool schedule_is_empty_{true};
  bool schedule_valid_{false};  // Track if schedule is valid
  bool schedule_empty_{true};  // Track if schedule is empty
  bool schedule_pending_{false};  // Track if schedule download is pending
  bool initial_boot_{true};  // Track if this is initial boot (prevents auto-correction during boot)
  size_t last_published_option_count_{0};  // Track last published option count
  
  void load_mode_from_pref();
  void save_mode_to_pref();
};

}  // namespace schedule
}  // namespace esphome
