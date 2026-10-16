// Skate 3 for PS5 - sound of the port's own (the hub's music and menu
// sounds), beside the game's.
//
// Files (.mp3, .wav, .flac, .ogg) are decoded on the fly to 48 kHz stereo
// and mixed into one audio port of their own, beside the game's.
// The decoding and mixing are plain C++ (tests run them on a PC); only the
// output is the PS5's (sceAudioOut).
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace skate3ps5::sound {

constexpr uint32_t kRate = 48000;

// A decoded sound: 48 kHz stereo float frames.
class Source {
 public:
  virtual ~Source() = default;
  // Up to `frames` frames into lr (interleaved); fewer at the end.
  virtual size_t Read(float* lr, size_t frames) = 0;
  // Back to the start (for a loop); false if it can't.
  virtual bool Rewind() = 0;
  // Its length in seconds (0: not known).
  virtual double Seconds() const { return 0; }
};

// The file's sound (by its extension and content); null if it can't be read.
std::unique_ptr<Source> Open(const std::filesystem::path& file);
// A sound in memory (48 kHz stereo frames), shared by every play of it.
std::unique_ptr<Source> Memory(std::shared_ptr<const std::vector<float>> lr);
// Whether Open knows the file's type (by its extension).
bool CanOpen(const std::filesystem::path& file);

// A voice of the mixer: plays one source at a time.
class Voice {
 public:
  // Starts `source` (replacing what was playing); `loop`: from the top again at its end.
  void Play(std::unique_ptr<Source> source, bool loop);
  void Stop();                  // at once (a short fade, no click)
  void FadeOut(float seconds);  // then stops
  void Pause(bool paused);
  void SetVolume(float volume);  // 0..1, eased
  // Playing or paused (not finished, not stopped).
  bool Active() const { return active_.load(std::memory_order_acquire); }
  // Honour the game's audio_mute (the game's sounds; not the hub's).
  bool game_sound = false;

 private:
  friend class Mixer;
  std::atomic<bool> active_{false};
  // (the rest belongs to the mixer, under its lock)
  std::unique_ptr<Source> source_;
  bool loop_ = false, paused_ = false, stopping_ = false;
  float volume_ = 1, gain_ = 0, fade_step_ = 0;
  bool fresh_ = false;  // (its gain starts at the volume: no fade in)
};

// The game's audio_mute (silences the voices marked game_sound).
void SetGameMuted(bool muted);

// A new voice (they live as long as the program).
Voice* NewVoice();

// Mixes every voice's next `frames` frames into out (interleaved stereo
// 16-bit): what the output plays. (Called by the output; tests call it.)
void Mix(int16_t* out, size_t frames);

// Starts the output (the PS5's own audio port, on a thread of its own).
// Idempotent; false if the port couldn't be opened.
bool StartOutput();

}  // namespace skate3ps5::sound
