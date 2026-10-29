// Where the mod's files are. The plugin lives in GTA Vice City.app/Contents/Frameworks
// next to libfpv.dylib; its log and test files go in Contents/Resources, the
// folder the game runs in. libfpv's settings are in its shared folder
// (fpvapi.cpp).
#pragma once

#include <string>

namespace paths {

std::string frameworks_dir();  // the folder this dylib was loaded from
std::string resources_dir();
std::string data_dir();        // the mod's test triggers and shots; libfpv's
                               // settings before the shared folder

}  // namespace paths
