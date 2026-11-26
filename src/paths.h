// Where the mod's files are. The plugin (libfpv-reVC.dylib) lives in
// GTA Vice City.app/Contents/Frameworks next to libfpv.dylib; its log, test
// triggers and screenshots go in Contents/Resources/libfpv, next to the
// folder the game runs in (labelled libfpv, as libfpv's integration guide
// asks of every game). libfpv's settings are in its shared folder (fpvapi.cpp).
#pragma once

#include <string>

namespace paths {

std::string frameworks_dir();   // the folder this dylib was loaded from
std::string resources_dir();
std::string data_dir();         // the mod's log, test triggers and shots (created on first use)
std::string legacy_data_dir();  // Contents/Resources/fpv-data: libfpv's settings before the shared folder

}  // namespace paths
