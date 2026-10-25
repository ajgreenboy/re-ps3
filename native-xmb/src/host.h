#pragma once
#include <QString>
#include <functional>

namespace xmb::host {
// The machine the XMB runs on, for the settings that map onto it (System Information, Network status, Video
// Output, Volume). Everything here reads or writes the real host; nothing is simulated.
QString systemInformation(const QString& renderer);   // OS, computer name, CPU, memory, GPU, display
using Label=std::function<QString(const QString& id,const QString& fallback)>; // firmware string lookup
QString networkStatus(const Label& label);             // physical adapters that are up, in the PS3 list's fields
QString audioOutput();                                 // default playback device name and its volume
QString displayOutput();                              // current resolution and refresh rate of each screen
// Windows default playback device (Core Audio endpoint). Volume 0-100; -1 / false when unavailable.
int masterVolume();
bool setMasterVolume(int percent);
bool masterMuted();
bool setMasterMuted(bool muted);
}
