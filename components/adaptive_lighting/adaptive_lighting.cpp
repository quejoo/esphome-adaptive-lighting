#include "adaptive_lighting.h"
#include "adaptive_lighting_version.h"

#include <cmath>
#include <utility>

#include "esphome/core/log.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"

static const char *TAG = "adaptive_lighting";
static constexpr float ELEVATION_ADJUSTMENT_STEP = 0.1f;

namespace esphome::adaptive_lighting {

#if ESPHOME_VERSION_CODE >= VERSION_CODE(2025, 12, 0)
class AdaptiveLightingListenerAdapter : public light::LightRemoteValuesListener {
public:
  explicit AdaptiveLightingListenerAdapter(AdaptiveLightingComponent *parent) : parent_(parent) {}
  void on_light_remote_values_update() override { parent_->on_light_remote_values_update(); }

private:
  AdaptiveLightingComponent *parent_;
};
#endif

AdaptiveLightingComponent::~AdaptiveLightingComponent() {
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2025, 12, 0)
  delete listener_;
  listener_ = nullptr;
#endif
}

void AdaptiveLightingComponent::setup() {
  if (light_ != nullptr) {
    auto traits = light_->get_traits();
    light_min_mireds_ = traits.get_min_mireds();
    light_max_mireds_ = traits.get_max_mireds();

    if (min_mireds_ <= 0) {
      min_mireds_ = light_min_mireds_;
    }
    if (max_mireds_ <= 0) {
      max_mireds_ = light_max_mireds_;
    }

#if ESPHOME_VERSION_CODE >= VERSION_CODE(2025, 12, 0)
    listener_ = new AdaptiveLightingListenerAdapter(this);
    light_->add_remote_values_listener(listener_);
#else
    light_->add_new_remote_values_callback([this]() { on_light_remote_values_update(); });
#endif
  }
  if (this->restore_mode == switch_::SWITCH_ALWAYS_ON) {
    this->publish_state(true);
  }

  if (sun_ != nullptr) {
    auto *time = sun_->get_time();
    if (time != nullptr) {
      time->add_on_time_sync_callback([this]() {
        ESP_LOGI(TAG, "Time synchronized, forcing adaptive lighting update");
        this->force_next_update();
        this->update();
      });
    }
  }
}

static float smooth_transition(float x, float y_min, float y_max, float speed = 1) {
  constexpr double y1 = 0.00001;
  constexpr double y2 = 0.999;
  static float a = (std::atanh(2 * y2 - 1) - std::atanh(2 * y1 - 1));
  static float b = -(std::atanh(2 * y1 - 1) / a);
  auto x_adj = std::pow(std::fabs(1 - x * 2), speed);
  return y_min + (y_max - y_min) * 0.5 * (std::tanh(a * (x_adj - b)) + 1);
}

void AdaptiveLightingComponent::update() {
  if (light_ == nullptr || sun_ == nullptr) {
    ESP_LOGW(TAG, "Light or Sun component not set!");
    return;
  }

  if (!this->state) {
    ESP_LOGD(TAG, "Update skipped - automatic updates disabled");
    return;
  }

  const auto now = sun_->get_time()->now();
  SunEvents sun_events = calc_sun_events(now);

  if (!sun_events.sunrise || !sun_events.sunset) {
    ESP_LOGW(TAG, "Could not determine sunrise or sunset");
    return;
  }

  const time_t sunrise_time = sun_events.sunrise->timestamp;
  const time_t sunset_time = sun_events.sunset->timestamp;
  float mireds = calc_color_temperature(now.timestamp, sunrise_time, sunset_time);
  bool color_needs_update = (!this->color_manually_controlled_ && std::fabs(mireds - last_requested_color_temp_) >= 0.1);

  bool apply_brightness = true;
  if (this->adaptive_brightness_switch_ != nullptr && !this->adaptive_brightness_switch_->state) {
      apply_brightness = false;
  }

  float new_brightness = light_->remote_values.get_brightness(); 
  bool brightness_needs_update = false;

  if (apply_brightness && !this->brightness_manually_controlled_ && 
      this->min_brightness_ != nullptr && this->max_brightness_ != nullptr) {

      float min_b = this->min_brightness_->state / 100.0f;
      float max_b = this->max_brightness_->state / 100.0f;

      if (now.timestamp < sunrise_time || now.timestamp > sunset_time) {
          new_brightness = min_b; 
      } else {
          float position = float(now.timestamp - sunrise_time) / float(sunset_time - sunrise_time);
          new_brightness = smooth_transition(position, max_b, min_b);
      }
      new_brightness = std::roundf(new_brightness * 100.0f) / 100.0f;

      if (this->last_brightness_ < 0.0f || std::fabs(new_brightness - this->last_brightness_) >= 0.01f) {
          brightness_needs_update = true;
      }
  }

  if (!color_needs_update && !brightness_needs_update) {
    return;
  }

  auto call = light_->make_call();

  if (!this->color_manually_controlled_) {
      if (mireds < light_min_mireds_) {
        mireds = light_min_mireds_;
      } else if (mireds > light_max_mireds_) {
        mireds = light_max_mireds_;
      }
      call.set_color_temperature(mireds);
      last_requested_color_temp_ = mireds;
  }

  if (apply_brightness && !this->brightness_manually_controlled_) {
      call.set_brightness(new_brightness);
      this->last_brightness_ = new_brightness;
  }

  if (transition_length_ > 0 && light_->remote_values.is_on()) {
    call.set_transition_length_if_supported(transition_length_);
  }
  
  call.perform();
}

