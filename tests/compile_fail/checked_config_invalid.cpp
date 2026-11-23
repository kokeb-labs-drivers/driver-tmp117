/**
 * @file checked_config_invalid.cpp
 * @brief Must NOT compile: a CheckedConfig built from a configuration that Validate() rejects fails
 * the build, naming the error (handbook KB-CPP20-013).
 *
 * EXPECT: ConfigIsInvalid
 */
#include "tmp117_types.hpp"

int main() {
  // The low limit above the high limit is not described by the datasheet: Validate() rejects it.
  constexpr tmp117::CheckedConfig kBoard{
      tmp117::Config{.low_limit_mc = 50000, .high_limit_mc = 20000}};
  return static_cast<int>(kBoard.Get().low_limit_mc);
}
