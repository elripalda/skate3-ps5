// Skate 3 for PS5 - the hub's own sound: its music and menu sounds (sound.h).
#include "sound.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#define DR_MP3_IMPLEMENTATION
#define DRMP3_API static
#include "thirdparty/dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DRFLAC_API static
#include "thirdparty/dr_flac.h"
#define STB_VORBIS_NO_PUSHDATA_API
#include "thirdparty/stb_vorbis.c"
#undef L
#undef C
#undef R

#if defined(__PROSPERO__)
#include <pthread.h>
extern "C" {
int32_t sceAudioOutInit(void);
int32_t sceAudioOutOpen(int32_t user_id, int32_t type, int32_t index, uint32_t len, uint32_t freq, uint32_t param);
int32_t sceAudioOutOutput(int32_t handle, const void* ptr);
}
#endif

namespace skate3ps5::sound {
namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// A decoder: frames at the file's own rate and channel count.
class Decoder {
 public:
  virtual ~Decoder() = default;
  virtual size_t Read(float* out, size_t frames) = 0;  // interleaved, `channels` per frame
  virtual bool Rewind() = 0;
  uint32_t rate = 0, channels = 0;
  uint64_t total = 0;  // frames (0: not known)
};

class Mp3 final : public Decoder {
 public:
  bool Open(const std::filesystem::path& f) {
    if (!drmp3_init_file(&mp3_, f.string().c_str(), nullptr)) return false;
    open_ = true;
    rate = mp3_.sampleRate, channels = mp3_.channels;
    return rate && channels;
  }
  ~Mp3() override {
    if (open_) drmp3_uninit(&mp3_);
  }
  size_t Read(float* out, size_t frames) override { return size_t(drmp3_read_pcm_frames_f32(&mp3_, frames, out)); }
  bool Rewind() override { return drmp3_seek_to_pcm_frame(&mp3_, 0); }

 private:
  drmp3 mp3_{};
  bool open_ = false;
};

class Flac final : public Decoder {
 public:
  bool Open(const std::filesystem::path& f) {
    flac_ = drflac_open_file(f.string().c_str(), nullptr);
    if (!flac_) return false;
    rate = flac_->sampleRate, channels = flac_->channels, total = flac_->totalPCMFrameCount;
    return rate && channels;
  }
  ~Flac() override {
    if (flac_) drflac_close(flac_);
  }
  size_t Read(float* out, size_t frames) override { return size_t(drflac_read_pcm_frames_f32(flac_, frames, out)); }
  bool Rewind() override { return drflac_seek_to_pcm_frame(flac_, 0); }

 private:
  drflac* flac_ = nullptr;
};

class Ogg final : public Decoder {
 public:
  bool Open(const std::filesystem::path& f) {
    int error = 0;
    ogg_ = stb_vorbis_open_filename(f.string().c_str(), &error, nullptr);
    if (!ogg_) return false;
    const stb_vorbis_info info = stb_vorbis_get_info(ogg_);
    rate = info.sample_rate, channels = uint32_t(info.channels), total = stb_vorbis_stream_length_in_samples(ogg_);
    return rate && channels;
  }
  ~Ogg() override {
    if (ogg_) stb_vorbis_close(ogg_);
  }
  size_t Read(float* out, size_t frames) override {
    return size_t(stb_vorbis_get_samples_float_interleaved(ogg_, int(channels), out, int(frames * channels)));
  }
  bool Rewind() override { return stb_vorbis_seek_start(ogg_) != 0; }

