// What the drone's impacts do to peds, vehicles and props.
#pragma once

#include <libfpv.h>

namespace effects {

// Once per drone step. Game thread.
void on_frame(const fpv_frame &frame);

}  // namespace effects
