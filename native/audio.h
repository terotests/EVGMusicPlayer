// SPDX-License-Identifier: AGPL-3.0-or-later
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
  float volume_ = 0.8f;

  // The analyser.
  static constexpr int FFT = 2048;
  std::vector<float> ring_ = std::vector<float>(FFT * 2, 0.f);
  size_t ringPos_ = 0;
  std::vector<float> smooth_ = std::vector<float>(FFT / 2, 0.f);
  float bands_[BANDS] = {0};
  float level_ = 0;
};