 private:
  stb_vorbis* ogg_ = nullptr;
};

// WAV: PCM 8/16/24/32-bit or 32-bit float (plain or WAVE_FORMAT_EXTENSIBLE).
class Wav final : public Decoder {
 public:
  bool Open(const std::filesystem::path& f) {
    file_ = std::fopen(f.string().c_str(), "rb");
    if (!file_) return false;
    uint8_t riff[12];
    if (std::fread(riff, 1, 12, file_) != 12 || std::memcmp(riff, "RIFF", 4) || std::memcmp(riff + 8, "WAVE", 4))
      return false;
    bool have_fmt = false;
    for (;;) {
      uint8_t head[8];
      if (std::fread(head, 1, 8, file_) != 8) return false;
      const uint32_t size = U32(head + 4);
      if (!std::memcmp(head, "fmt ", 4)) {
        uint8_t fmt[40] = {};
        const size_t n = std::min<size_t>(size, sizeof(fmt));
        if (std::fread(fmt, 1, n, file_) != n) return false;
        if (size > n) std::fseek(file_, long(size - n), SEEK_CUR);
        uint32_t tag = U16(fmt);
        channels = U16(fmt + 2), rate = U32(fmt + 4), bits_ = U16(fmt + 14);
        if (tag == 0xFFFE && size >= 26) tag = U16(fmt + 24);  // (extensible: the sub-format's first word)
        float_ = tag == 3;
        if ((tag != 1 && tag != 3) || (float_ && bits_ != 32) || (!float_ && bits_ != 8 && bits_ != 16 && bits_ != 24 && bits_ != 32))
          return false;
        have_fmt = true;
      } else if (!std::memcmp(head, "data", 4)) {
        if (!have_fmt || !channels) return false;
        data_ = std::ftell(file_);
        frame_bytes_ = channels * (bits_ / 8);
        total = size / frame_bytes_;
        left_ = total;
        return rate != 0;
      } else {
        std::fseek(file_, long(size + (size & 1)), SEEK_CUR);
      }
    }
  }
  ~Wav() override {
    if (file_) std::fclose(file_);
  }
  size_t Read(float* out, size_t frames) override {
    frames = size_t(std::min<uint64_t>(frames, left_));
    buf_.resize(frames * frame_bytes_);
    const size_t got = std::fread(buf_.data(), frame_bytes_, frames, file_);
    left_ -= got;
    const size_t n = got * channels;
    const uint8_t* p = buf_.data();
    for (size_t i = 0; i < n; ++i) {
      switch (bits_) {
        case 8: out[i] = (float(p[i]) - 128.0f) / 128.0f; break;
        case 16: out[i] = float(int16_t(U16(p + i * 2))) / 32768.0f; break;
        case 24: out[i] = float(int32_t(U32Of3(p + i * 3) << 8) >> 8) / 8388608.0f; break;
        default: {
          const uint32_t v = U32(p + i * 4);
          if (float_) std::memcpy(&out[i], &v, 4);
          else out[i] = float(int32_t(v)) / 2147483648.0f;
        }
      }
    }
    return got;
  }
  bool Rewind() override {
    left_ = total;
    return std::fseek(file_, data_, SEEK_SET) == 0;
  }

 private:
  static uint32_t U16(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }
  static uint32_t U32(const uint8_t* p) { return U16(p) | U16(p + 2) << 16; }
  static uint32_t U32Of3(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16; }
  std::FILE* file_ = nullptr;
  long data_ = 0;
  uint32_t bits_ = 0, frame_bytes_ = 1;
  bool float_ = false;
  uint64_t left_ = 0;
  std::vector<uint8_t> buf_;
};

// Any decoder as 48 kHz stereo (linear resampling; mono doubled, more than
// two channels: the first two).
class Resampled final : public Source {
 public:
  explicit Resampled(std::unique_ptr<Decoder> d) : d_(std::move(d)), step_(double(d_->rate) / kRate) {}
  size_t Read(float* lr, size_t frames) override {
    size_t done = 0;
    while (done < frames) {
      if (pos_ + 1 >= double(have_)) {
        if (!Fill()) break;
        continue;
      }
      const size_t i = size_t(pos_);
      const float t = float(pos_ - double(i));
      const float* a = &buf_[i * 2];
      const float* b = &buf_[(i + 1) * 2];
      lr[done * 2] = a[0] + (b[0] - a[0]) * t;
      lr[done * 2 + 1] = a[1] + (b[1] - a[1]) * t;
      pos_ += step_;
      ++done;
    }
    return done;
  }
  bool Rewind() override {
    if (!d_->Rewind()) return false;
    buf_.clear(), have_ = 0, pos_ = 0, ended_ = false;
    return true;
  }
  double Seconds() const override { return d_->total && d_->rate ? double(d_->total) / d_->rate : 0; }

