// SPDX-License-Identifier: AGPL-3.0-or-later
// See platform.h. The window parts for macOS are in mac_window.mm.

#include "platform.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#if !defined(__APPLE__)
void platformMakeTransparent(SDL_Window*) {}
void platformRefreshShadow(SDL_Window*) {}
bool platformUsesShapeApi() { return true; }
#endif

namespace {

std::string run(const std::string& cmd) {
  std::string out;
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) return out;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
  pclose(p);
  return out;
}

bool have(const char* tool) {
  return !run(std::string("command -v ") + tool + " 2>/dev/null").empty();
}

std::vector<std::string> lines(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start < s.size()) {
    size_t nl = s.find('\n', start);
    if (nl == std::string::npos) nl = s.size();
    std::string line = s.substr(start, nl - start);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (!line.empty()) out.push_back(line);
    start = nl + 1;
  }
  return out;
}

}  // namespace

bool isAudioFile(const std::string& path) {
  std::string ext = std::filesystem::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext == ".mp3" || ext == ".wav";
}

std::vector<std::string> pickAudioFiles() {
#if defined(__APPLE__)
  return lines(run(
      "osascript -e 'set fl to choose file with prompt \"Choose music\" of type {\"public.mp3\", \"com.microsoft.waveform-audio\"} with multiple selections allowed'"
      " -e 'set out to \"\"' -e 'repeat with f in fl' -e 'set out to out & POSIX path of f & linefeed' -e 'end repeat'"
      " -e 'return out' 2>/dev/null"));
#else
  if (have("zenity")) {
    return lines(run("zenity --file-selection --multiple --separator='\n' --title='Choose music'"
                     " --file-filter='Audio | *.mp3 *.MP3 *.wav *.WAV' 2>/dev/null"));
  }
  if (have("kdialog")) {
    return lines(run("kdialog --getopenfilename --multiple --separate-output . 'Audio (*.mp3 *.wav)' 2>/dev/null"));
  }
  return {};
#endif
}

std::string pickFolder() {
  std::vector<std::string> l;
#if defined(__APPLE__)
  l = lines(run("osascript -e 'POSIX path of (choose folder with prompt \"Choose a music folder\")' 2>/dev/null"));
#else
  if (have("zenity")) l = lines(run("zenity --file-selection --directory --title='Choose a music folder' 2>/dev/null"));
  else if (have("kdialog")) l = lines(run("kdialog --getexistingdirectory . 2>/dev/null"));
#endif
  return l.empty() ? "" : l[0];
}

std::vector<std::string> audioFilesUnder(const std::string& dir) {
  std::vector<std::string> out;
  std::error_code ec;
  for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
       !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    if (it->is_regular_file(ec) && isAudioFile(it->path().string())) out.push_back(it->path().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}
