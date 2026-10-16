// Skate 3 for PS5 - the hub: the screen the app opens on, before the game.
// Home (START GAME), Settings and About, driven by the DualSense.
//
// This part is plain ImGui + the standard library. ps5_hub.cpp puts it on
// the PS5's screen, reads the controller, plays its sounds and starts the
// game on START GAME. (Built on the SmackDown vs. Raw 2011 PS5 hub.)
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"

namespace skate3ps5::hub {

// The controller, as the hub sees it.
enum Button : uint32_t {
  kUp = 1u << 0,
  kDown = 1u << 1,
  kLeft = 1u << 2,
  kRight = 1u << 3,
  kCross = 1u << 4,
  kCircle = 1u << 5,
  kSquare = 1u << 6,
  kTriangle = 1u << 7,
  kL1 = 1u << 8,
  kR1 = 1u << 9,
  kOptions = 1u << 10,
  kR3 = 1u << 11,
};

struct Pad {
  uint32_t held = 0;     // down now
  uint32_t pressed = 0;  // down now, not before (with key repeat for the d-pad)
};

struct Image {
  ImTextureID id = 0;
  float width = 0, height = 0;
  explicit operator bool() const { return id != 0; }
};

struct Assets {
  ImFont* display = nullptr;    // Oswald Bold: titles, the big button
  ImFont* nav = nullptr;        // Oswald SemiBold: the tabs, section names
  ImFont* body = nullptr;       // Inter Regular: text
  ImFont* body_bold = nullptr;  // Inter SemiBold: labels, values
  Image logo;                   // hub/logo.png
  Image hero;                   // hub/hero.jpg: the picture behind it all
  std::map<uint32_t, Image> buttons;  // hub/buttons, by Button (kLeft | kRight: the d-pad)
};

// What the game folder has (Assets/), looked at when the hub opens.
struct GameState {
  bool files = false;         // default.xex and data/
  bool title_update = false;  // Title Update 3 set up (its .xexp patches)
  bool title_update_package = false;  // the TU file is there (set up on START GAME)
  int dlc = 0;                // DLC packages in Assets/dlc
  bool Ready() const { return files && (title_update || title_update_package); }
};

// The sounds the hub asks for (ps5_hub.cpp plays them).
enum class Sound { kMove, kSelect, kBack, kPage, kOn, kOff, kStart, kError };

struct Audio {
  std::function<void(Sound)> sfx;
  // The menu music: R3 plays the next song; its volume (0..100, Settings).
  std::function<void()> next_song;
  std::function<void(int)> music_volume;
};

// The settings live in the game's settings file (cvars): read and written by name.
struct Config {
  std::function<std::string(const std::string&)> get;
  std::function<void(const std::string&, const std::string&)> set;  // (saved at once)
};

// The pages, in the order of the top bar.
enum Tab { kHome, kSettingsTab, kAboutTab, kTabCount };

class Hub {
 public:
  void Init(const Assets& assets, const GameState& game, Config config, std::string version);
  void SetAudio(Audio audio) { audio_ = std::move(audio); }
  // Draws the hub over the whole display; true when the game should start.
  bool Draw(const Pad& pad, float dt);
  // From START GAME until Draw returns true: the hub fades out (and its music).
  bool Leaving() const { return leaving_; }
  float LeaveProgress() const { return leave_; }
  // The menu music's volume, 0..100 (Settings: Menu Music).
  int MusicVolume() const;

  struct Setting;

 private:
  // A row of a list: an item, or a heading over the next ones.
  struct Row {
    int item = -1;  // -1: a heading
    std::string heading;
    float y = 0;
  };

  std::string Value(const Setting& s) const;
  int SettingIndex(const Setting& s) const;
  void Apply(const Setting& s, int index);

  void DrawBackdrop();
  void DrawTopBar();
  void DrawHints();
  void DrawConfirm();
  void DrawHome();
  void DrawSettings();
  void DrawAbout();

  void PageTitle(const char* title, const std::string& subtitle);
  void List(std::vector<Row>& rows, int sel, float rh, const std::function<void(int, float, float)>& draw);
  void Panel(const std::function<void(float x, float y, float w)>& draw);

  void Navigate(const Pad& pad);
  int Items() const;
  void Activate(int dir);
  void GoTo(int tab, float dir);
  void Play(Sound s) {
    if (audio_.sfx) audio_.sfx(s);
  }
  void Toast(std::string text) {
    toast_ = std::move(text);
    toast_time_ = 3;
  }
  void Notice(std::string title, std::string text) {
    confirm_title_ = std::move(title), confirm_text_ = std::move(text);
    confirm_open_ = true;
    confirm_in_ = 0;
  }

  Assets a_;
  GameState game_;
  Config config_;
  Audio audio_;
  std::string version_;
  // layout: virtual 1920 x 1080, scaled to the display
  float s_ = 1, ox_ = 0, oy_ = 0;
  ImDrawList* dl_ = nullptr;
  float time_ = 0, dt_ = 0;

  int tab_ = 0;
  int item_[kTabCount] = {};
  bool launch_ = false;
  std::string toast_;
  float toast_time_ = 0;
  // a message (Cross closes it)
  bool confirm_open_ = false;
  std::string confirm_title_, confirm_text_;
  float confirm_in_ = 0;
  // animation (eased toward their targets)
  float tab_x_ = -1, tab_w_ = 0, focus_y_ = -1, shade_ = 0.3f, page_in_ = 1;
  float page_dir_ = 1;
  float panel_in_ = 1;
  int panel_item_ = -1;
  float value_in_ = 1, value_dir_ = 0;
  float press_ = 0;
  float hero_x_ = -1;
  bool leaving_ = false;
  float leave_ = 0;
  float scroll_f_ = 0, scroll_px_ = 0;
  std::map<int, float> knobs_;

  // helpers (virtual coordinates)
  ImVec2 P(float x, float y) const { return ImVec2(ox_ + x * s_, oy_ + y * s_); }
  float Ease(float& v, float target, float speed = 16);
  void Text(ImFont* f, float size, float x, float y, ImU32 col, const std::string& t, float align = 0,
            float spacing = 0);
  float TextWidth(ImFont* f, float size, const std::string& t, float spacing = 0) const;
  std::string Fit(ImFont* f, float size, const std::string& t, float width) const;
  float Wrapped(ImFont* f, float size, float x, float y, float width, ImU32 col, const std::string& t,
                float line = 1.4f);
  void Rect(float x, float y, float w, float h, ImU32 col, float r = 0);
  void Glyph(uint32_t button, float cx, float cy, ImU32 col);  // a PlayStation button
  float Hint(float right, uint32_t button, const char* label);  // returns its left edge
  void Fade(int first_vertex, float alpha);                     // fades what was drawn since
};

}  // namespace skate3ps5::hub