 private:
  // More decoded frames after the current ones (keeping the one under pos_).
  bool Fill() {
    if (ended_) return false;
    const size_t keep = std::min(size_t(pos_), have_);
    buf_.erase(buf_.begin(), buf_.begin() + std::ptrdiff_t(keep * 2));
    have_ -= keep;
    pos_ -= double(keep);
    constexpr size_t kChunk = 2048;
    raw_.resize(kChunk * d_->channels);
    const size_t got = d_->Read(raw_.data(), kChunk);
    if (!got) {
      ended_ = true;
      return false;
    }
    for (size_t f = 0; f < got; ++f) {
      const float* s = &raw_[f * d_->channels];
      buf_.push_back(s[0]);
      buf_.push_back(d_->channels > 1 ? s[1] : s[0]);
    }
    have_ += got;
    return true;
  }
  std::unique_ptr<Decoder> d_;
  double step_, pos_ = 0;
  std::vector<float> buf_, raw_;
  size_t have_ = 0;
  bool ended_ = false;
};

class InMemory final : public Source {
 public:
  explicit InMemory(std::shared_ptr<const std::vector<float>> lr) : lr_(std::move(lr)) {}
  size_t Read(float* out, size_t frames) override {
    const size_t n = std::min(frames, lr_->size() / 2 - at_);
    std::memcpy(out, lr_->data() + at_ * 2, n * 2 * sizeof(float));
    at_ += n;
    return n;
  }
  bool Rewind() override {
    at_ = 0;
    return true;
  }
  double Seconds() const override { return double(lr_->size() / 2) / kRate; }

 private:
  std::shared_ptr<const std::vector<float>> lr_;
  size_t at_ = 0;
};

template <typename T>
std::unique_ptr<Decoder> Try(const std::filesystem::path& file) {
  auto d = std::make_unique<T>();
  if (!d->Open(file)) return nullptr;
  return d;
}

}  // namespace

bool CanOpen(const std::filesystem::path& file) {
  const std::string ext = Lower(file.extension().string());
  return ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".ogg";
}

std::unique_ptr<Source> Open(const std::filesystem::path& file) {
  const std::string ext = Lower(file.extension().string());
  std::unique_ptr<Decoder> d;
  if (ext == ".mp3") d = Try<Mp3>(file);
  else if (ext == ".wav") d = Try<Wav>(file);
  else if (ext == ".flac") d = Try<Flac>(file);
  else if (ext == ".ogg") d = Try<Ogg>(file);
  if (!d) return nullptr;
  return std::make_unique<Resampled>(std::move(d));
}

std::unique_ptr<Source> Memory(std::shared_ptr<const std::vector<float>> lr) {
  return std::make_unique<InMemory>(std::move(lr));
}

// ------------------------------------------------------------------- mixer

class Mixer {
 public:
  static Mixer& Get() {
    static Mixer* m = new Mixer;
    return *m;
  }
  std::mutex mutex;
  std::vector<Voice*> voices;
  std::vector<float> scratch, sum;
  std::atomic<bool> game_muted{false};

