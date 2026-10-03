// SPDX-License-Identifier: MIT
// See audio.h.

#include "audio.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

#define DR_MP3_IMPLEMENTATION
#include "third_party/dr_mp3.h"

namespace {

bool endsWith(std::string s, const char* ext) {
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
  size_t n = std::strlen(ext);
  return s.size() >= n && s.compare(s.size() - n, n, ext) == 0;
}

void fft(std::vector<std::complex<float>>& a) {
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; i++) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    float ang = -2.f * (float)M_PI / (float)len;
    std::complex<float> wl(std::cos(ang), std::sin(ang));
    for (size_t i = 0; i < n; i += len) {
      std::complex<float> w(1.f, 0.f);
      for (size_t k = 0; k < len / 2; k++) {
        auto u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}

}  // namespace

bool Audio::open(std::string& err) {
  SDL_AudioSpec want{}, have{};
  want.freq = 44100;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = 1024;
  want.callback = &Audio::callback;
  want.userdata = this;
  dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (!dev_) {
    err = SDL_GetError();
    return false;
  }
  devRate_ = have.freq;
  SDL_PauseAudioDevice(dev_, 0);
  return true;
}

void Audio::close() {
  if (dev_) SDL_CloseAudioDevice(dev_);
  dev_ = 0;
  unload();
}

bool Audio::reopen(std::string& err) {
  // Where the listener is: the clock, not the decoder, which a device that
  // raced may have run ahead of what was heard.
  double at = position();
  bool wasPlaying = playing_;
  if (dev_) SDL_CloseAudioDevice(dev_);  // waits for the callback to finish
  dev_ = 0;
  playing_ = false;
  racing_ = false;
  windowStartMs_ = windowFrames_ = 0;
  if (!open(err)) return false;
  if (source_ != None) {
    SDL_LockAudioDevice(dev_);
    // The new device may run at another rate: a new converter to it.
    if (stream_) SDL_FreeAudioStream(stream_);
    stream_ = SDL_NewAudioStream(AUDIO_F32SYS, (Uint8)channels_, srcRate_, AUDIO_F32SYS, 2, devRate_);
    SDL_UnlockAudioDevice(dev_);
    seek(at);
    if (wasPlaying) play();
  }
  return true;
}

bool Audio::takeRacing() {
  if (!dev_) return false;
  SDL_LockAudioDevice(dev_);
  bool r = racing_;
  SDL_UnlockAudioDevice(dev_);
  return r;
}

void Audio::unload() {
  if (mp3_) {
    drmp3_uninit((drmp3*)mp3_);
    delete (drmp3*)mp3_;
    mp3_ = nullptr;
  }
  if (stream_) SDL_FreeAudioStream(stream_);
  stream_ = nullptr;
  pcm_.clear();
  source_ = None;
}

bool Audio::load(const std::string& path, std::string& err) {
  if (dev_) SDL_LockAudioDevice(dev_);
  unload();
  playing_ = false;
  bool ok = false;
  if (endsWith(path, ".mp3")) {
    auto* mp3 = new drmp3;
    if (drmp3_init_file(mp3, path.c_str(), nullptr)) {
      channels_ = (int)mp3->channels;
      srcRate_ = (int)mp3->sampleRate;
      drmp3_uint64 frames = drmp3_get_pcm_frame_count(mp3);
      drmp3_seek_to_pcm_frame(mp3, 0);
      duration_ = srcRate_ > 0 ? (double)frames / srcRate_ : 0;
      mp3_ = mp3;
      source_ = Mp3;
      ok = true;
    } else {
      delete mp3;
      err = "not an MP3 this decoder can read";
    }
  } else if (endsWith(path, ".wav")) {
    SDL_AudioSpec spec;
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if (SDL_LoadWAV(path.c_str(), &spec, &buf, &len)) {
      // To float at the file's own rate; the stream does the rate.
      SDL_AudioCVT cvt;
      SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_F32SYS, spec.channels, spec.freq);
      std::vector<Uint8> tmp((size_t)len * (cvt.len_mult > 0 ? cvt.len_mult : 1));
      std::memcpy(tmp.data(), buf, len);
      SDL_FreeWAV(buf);
      cvt.buf = tmp.data();
      cvt.len = (int)len;
      if (!cvt.needed || SDL_ConvertAudio(&cvt) == 0) {
        size_t bytes = cvt.needed ? (size_t)cvt.len_cvt : len;
        pcm_.resize(bytes / sizeof(float));
        std::memcpy(pcm_.data(), tmp.data(), bytes);
        channels_ = spec.channels;
        srcRate_ = spec.freq;
        duration_ = (double)(pcm_.size() / channels_) / srcRate_;
        source_ = Pcm;
        ok = true;
      } else {
        err = SDL_GetError();
      }
    } else {
      err = SDL_GetError();
    }
  } else {
    err = "only MP3 and WAV files are played";
  }
  if (ok) {
    stream_ = SDL_NewAudioStream(AUDIO_F32SYS, (Uint8)channels_, srcRate_, AUDIO_F32SYS, 2, devRate_);
    pcmPos_ = 0;
    srcEnd_ = false;
    ended_ = false;
    seekBase_ = 0;
    devFrames_ = 0;
    windowStartMs_ = windowFrames_ = 0;
  }
  if (dev_) SDL_UnlockAudioDevice(dev_);
  return ok;
}

