/**
 * @file tmp117_basic_example.cpp
 * @brief Initialise the TMP117 from the menuconfig values and log the temperature twice a second (a
 * new result arrives every cycle, 1 s by default).
 *
 * Wiring: examples/esp32/README.md. Expected output: one "initialised" line with the silicon
 * revision, then "23.456 C" lines. Every Result is checked and logged by name.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp32_tmp117_port.hpp"
#include "tmp117.hpp"
#include "tmp117_kconfig.hpp"

namespace {
constexpr const char* kTag = "tmp117_example";
}

extern "C" void app_main(void) {
  static Esp32Tmp117Port port; // static: the driver keeps a reference
  static tmp117::TMP117<Esp32Tmp117Port> sensor(port, tmp117::KconfigDefaults()); // no I/O

  ESP_LOGI(kTag, "TMP117 driver %s", tmp117::GetDriverVersion());
  const auto init = sensor.EnsureInitialized();
  if (!init) {
    ESP_LOGE(kTag, "EnsureInitialized failed: %s", tmp117::ToString(init.error));
    return;
  }
  ESP_LOGI(kTag, "initialised, silicon revision %u", static_cast<unsigned>(sensor.GetRevision()));

  while (true) {
    const auto t = sensor.ReadTemperatureMilliC();
    if (t) {
      const long mc = static_cast<long>(t.value);
      ESP_LOGI(kTag, "%s%ld.%03ld C", (mc < 0) ? "-" : "", (mc < 0 ? -mc : mc) / 1000,
               (mc < 0 ? -mc : mc) % 1000);
    } else {
      ESP_LOGW(kTag, "read failed: %s", tmp117::ToString(t.error));
      if (t.error == tmp117::Error::UnexpectedReset) {
        (void)sensor.EnsureInitialized(); // the chip lost its configuration: start it again
      } else {
        // Bus, NoMeasurement or another error: the next read reports again.
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
