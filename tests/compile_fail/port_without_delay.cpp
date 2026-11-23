/**
 * @file port_without_delay.cpp
 * @brief Must NOT compile: the chip's reset and conversion waits need the port's DelayUs().
 *
 * EXPECT: DelayUs
 */
#include "mock_tmp117.hpp"
#include "tmp117.hpp"

class NoDelayPort : public tmp117_test::MockPortBase<NoDelayPort> {};

int main() {
  NoDelayPort port;
  tmp117::TMP117<NoDelayPort> sensor(port);
  return static_cast<int>(sensor.EnsureInitialized().error);
}