void AdaptiveLightingComponent::write_state(bool state) {
  if (this->state != state) {
    this->force_next_update();
    this->publish_state(state);
    this->update();
  }
}

void AdaptiveLightingComponent::on_light_remote_values_update() {
  if (light_ == nullptr)
    return;

  bool current_state = light_->remote_values.is_on();

  if (current_state) {
    float current_temp = light_->remote_values.get_color_temperature();
    float current_brightness = light_->remote_values.get_brightness();

    if (this->state && !this->color_manually_controlled_ && last_requested_color_temp_ > 0 && std::fabs(current_temp - last_requested_color_temp_) > 0.1) {
      ESP_LOGI(TAG, "Color temperature changed externally, pausing adaptive color");
      this->color_manually_controlled_ = true;
    }

    if (this->state && !this->brightness_manually_controlled_ && this->last_brightness_ >= 0.0f && std::fabs(current_brightness - this->last_brightness_) > 0.01f) {
      ESP_LOGI(TAG, "Brightness changed externally, pausing adaptive brightness");
      this->brightness_manually_controlled_ = true;
      this->last_brightness_ = current_brightness;
    }
  }
  else if (previous_light_state_ && !this->state && this->restore_mode == switch_::SWITCH_ALWAYS_ON) {
    this->write_state(true);
  }

  if (!current_state && previous_light_state_) {
      this->brightness_manually_controlled_ = false;
      this->color_manually_controlled_ = false;
      this->last_brightness_ = -1.0f; 
      this->last_requested_color_temp_ = 0;
  }

  previous_light_state_ = current_state;
}

SunEvents AdaptiveLightingComponent::calc_sun_events(const ESPTime &now) {
  ESPTime today = now;
  today.hour = today.minute = today.second = 0;
  today.recalc_timestamp_local();

  float sunrise_elevation = sunrise_elevation_;
  optional<ESPTime> sunrise = sun_->sunrise(today, sunrise_elevation);

  while (!sunrise && sunrise_elevation < 0.0f) {
    sunrise_elevation += ELEVATION_ADJUSTMENT_STEP;
    sunrise = sun_->sunrise(today, sunrise_elevation);
  }
  while (!sunrise && sunrise_elevation > -90.0f) {
    sunrise_elevation -= ELEVATION_ADJUSTMENT_STEP;
    sunrise = sun_->sunrise(today, sunrise_elevation);
  }

  float sunset_elevation = sunset_elevation_;
  optional<ESPTime> sunset = sun_->sunset(today, sunset_elevation);

  while (!sunset && sunset_elevation < 0.0f) {
    sunset_elevation += ELEVATION_ADJUSTMENT_STEP;
    sunset = sun_->sunset(today, sunset_elevation);
  }
  while (!sunset && sunset_elevation > -90.0f) {
    sunset_elevation -= ELEVATION_ADJUSTMENT_STEP;
    sunset = sun_->sunset(today, sunset_elevation);
  }

  return {today, sunrise, sunset, sunrise_elevation, sunset_elevation};
}

float AdaptiveLightingComponent::calc_color_temperature(const time_t now, const time_t sunrise, const time_t sunset,
                                                        float min_mireds, float max_mireds, float speed) {
  if (now < sunrise || now > sunset) {
    return max_mireds;
  } else {
    float position = float(now - sunrise) / float(sunset - sunrise);
    float mireds = smooth_transition(position, min_mireds, max_mireds, speed);
    return std::roundf(mireds * 10) / 10;
  }
}

void AdaptiveLightingComponent::dump_config() {
  if (light_ == nullptr || sun_ == nullptr) {
    return;
  }

  const auto now = sun_->get_time()->now();
  SunEvents sun_events = calc_sun_events(now);

  if (!sun_events.sunrise || !sun_events.sunset) {
    return;
  }

  for (int i = 0; i < 24; i++) {
    auto time = sun_events.today;
    time.hour = i;
    time.recalc_timestamp_local();
    float mireds = calc_color_temperature(time.timestamp, sun_events.sunrise->timestamp, sun_events.sunset->timestamp);
  }
}

} // namespace esphome::adaptive_lighting