void Audio::play() {
  if (!dev_ || source_ == None) return;
  SDL_LockAudioDevice(dev_);
  if (srcEnd_ && stream_ && SDL_AudioStreamAvailable(stream_) == 0) {
    // Played to the end: play again from the start.
    SDL_UnlockAudioDevice(dev_);
    seek(0);
    SDL_LockAudioDevice(dev_);
  }
  playing_ = true;
  SDL_UnlockAudioDevice(dev_);
}

void Audio::pause() {
  if (!dev_) return;
  SDL_LockAudioDevice(dev_);
  playing_ = false;
  SDL_UnlockAudioDevice(dev_);
}

void Audio::stop() {
  pause();
  seek(0);
}

void Audio::seek(double s) {
  if (!dev_ || source_ == None) return;
  SDL_LockAudioDevice(dev_);
  s = std::max(0.0, std::min(s, duration_));
  Uint64 frame = (Uint64)(s * srcRate_);
  if (source_ == Mp3) drmp3_seek_to_pcm_frame((drmp3*)mp3_, frame);
  if (source_ == Pcm) pcmPos_ = std::min((size_t)frame, pcm_.size() / channels_);
  SDL_AudioStreamClear(stream_);
  srcEnd_ = false;
  seekBase_ = s;
  devFrames_ = 0;
  windowStartMs_ = windowFrames_ = 0;
  SDL_UnlockAudioDevice(dev_);
}

double Audio::position() {
  if (!dev_) return 0;
  SDL_LockAudioDevice(dev_);
  double p = seekBase_ + (double)devFrames_ / devRate_;
  SDL_UnlockAudioDevice(dev_);
  return std::min(p, duration_);
}

bool Audio::playing() {
  if (!dev_) return false;
  SDL_LockAudioDevice(dev_);
  bool p = playing_;
  SDL_UnlockAudioDevice(dev_);
  return p;
}

bool Audio::takeEnded() {
  if (!dev_) return false;
  SDL_LockAudioDevice(dev_);
  bool e = ended_;
  ended_ = false;
  SDL_UnlockAudioDevice(dev_);
  return e;
}

void Audio::setVolume(float v) {
  volume_ = std::max(0.f, std::min(1.f, v));
}

int Audio::decode(float* out, int frames) {
  if (source_ == Mp3) return (int)drmp3_read_pcm_frames_f32((drmp3*)mp3_, (drmp3_uint64)frames, out);
  if (source_ == Pcm) {
    size_t total = pcm_.size() / channels_;
    size_t n = std::min((size_t)frames, total - pcmPos_);
    std::memcpy(out, &pcm_[pcmPos_ * channels_], n * channels_ * sizeof(float));
    pcmPos_ += n;
    return (int)n;
  }
  return 0;
}

void Audio::callback(void* self, Uint8* stream, int len) {
  static_cast<Audio*>(self)->fill(stream, len);
}

