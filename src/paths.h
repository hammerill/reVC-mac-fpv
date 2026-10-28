// Where the mod's files are. The plugin lives in GTA Vice City.app/Contents/Frameworks
// next to libfpv.dylib; its log and settings go in Contents/Resources, the
// folder the game runs in.
#pragma once

#include <string>

namespace paths {

std::string frameworks_dir();  // the folder this dylib was loaded from
std::string resources_dir();
std::string data_dir();        // libfpv's settings, radio mappings, EEPROM

}  // namespace paths
