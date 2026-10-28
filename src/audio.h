// libfpv's motor sound through the default output device.
#pragma once

namespace audio {

// Starts pulling fpv_audio_read on CoreAudio's thread. Stop it before the drone goes.
void start();
void stop();

// Signal lost: the motors give way to receiver hiss.
void set_static(bool on);

}  // namespace audio
