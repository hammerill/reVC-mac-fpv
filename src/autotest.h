// A scripted test flight, started by creating Resources/libfpv/autotest.txt while a
// game is loaded. The file is deleted when the test starts, so a normal game
// start never runs it. See autotest.cpp for what it does.
#pragma once

namespace autotest {

// Once per frame after CGame::Process. Game thread.
void tick();

}  // namespace autotest
