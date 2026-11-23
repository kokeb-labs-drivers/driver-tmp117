/**
 * @file test_bus_faults.cpp
 * @brief Every public operation reports a failed bus transfer as Error::Bus, whichever transfer
 * fails.
 *
 * Each operation is run once on a clean bus to count its transfers, then once per transfer with
 * exactly that transfer failing. This reaches every "the transfer failed, return the error" path in
 * the driver, which a single fault test per operation would not.
 */
#include <cstdio>

#include "mock_tmp117.hpp"
#include "test_harness.hpp"
#include "test_support.hpp"
#include "tmp117.hpp"

using tmp117::Error;
using tmp117_test::FullPort;
using tmp117_test::MinimalPort;

namespace {

/// Runs `op` with each of its transfers failing in turn; every run must return Error::Bus.
/// With `poll_tolerant`, a failed read inside a busy poll may be retried to success (Ok), never to
/// another error.
template <typename Port, typename Op>
void SweepBusFaults(const char* name, const tmp117::Config& config, Op op,
                    bool poll_tolerant = false) {
  int transfers = 0;
  {
    Port clean;
    tmp117::TMP117<Port> sensor(clean, config);
    REQUIRE(sensor.EnsureInitialized().ok());
    clean.DelayUs(2000000U); // a first result exists
    const int before = clean.state.transfers;
    CHECK_EQ(op(sensor), Error::Ok);
    transfers = clean.state.transfers - before;
  }
  if (transfers <= 0) {
    std::printf("    %s: no transfers counted\n", name);
  }
  CHECK(transfers > 0);
  for (int i = 0; i < transfers; ++i) {
    Port port;
    tmp117::TMP117<Port> sensor(port, config);
    REQUIRE(sensor.EnsureInitialized().ok());
    port.DelayUs(2000000U);
    port.state.fail_transfer = port.state.transfers + i;
    const Error e = op(sensor);
    if (e != Error::Bus && (!poll_tolerant || e != Error::Ok)) {
      std::printf("    %s: transfer %d of %d failed, got %s\n", name, i + 1, transfers,
                  tmp117::ToString(e));
    }
    CHECK(e == Error::Bus || (poll_tolerant && e == Error::Ok));
  }
}

tmp117::Config Custom() {
  tmp117::Config c{};
  c.averaging = tmp117::Averaging::X64;
  return c;
}

} // namespace

TEST_CASE("Every transfer of every read can fail and is reported as Bus") {
  using S = tmp117::TMP117<MinimalPort>;
  SweepBusFaults<MinimalPort>("ReadStatus", {}, [](S& s) { return s.ReadStatus().error; });
  SweepBusFaults<MinimalPort>("ReadTemperatureRaw", {},
                              [](S& s) { return s.ReadTemperatureRaw().error; });
  SweepBusFaults<MinimalPort>("ReadTemperatureMilliC", {},
                              [](S& s) { return s.ReadTemperatureMilliC().error; });
  SweepBusFaults<MinimalPort>("ReadTemperatureMilliC(Device)", {}, [](S& s) {
    return s.ReadTemperatureMilliC(tmp117::Verify::Device).error;
  });
  SweepBusFaults<MinimalPort>("CheckHealth", {}, [](S& s) { return s.CheckHealth().error; });
  SweepBusFaults<MinimalPort>("ReadUniqueId", {}, [](S& s) { return s.ReadUniqueId().error; });
  SweepBusFaults<MinimalPort>("ReadRegister", {}, [](S& s) { return s.ReadRegister(0x02).error; });
}

TEST_CASE("Every transfer of every configuration change can fail and is reported as Bus") {
  using S = tmp117::TMP117<MinimalPort>;
  SweepBusFaults<MinimalPort>("Configure", Custom(),
                              [](S& s) { return s.Configure(tmp117::Config{}).error; });
  SweepBusFaults<MinimalPort>("SetMode", {},
                              [](S& s) { return s.SetMode(tmp117::Mode::Shutdown).error; });
  SweepBusFaults<MinimalPort>("SetAveraging", {},
                              [](S& s) { return s.SetAveraging(tmp117::Averaging::X64).error; });
  SweepBusFaults<MinimalPort>("SetLimits", {}, [](S& s) { return s.SetLimits(0, 50000).error; });
  SweepBusFaults<MinimalPort>("SetOffsetMilliC", {},
                              [](S& s) { return s.SetOffsetMilliC(1500).error; });
  SweepBusFaults<MinimalPort>("SoftReset", {}, [](S& s) { return s.SoftReset().error; });
  SweepBusFaults<MinimalPort>("Deinitialize", {}, [](S& s) { return s.Deinitialize().error; });
}

TEST_CASE("Every transfer of a one-shot and of the programming can fail and is reported as Bus") {
  using S = tmp117::TMP117<MinimalPort>;
  SweepBusFaults<MinimalPort>("StartOneShot", {}, [](S& s) { return s.StartOneShot().error; });
  SweepBusFaults<MinimalPort>("ReadOneShotMilliC", {},
                              [](S& s) { return s.ReadOneShotMilliC().error; });
  SweepBusFaults<MinimalPort>("WaitDataReady", {}, [](S& s) { return s.WaitDataReady(50).error; });
  SweepBusFaults<MinimalPort>(
      "ProgramUserWord", {}, [](S& s) { return s.ProgramUserWord(0x1234).error; }, true);
  SweepBusFaults<MinimalPort>(
      "ProgramPowerOnDefaults", Custom(),
      [](S& s) { return s.ProgramPowerOnDefaults(Custom()).error; }, true);
}

TEST_CASE("Every transfer of the general-call reset can fail and is reported as Bus") {
  using S = tmp117::TMP117<FullPort>;
  tmp117::Config c{};
  c.allow_general_call = true;
  SweepBusFaults<FullPort>("GeneralCallReset", c, [](S& s) { return s.GeneralCallReset().error; });
}