  void MixInto(int16_t* out, size_t frames) {
    sum.assign(frames * 2, 0.0f);
    scratch.resize(frames * 2);
    {
      std::lock_guard lock(mutex);
      for (Voice* v : voices) Render(*v, frames);
    }
    for (size_t i = 0; i < frames * 2; ++i) {
      // (a soft knee above 0.9 instead of a hard clip when sounds pile up)
      float s = sum[i];
      const float a = std::fabs(s);
      if (a > 0.9f) s = std::copysign(0.9f + 0.1f * std::tanh((a - 0.9f) * 10.0f), s);
      out[i] = int16_t(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
    }
  }

 private:
  void Render(Voice& v, size_t frames) {
    if (!v.source_ || v.paused_) return;
    size_t got = 0;
    while (got < frames) {
      const size_t n = v.source_->Read(scratch.data() + got * 2, frames - got);
      got += n;
      if (n == 0) {
        if (v.loop_ && v.source_->Rewind() && got < frames) {
          const size_t again = v.source_->Read(scratch.data() + got * 2, frames - got);
          if (!again) break;  // (an empty loop)
          got += again;
          continue;
        }
        break;
      }
    }
    const float target = (v.stopping_ || (v.game_sound && game_muted)) ? 0.0f : v.volume_;
    if (v.fresh_) v.gain_ = target, v.fresh_ = false;
    // (volume changes glide over ~20 ms; a fade-out at its own pace)
    const float glide = 1.0f / (0.02f * kRate);
    for (size_t i = 0; i < got; ++i) {
      if (v.stopping_ && v.fade_step_ > 0) v.gain_ = std::max(0.0f, v.gain_ - v.fade_step_);
      else if (v.gain_ < target) v.gain_ = std::min(target, v.gain_ + glide);
      else if (v.gain_ > target) v.gain_ = std::max(target, v.gain_ - glide);
      const float g = v.gain_ * v.gain_;  // (a gentler curve than linear)
      sum[i * 2] += scratch[i * 2] * g;
      sum[i * 2 + 1] += scratch[i * 2 + 1] * g;
    }
    if (got < frames || (v.stopping_ && v.gain_ <= 0.0f)) {
      v.source_.reset();
      v.active_.store(false, std::memory_order_release);
    }
  }
};

void Voice::Play(std::unique_ptr<Source> source, bool loop) {
  Mixer& m = Mixer::Get();
  std::lock_guard lock(m.mutex);
  source_ = std::move(source);
  loop_ = loop, paused_ = false, stopping_ = false, fade_step_ = 0, fresh_ = true;
  active_.store(source_ != nullptr, std::memory_order_release);
}

void Voice::Stop() { FadeOut(0.02f); }

void Voice::FadeOut(float seconds) {
  Mixer& m = Mixer::Get();
  std::lock_guard lock(m.mutex);
  if (!source_) return;
  stopping_ = true;
  fade_step_ = std::max(gain_, 0.001f) / (std::max(seconds, 0.001f) * kRate);
}

void Voice::Pause(bool paused) {
  Mixer& m = Mixer::Get();
  std::lock_guard lock(m.mutex);
  paused_ = paused;
}

void Voice::SetVolume(float volume) {
  Mixer& m = Mixer::Get();
  std::lock_guard lock(m.mutex);
  volume_ = std::clamp(volume, 0.0f, 1.0f);
}

Voice* NewVoice() {
  Voice* v = new Voice;
  Mixer& m = Mixer::Get();
  std::lock_guard lock(m.mutex);
  m.voices.push_back(v);
  return v;
}

void Mix(int16_t* out, size_t frames) { Mixer::Get().MixInto(out, frames); }

void SetGameMuted(bool muted) { Mixer::Get().game_muted = muted; }

bool StartOutput() {
#if defined(__PROSPERO__)
  static std::atomic<int> state{0};  // 0 not yet, 1 running, -1 failed
  int expected = 0;
  if (!state.compare_exchange_strong(expected, 2)) {
    while (state.load() == 2) {
    }
    return state.load() == 1;
  }
  sceAudioOutInit();
  constexpr uint32_t kFrames = 256;
  static int32_t port = sceAudioOutOpen(0xFF, 0, 0, kFrames, kRate, 1 /* s16 stereo */);
  if (port < 0) {
    state = -1;
    return false;
  }
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 1 << 20);  // (the decoders' frames are big)
  pthread_t thread;
  const int rc = pthread_create(
      &thread, &attr,
      [](void*) -> void* {
        int16_t out[kFrames * 2];
        for (;;) {
          Mix(out, kFrames);
          sceAudioOutOutput(port, out);  // (blocks: paces the loop)
        }
        return nullptr;
      },
      nullptr);
  pthread_attr_destroy(&attr);
  state = rc == 0 ? 1 : -1;
  return rc == 0;
#else
  return false;  // (tests call Mix themselves)
#endif
}

}  // namespace skate3ps5::sound
