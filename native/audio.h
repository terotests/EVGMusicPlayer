// SPDX-License-Identifier: MIT
//
// Playback and analysis for the native player: what <audio> and the Web Audio
// AnalyserNode do for the page.
//
//   MP3  decoded as it plays by dr_mp3
//   WAV  read whole by SDL_LoadWAV
//
// Both are resampled to the device by an SDL_AudioStream. The output is also
// written, mixed to mono, into a ring the analyser reads; `bands` turns that
// into the same 32 log-spaced levels web/main.js computes, through the same
// FFT size, smoothing and dB range the browser's analyser uses.

#pragma once
#include <SDL.h>

#include <string>
#include <vector>

class Audio {
 public:
  static constexpr int BANDS = 32;

  bool open(std::string& err);
  void close();
  // Close the device and open the system's default output again, keeping the
  // track, its position and whether it was playing. For a device that went
  // away (a display or headphones unplugged, the machine asleep) or one that
  // stopped keeping time.
  bool reopen(std::string& err);
  SDL_AudioDeviceID device() const { return dev_; }
  // True once after the device was seen taking audio much faster than real
  // time: SDL's stand-in for a lost device, which plays nothing. Decoding
  // stops the moment it is seen, so the track and its clock stay where the
  // sound stopped; the host reopens the device.
  bool takeRacing();
  bool load(const std::string& path, std::string& err);
  void play();
  void pause();
  void stop();
  void seek(double seconds);
  double position();
  double duration() const { return duration_; }
  bool playing();
  bool hasTrack() const { return source_ != None; }
  // True once after a track played to its end.
  bool takeEnded();
  void setVolume(float v);
  float volume() const { return volume_; }
  // Advance the band levels by dtMs and write them out; `level` is the bass.
  void bands(double dtMs, float out[BANDS], float& level);

 private:
  enum Source { None, Mp3, Pcm };
  static void callback(void* self, Uint8* stream, int len);
  void fill(Uint8* stream, int len);
  int decode(float* out, int frames);  // source frames as float, interleaved
  void unload();

  SDL_AudioDeviceID dev_ = 0;
  int devRate_ = 44100;
  SDL_AudioStream* stream_ = nullptr;
  Source source_ = None;
  void* mp3_ = nullptr;          // drmp3*
  std::vector<float> pcm_;       // Pcm source, interleaved
  size_t pcmPos_ = 0;            // in frames
  int channels_ = 2, srcRate_ = 44100;
  double duration_ = 0;
  bool playing_ = false, srcEnd_ = false, ended_ = false;
  double seekBase_ = 0;
  Uint64 devFrames_ = 0;
  // The watchdog: frames the device took against the wall clock, per window.
  Uint64 windowStartMs_ = 0, windowFrames_ = 0;
  bool racing_ = false;
  float volume_ = 0.8f;

  // The analyser.
  static constexpr int FFT = 2048;
  std::vector<float> ring_ = std::vector<float>(FFT * 2, 0.f);
  size_t ringPos_ = 0;
  std::vector<float> smooth_ = std::vector<float>(FFT / 2, 0.f);
  float bands_[BANDS] = {0};
  float level_ = 0;
};
