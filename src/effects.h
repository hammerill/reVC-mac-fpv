// What the drone's impacts do to peds, vehicles and props.
#pragma once

#include <libfpv.h>

namespace effects {

// Once per drone step. Game thread.
void on_frame(const fpv_frame &frame);

// The drone's props cut the peds it flies through (on by default). Off while
// something else does it: the web mission's RC helicopter rotor.
void set_prop_strikes(bool on);

}  // namespace effects
