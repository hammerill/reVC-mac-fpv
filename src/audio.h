// libfpv's motor sound through the default output device.
#pragma once

namespace audio {

// Starts pulling fpv_audio_read on CoreAudio's thread. Stop it before the drone goes.
void start();
void stop();

// Signal lost: the motors give way to receiver hiss.
void set_static(bool on);

// Once per frame: the web build feeds the browser's audio from the game's
// thread here (web/src/audio_web.cpp). Nothing to do with CoreAudio's thread.
void pump();

}  // namespace audio