void Audio::fill(Uint8* out, int len) {
  std::memset(out, 0, (size_t)len);
  if (!playing_ || !stream_ || racing_) {
    windowStartMs_ = windowFrames_ = 0;
    return;
  }
  // A device that takes audio much faster than it can play it is not
  // playing it (SDL's stand-in for a lost device does exactly that). Stop
  // feeding it at once, so neither the track nor its clock run away, and
  // let the host open a real device.
  // A budget, checked on every call: within a window of a few seconds the
  // device may take 1.5 times what real time allows, plus 300 ms for the
  // buffers it fills up front. Past that it is not playing what it takes:
  // what it took beyond real time never reached a speaker, so the clock
  // gives it back, and nothing more is decoded for it.
  Uint64 now = SDL_GetTicks64();
  if (!windowStartMs_ || now - windowStartMs_ > 4000) {
    windowStartMs_ = now;
    windowFrames_ = 0;
  }
  double elapsedMs = (double)(now - windowStartMs_);
  Uint64 want = (Uint64)(len / (int)(2 * sizeof(float)));
  double budget = (elapsedMs + 300.0) * devRate_ / 1000.0 * 1.5;
  if ((double)(windowFrames_ + want) > budget) {
    Uint64 real = (Uint64)(elapsedMs * devRate_ / 1000.0);
    if (windowFrames_ > real) devFrames_ -= std::min(devFrames_, windowFrames_ - real);
    racing_ = true;
    return;
  }
  float tmp[8192];
  const int chunk = 8192 / std::max(1, channels_);  // frames that fit in tmp
  while (SDL_AudioStreamAvailable(stream_) < len && !srcEnd_) {
    int got = decode(tmp, chunk);
    if (got <= 0) {
      srcEnd_ = true;
      SDL_AudioStreamFlush(stream_);
      break;
    }
    SDL_AudioStreamPut(stream_, tmp, got * channels_ * (int)sizeof(float));
  }
  int got = SDL_AudioStreamGet(stream_, out, len);
  if (got < 0) got = 0;
  auto* f = reinterpret_cast<float*>(out);
  int frames = got / (int)(2 * sizeof(float));
  for (int i = 0; i < frames; i++) {
    float l = f[i * 2], r = f[i * 2 + 1];
    ring_[ringPos_] = 0.5f * (l + r);
    ringPos_ = (ringPos_ + 1) % ring_.size();
    f[i * 2] = l * volume_;
    f[i * 2 + 1] = r * volume_;
  }
  devFrames_ += (Uint64)frames;
  windowFrames_ += (Uint64)frames;
  if (srcEnd_ && SDL_AudioStreamAvailable(stream_) == 0) {
    playing_ = false;
    ended_ = true;
  }
}

void Audio::bands(double dtMs, float out[BANDS], float& level) {
  // What the browser's AnalyserNode does for getByteFrequencyData: a Blackman
  // window, |X| / N, time smoothing 0.72, and dB from -100 to -30 mapped to
  // 0..255. web/main.js then folds the bins into bands; so does this.
  std::vector<std::complex<float>> a(FFT);
  bool live = false;
  if (dev_) {
    SDL_LockAudioDevice(dev_);
    live = playing_;
    size_t start = (ringPos_ + ring_.size() - FFT) % ring_.size();
    for (int i = 0; i < FFT; i++) a[i] = ring_[(start + i) % ring_.size()];
    SDL_UnlockAudioDevice(dev_);
  }
  const float fall = (float)std::exp(-dtMs / 220.0);
  if (live) {
    for (int i = 0; i < FFT; i++) {
      float x = (float)i / FFT;
      float w = 0.42f - 0.5f * std::cos(2.f * (float)M_PI * x) + 0.08f * std::cos(4.f * (float)M_PI * x);
      a[i] *= w;
    }
    fft(a);
    std::vector<float> bytes(FFT / 2);
    for (int k = 0; k < FFT / 2; k++) {
      float mag = std::abs(a[k]) / FFT;
      smooth_[k] = 0.72f * smooth_[k] + 0.28f * mag;
      float db = 20.f * std::log10(std::max(smooth_[k], 1e-12f));
      bytes[k] = std::max(0.f, std::min(255.f, 255.f * (db + 100.f) / 70.f));
    }
    const int bins = FFT / 2;
    int edges[BANDS + 1];
    for (int i = 0; i <= BANDS; i++) {
      double f = 40.0 * std::pow(16000.0 / 40.0, (double)i / BANDS);
      edges[i] = std::min(bins - 1, std::max(1, (int)std::lround(f / (devRate_ / 2.0) * bins)));
    }
    for (int i = 0; i < BANDS; i++) {
      float sum = 0;
      int n = 0;
      for (int k = edges[i]; k <= std::max(edges[i], edges[i + 1] - 1); k++) {
        sum += bytes[k];
        n++;
      }
      float tilt = 1.f + ((float)i / BANDS) * 0.6f;
      float target = std::min(1.f, std::pow((sum / n) / 255.f, 1.4f) * tilt);
      bands_[i] = target > bands_[i] ? target : bands_[i] * fall;
    }
  } else {
    for (float& b : bands_) b *= fall;
  }
  float bass = (bands_[0] + bands_[1] + bands_[2] + bands_[3]) / 4;
  level_ = bass > level_ ? bass : level_ * (float)std::exp(-dtMs / 160.0);
  std::memcpy(out, bands_, sizeof bands_);
  level = level_;
}
