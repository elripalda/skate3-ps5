// Skate 3 for PS5 - the hub's menu sounds, made here (no
// sound files needed): short, soft clicks and tones (see hub_sfx.h).
#include "hub_sfx.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace skate3ps5::hub {
namespace {

constexpr float kRate = 48000;
constexpr float kPi = 3.14159265f;

struct Buffer {
  std::vector<float> mono;
  explicit Buffer(float seconds) : mono(size_t(seconds * kRate), 0.0f) {}
  // A tone from `start` s: frequency gliding f0 -> f1, attack, then an exponential decay.
  void Tone(float start, float length, float f0, float f1, float amp, float attack, float decay, float bright = 0.25f) {
    const size_t a = size_t(start * kRate), n = size_t(length * kRate);
    float phase = 0;
    for (size_t i = 0; i < n && a + i < mono.size(); ++i) {
      const float t = float(i) / kRate;
      const float f = f0 + (f1 - f0) * std::min(1.0f, t / length);
      phase += 2 * kPi * f / kRate;
      const float env = std::min(1.0f, t / attack) * std::exp(-t / decay);
      // (a touch of the 2nd and 3rd harmonics: rounder than a bare sine)
      const float s = std::sin(phase) + bright * 0.5f * std::sin(2 * phase) + bright * 0.25f * std::sin(3 * phase);
      mono[a + i] += amp * env * s;
    }
  }
  // Noise through a band (two one-pole filters), shaped by rise / fall.
  void Noise(float start, float length, float low_hz, float high_hz, float amp, float rise, float fall) {
    uint32_t seed = 0x9E3779B9u;
    float lp = 0, hp_in = 0, hp = 0;
    const float a_lp = 1 - std::exp(-2 * kPi * high_hz / kRate), a_hp = std::exp(-2 * kPi * low_hz / kRate);
    const size_t a = size_t(start * kRate), n = size_t(length * kRate);
    for (size_t i = 0; i < n && a + i < mono.size(); ++i) {
      seed = seed * 1664525u + 1013904223u;
      const float w = float(int32_t(seed)) / 2147483648.0f;
      lp += a_lp * (w - lp);
      hp = a_hp * (hp + lp - hp_in);
      hp_in = lp;
      const float t = float(i) / kRate;
      const float env = t < rise ? t / rise : std::exp(-(t - rise) / fall);
      mono[a + i] += amp * env * hp;
    }
  }
  // A little room: two short echoes, quiet.
  void Room() {
    const size_t d1 = size_t(0.031f * kRate), d2 = size_t(0.047f * kRate);
    std::vector<float> dry = mono;
    for (size_t i = 0; i < mono.size(); ++i) {
      float wet = 0;
      if (i >= d1) wet += 0.16f * mono[i - d1];
      if (i >= d2) wet += 0.10f * mono[i - d2];
      mono[i] = dry[i] + wet;
    }
  }
  std::vector<float> Stereo() const {
    // (the end faded over 5 ms: no click)
    std::vector<float> lr(mono.size() * 2);
    const size_t tail = size_t(0.005f * kRate);
    for (size_t i = 0; i < mono.size(); ++i) {
      float s = mono[i] * 1.6f;  // (made quiet above, brought to sit just under the music)
      if (i + tail > mono.size()) s *= float(mono.size() - i) / float(tail);
      lr[i * 2] = lr[i * 2 + 1] = s;
    }
    return lr;
  }
};

}  // namespace

std::vector<float> SynthSound(Sound sound) {
  switch (sound) {
    case Sound::kMove: {  // a soft tick
      Buffer b(0.06f);
      b.Tone(0, 0.05f, 2600, 2400, 0.10f, 0.0008f, 0.006f, 0.1f);
      b.Tone(0, 0.05f, 1300, 1250, 0.06f, 0.0008f, 0.012f, 0.1f);
      return b.Stereo();
    }
    case Sound::kSelect: {  // up a fifth
      Buffer b(0.22f);
      b.Tone(0.000f, 0.10f, 880, 880, 0.14f, 0.002f, 0.035f);
      b.Tone(0.045f, 0.16f, 1320, 1320, 0.14f, 0.002f, 0.05f);
      b.Room();
      return b.Stereo();
    }
    case Sound::kBack: {  // down a fifth, softer
      Buffer b(0.2f);
      b.Tone(0.000f, 0.10f, 1320, 1320, 0.10f, 0.002f, 0.03f);
      b.Tone(0.040f, 0.14f, 880, 860, 0.11f, 0.002f, 0.045f);
      b.Room();
      return b.Stereo();
    }
    case Sound::kPage: {  // a short swish and a tick
      Buffer b(0.22f);
      b.Noise(0, 0.2f, 1800, 7000, 0.20f, 0.035f, 0.05f);
      b.Tone(0.03f, 0.05f, 2000, 1900, 0.05f, 0.001f, 0.01f, 0.1f);
      return b.Stereo();
    }
    case Sound::kOn: {  // a click and a rising chirp
      Buffer b(0.16f);
      b.Noise(0, 0.01f, 2000, 9000, 0.12f, 0.0005f, 0.002f);
      b.Tone(0.004f, 0.09f, 700, 1150, 0.13f, 0.002f, 0.03f);
      b.Room();
      return b.Stereo();
    }
    case Sound::kOff: {  // a click and a falling chirp
      Buffer b(0.16f);
      b.Noise(0, 0.01f, 2000, 9000, 0.12f, 0.0005f, 0.002f);
      b.Tone(0.004f, 0.09f, 1000, 620, 0.12f, 0.002f, 0.03f);
      b.Room();
      return b.Stereo();
    }
    case Sound::kStart: {  // a deep hit and a rising chord
      Buffer b(1.6f);
      b.Tone(0, 0.6f, 90, 42, 0.38f, 0.003f, 0.16f, 0.15f);       // the thump
      b.Noise(0, 0.12f, 300, 5000, 0.18f, 0.002f, 0.03f);         // its crack
      const float chord[] = {440.0f, 554.37f, 659.25f, 880.0f};   // A major
      for (int k = 0; k < 4; ++k) b.Tone(0.05f + k * 0.045f, 1.3f, chord[k], chord[k], 0.07f, 0.01f, 0.45f, 0.35f);
      b.Noise(0.05f, 0.9f, 4000, 12000, 0.04f, 0.3f, 0.25f);      // a shimmer
      b.Room();
      return b.Stereo();
    }
    case Sound::kError: {  // two low buzzes
      Buffer b(0.22f);
      b.Tone(0.00f, 0.07f, 196, 196, 0.12f, 0.002f, 0.05f, 0.9f);
      b.Tone(0.09f, 0.09f, 185, 180, 0.12f, 0.002f, 0.06f, 0.9f);
      return b.Stereo();
    }
  }
  return {};
}

const char* SoundName(Sound sound) {
  switch (sound) {
    case Sound::kMove: return "move";
    case Sound::kSelect: return "select";
    case Sound::kBack: return "back";
    case Sound::kPage: return "page";
    case Sound::kOn: return "on";
    case Sound::kOff: return "off";
    case Sound::kStart: return "start";
    case Sound::kError: return "error";
  }
  return "";
}

}  // namespace skate3ps5::hub
