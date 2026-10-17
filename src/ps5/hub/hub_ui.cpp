// Skate 3 for PS5 - the hub (see hub_ui.h): the frame, the Home, Settings and
// About pages, and the controls.
#include "hub_ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "hub_style.h"

namespace skate3ps5::hub {

static const char* const kTabNames[kTabCount] = {"HOME", "SETTINGS", "ABOUT"};

// ----------------------------------------------------------------- settings

struct Hub::Setting {
  const char* section;
  std::vector<const char*> keys;  // the cvars it sets (all to the same value)
  const char* label;
  const char* help;
  std::vector<const char*> values;  // as the cvar takes them
  std::vector<const char*> labels;
  const char* fallback;  // when the cvar's value isn't one of the values
};

static const std::vector<Hub::Setting>& Settings() {
  static const std::vector<Hub::Setting> rows = {
      {"DISPLAY", {"skate3_native_render_scene"}, "Renderer",
       "Native draws the game directly on the PS5's graphics chip, with all the enhancements below; it's much "
       "faster. Emulated replays the Xbox 360's graphics exactly, as a fallback if something looks wrong.",
       {"true", "false"}, {"Native", "Emulated"}, "true"},
      {"DISPLAY", {"resolution_scale", "draw_resolution_scale_x", "draw_resolution_scale_y"}, "Resolution",
       "How sharp the game is drawn. 720p is how the Xbox 360 drew it; 1440p and 4K draw every frame at two and "
       "three times that, for a far cleaner picture. The picture always goes to the TV in 4K. 4K asks the most of "
       "the PS5: if the game can't keep up, step down to 1440p.",
       {"1", "2", "3"}, {"720p (Original)", "1440p", "4K"}, "2"},
      {"DISPLAY", {"skate3_guest_fps_cap"}, "Frame Rate",
       "How many pictures a second the game draws. The Xbox 360 game ran at 30; this version runs at 60. 30 leaves "
       "the PS5 twice the time for each picture, for the highest settings.",
       {"60", "30"}, {"60 FPS", "30 FPS"}, "60"},
      {"DISPLAY", {"skate3_native_render_scene_msaa"}, "Anti-Aliasing",
       "Smooths jagged edges, and the shimmer of thin things far away (railings, wires, fences). Each step up costs "
       "more. Native renderer only.",
       {"1", "2", "4", "8"}, {"Off", "2x", "4x", "8x"}, "4"},
      {"DISPLAY", {"show_fps_counter"}, "FPS Counter", "Shows the frame rate in the corner of the screen.",
       {"false", "true"}, {"Off", "On"}, "false"},
      {"GRAPHICS", {"skate3_native_render_scene_shadows"}, "Shadows",
       "Shadows of the skaters, people and props, as the game draws them.", {"true", "false"}, {"On", "Off"},
       "true"},
      {"GRAPHICS", {"skate3_native_render_scene_shadow_tile"}, "Shadow Quality",
       "How sharp the shadows of the skaters and props are. Auto follows the resolution; 512 is the original "
       "game's.",
       {"0", "512", "1024", "1536", "2048"}, {"Auto", "512 (Original)", "1024", "1536", "2048"}, "0"},
      {"GRAPHICS", {"skate3_native_render_scene_shadow_static_casters"}, "Enhanced Shadows",
       "Real-time sun shadows from buildings, trees and props, which the original game only painted into the "
       "world. They also shade the sun shafts.",
       {"true", "false"}, {"On", "Off"}, "true"},
      {"GRAPHICS", {"skate3_native_render_scene_shadow_static_size"}, "Enhanced Shadow Detail",
       "How sharp the enhanced shadows are far away. Higher uses more memory (about 50 MB at 2048, 200 MB at 4096, "
       "800 MB at 8192).",
       {"2048", "4096", "8192"}, {"2048", "4096", "8192"}, "4096"},
      {"GRAPHICS", {"skate3_native_render_scene_shadow_pcss"}, "Soft Shadows",
       "Shadows sharp where they meet what casts them and softer further away, as the sun makes them (PCSS). The "
       "most demanding of the shadow settings at high resolutions.",
       {"true", "false"}, {"On", "Off"}, "true"},
      {"GRAPHICS", {"skate3_native_render_scene_ssao"}, "Ambient Occlusion",
       "Soft shading where surfaces meet: under ledges, rails, cars and the skater (GTAO).", {"true", "false"},
       {"On", "Off"}, "true"},
      {"GRAPHICS", {"skate3_native_render_scene_bloom"}, "Bloom",
       "A glow around bright lights (lamps, neon, the sun), from how bright they really are. Most visible at "
       "night.",
       {"true", "false"}, {"On", "Off"}, "true"},
      {"GRAPHICS", {"skate3_native_render_scene_shafts", "skate3_native_render_scene_haze"}, "Volumetric Lighting",
       "Shafts of sunlight through buildings and trees, and haze in the air, both following the time of day.",
       {"true", "false"}, {"On", "Off"}, "true"},
      {"GRAPHICS", {"skate3_draw_distance_scale", "skate3_lod_distance_scale"}, "Draw Distance",
       "How far away small objects, plants and the skaters' detail stay in view, as a multiple of the original "
       "game's distances. Higher draws more of Port Carverton, and costs some speed.",
       {"1.0", "2.0", "3.0"}, {"Original", "2x", "3x"}, "2.0"},
      {"GRAPHICS", {"skate3_draw_distance_stream_probe"}, "World Streaming",
       "Loads the parts of the city around you before they come into view, so buildings and trees appear far away "
       "instead of close by. Uses a little more memory. Original is how the game did it.",
       {"0", "100", "200", "300"}, {"Original", "Near (100 m)", "Extended (200 m)", "Far (300 m)"}, "0"},
      {"GRAPHICS", {"skate3_field_of_view"}, "Field of View",
       "How wide the camera sees, in degrees. The game's own is 60.",
       {"60.0", "65.0", "70.0", "75.0", "80.0", "85.0", "90.0"}, {"60", "65", "70", "75", "80", "85", "90"},
       "60.0"},
      {"GAME", {"skate3_button_prompts"}, "Button Prompts",
       "The buttons the game shows in its prompts and tutorials: PlayStation's, or the Xbox 360's it was made "
       "with.",
       {"playstation", "xbox"}, {"PlayStation", "Xbox"}, "playstation"},
      {"GAME", {"user_language"}, "Language", "The language of the game's text and menus.",
       {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"},
       {"English", "Japanese", "German", "French", "Spanish", "Italian", "Korean", "Traditional Chinese",
        "Portuguese", "Simplified Chinese", "Polish", "Russian"},
       "1"},
      {"GAME", {"skate3_intro_movie_skip"}, "Skip Intro Movie",
       "Lets Cross or OPTIONS skip the movie the game opens with.", {"true", "false"}, {"On", "Off"}, "true"},
      {"HUB", {"hub_music_volume"}, "Menu Music",
       "The volume of the music here (the songs in the hub's music folder). R3 plays the next song.",
       {"0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100"},
       {"Off", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"}, "80"},
      {"HUB", {}, "Reset to Defaults",
       "Every setting goes back to how the port comes: the native renderer at 1440p and 60 FPS with all its "
       "graphics options, PlayStation buttons, English, the menu music at 80%. Your saves aren't touched.",
       {}, {}, ""},
  };
  return rows;
}

// A cvar's text as the settings compare it: booleans and numbers normalised.
static std::string Normal(std::string v) {
  if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()) v = v.substr(1, v.size() - 2);
  char* end = nullptr;
  const double d = std::strtod(v.c_str(), &end);
  if (!v.empty() && end && *end == 0) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", d);
    return buf;
  }
  return v;
}

// --------------------------------------------------------------------- data

void Hub::Init(const Assets& assets, const GameState& game, Config config, std::string version) {
  a_ = assets;
  game_ = game;
  config_ = std::move(config);
  version_ = std::move(version);
}

std::string Hub::Value(const Setting& s) const {
  if (s.keys.empty()) return std::string();
  return config_.get ? config_.get(s.keys[0]) : std::string(s.fallback);
}

int Hub::SettingIndex(const Setting& r) const {
  if (r.values.empty()) return 0;
  const std::string v = Normal(Value(r));
  for (size_t k = 0; k < r.values.size(); ++k)
    if (v == Normal(r.values[k])) return int(k);
  for (size_t k = 0; k < r.values.size(); ++k)
    if (std::string(r.fallback) == r.values[k]) return int(k);
  return 0;
}

void Hub::Apply(const Setting& s, int index) {
  if (!config_.set) return;
  for (const char* key : s.keys) config_.set(key, s.values[size_t(index)]);
}

int Hub::MusicVolume() const {
  const std::string v = config_.get ? config_.get("hub_music_volume") : std::string("80");
  return std::clamp(std::atoi(v.c_str()), 0, 100);
}

// ------------------------------------------------------------------ drawing

float Hub::Ease(float& v, float target, float speed) {
  v = v < -0.5f ? target : v + (target - v) * (1.0f - std::exp(-dt_ * speed));
  return v;
}

void Hub::Fade(int first_vertex, float alpha) {
  if (alpha >= 0.999f) return;
  for (int i = first_vertex; i < dl_->VtxBuffer.Size; ++i) {
    ImU32& c = dl_->VtxBuffer[i].col;
    c = WithAlpha(c, alpha);
  }
}

void Hub::Text(ImFont* f, float size, float x, float y, ImU32 col, const std::string& t, float align, float spacing) {
  if (!f) f = ImGui::GetFont();
  float px = x - TextWidth(f, size, t, spacing) * align;
  if (spacing <= 0) {
    dl_->AddText(f, size * s_, P(px, y), col, t.c_str());
    return;
  }
  // (letter by letter: a UTF-8 letter's bytes stay together)
  for (size_t i = 0; i < t.size();) {
    size_t n = 1;
    const unsigned char c = static_cast<unsigned char>(t[i]);
    if (c >= 0xF0) n = 4;
    else if (c >= 0xE0) n = 3;
    else if (c >= 0xC0) n = 2;
    const char* a = t.c_str() + i;
    const char* b = t.c_str() + std::min(t.size(), i + n);
    dl_->AddText(f, size * s_, P(px, y), col, a, b);
    px += f->CalcTextSizeA(size, FLT_MAX, 0, a, b).x + spacing;
    i += n;
  }
}

float Hub::TextWidth(ImFont* f, float size, const std::string& t, float spacing) const {
  if (!f) f = ImGui::GetFont();
  if (spacing <= 0) return f->CalcTextSizeA(size, FLT_MAX, 0, t.c_str()).x;
  float w = 0;
  for (size_t i = 0; i < t.size();) {
    size_t n = 1;
    const unsigned char c = static_cast<unsigned char>(t[i]);
    if (c >= 0xF0) n = 4;
    else if (c >= 0xE0) n = 3;
    else if (c >= 0xC0) n = 2;
    w += f->CalcTextSizeA(size, FLT_MAX, 0, t.c_str() + i, t.c_str() + std::min(t.size(), i + n)).x + spacing;
    i += n;
  }
  return t.empty() ? 0 : w - spacing;
}

std::string Hub::Fit(ImFont* f, float size, const std::string& t, float width) const {
  if (TextWidth(f, size, t) <= width) return t;
  std::string s = t;
  while (!s.empty() && TextWidth(f, size, s + "...") > width) {
    s.pop_back();
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
  }
  return s + "...";
}

// Text broken into lines of at most `width`; returns the height used.
float Hub::Wrapped(ImFont* f, float size, float x, float y, float width, ImU32 col, const std::string& t, float line) {
  std::istringstream words(t);
  std::string word, cur;
  float yy = y;
  while (words >> word) {
    const std::string next = cur.empty() ? word : cur + " " + word;
    if (!cur.empty() && TextWidth(f, size, next) > width) {
      Text(f, size, x, yy, col, cur);
      yy += size * line;
      cur = word;
    } else {
      cur = next;
    }
  }
  if (!cur.empty()) Text(f, size, x, yy, col, Fit(f, size, cur, width)), yy += size * line;
  return yy - y;
}

void Hub::Rect(float x, float y, float w, float h, ImU32 col, float r) {
  dl_->AddRectFilled(P(x, y), P(x + w, y + h), col, r * s_);
}

// The DualSense's buttons: the pictures in hub/buttons, else drawn in one colour.
void Hub::Glyph(uint32_t button, float cx, float cy, ImU32 col) {
  if (auto it = a_.buttons.find(button); it != a_.buttons.end() && it->second) {
    const float h = 24;  // (the pictures' buttons fill about two thirds of them)
    dl_->AddImage(it->second.id, P(cx - h, cy - h), P(cx + h, cy + h), ImVec2(0, 0), ImVec2(1, 1), col);
    return;
  }
  const ImVec2 c = P(cx, cy);
  const float R = 15 * s_, k = 6.2f * s_, t = 2.2f * s_;
  switch (button) {
    case kCross:
    case kCircle:
    case kSquare:
    case kTriangle:
      dl_->AddCircle(c, R, col, 40, 1.6f * s_);
      if (button == kCross) {
        dl_->AddLine(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), col, t);
        dl_->AddLine(ImVec2(c.x - k, c.y + k), ImVec2(c.x + k, c.y - k), col, t);
      } else if (button == kCircle) {
        dl_->AddCircle(c, k * 1.1f, col, 28, t);
      } else if (button == kSquare) {
        dl_->AddRect(ImVec2(c.x - k * 0.95f, c.y - k * 0.95f), ImVec2(c.x + k * 0.95f, c.y + k * 0.95f), col, 0, 0, t);
      } else {
        dl_->AddTriangle(ImVec2(c.x, c.y - k * 1.15f), ImVec2(c.x + k * 1.1f, c.y + k * 0.8f),
                         ImVec2(c.x - k * 1.1f, c.y + k * 0.8f), col, t);
      }
      break;
    case kLeft | kRight: {
      const float a = 7 * s_, gap = 5 * s_;
      dl_->AddTriangleFilled(ImVec2(c.x - gap, c.y - a), ImVec2(c.x - gap, c.y + a), ImVec2(c.x - gap - a * 1.2f, c.y), col);
      dl_->AddTriangleFilled(ImVec2(c.x + gap, c.y - a), ImVec2(c.x + gap, c.y + a), ImVec2(c.x + gap + a * 1.2f, c.y), col);
      break;
    }
    case kOptions:
      dl_->AddRect(ImVec2(c.x - 13 * s_, c.y - 11 * s_), ImVec2(c.x + 13 * s_, c.y + 11 * s_), col, 6 * s_, 0, 1.6f * s_);
      for (int i = -1; i <= 1; ++i)
        dl_->AddLine(ImVec2(c.x - 6 * s_, c.y + i * 4.5f * s_), ImVec2(c.x + 6 * s_, c.y + i * 4.5f * s_), col, 1.8f * s_);
      break;
    case kL1:
    case kR1:
      dl_->AddRect(ImVec2(c.x - 20 * s_, c.y - 13 * s_), ImVec2(c.x + 20 * s_, c.y + 13 * s_), col, 7 * s_, 0, 1.6f * s_);
      Text(a_.body_bold, 15, cx, cy - 9.5f, col, button == kL1 ? "L1" : "R1", 0.5f);
      break;
  }
}

// One hint at the bottom, right-aligned at `right`: the button, then what it does.
float Hub::Hint(float right, uint32_t button, const char* label) {
  const float tw = TextWidth(a_.body_bold, 20, label);
  const float gw = (button == kL1 || button == kR1) ? 40 : 30;
  const float x = right - tw;
  Text(a_.body_bold, 20, x, 1010, White(0.78f), label);
  Glyph(button, x - 12 - gw * 0.5f, 1023, White(0.9f));
  return x - 12 - gw - 40;
}

void Hub::PageTitle(const char* title, const std::string& subtitle) {
  Text(a_.display, 60, 112, 132, kText, title, 0, 1);
  Text(a_.body, 22, 114, 214, White(0.6f), subtitle);
}

void Hub::List(std::vector<Row>& rows, int sel, float rh, const std::function<void(int, float, float)>& draw) {
  const float hh = 50, view = kListView, x = kListX, w = kListW, y0 = kListY;
  float yy = 0;
  int sel_row = -1;
  for (size_t k = 0; k < rows.size(); ++k) {
    Row& r = rows[k];
    if (r.item < 0) {
      yy += k ? 14 : 0;
      r.y = yy;
      yy += hh;
    } else {
      r.y = yy;
      if (r.item == sel) sel_row = int(k);
      yy += rh;
    }
  }
  const float total = yy;
  if (sel_row < 0) return;
  // keep the focused row in view (with its heading, when it's the first under it)
  {
    float top = rows[size_t(sel_row)].y, bottom = top + rh;
    if (sel_row > 0 && rows[size_t(sel_row) - 1].item < 0) top = rows[size_t(sel_row) - 1].y;
    float target = scroll_px_;
    if (top < target) target = top;
    if (bottom > target + view) {
      target = bottom - view;
      for (const Row& r : rows)
        if (r.y >= target - 0.5f) {
          target = r.y;
          break;
        }
    }
    scroll_px_ = std::clamp(target, 0.0f, std::max(0.0f, total - view));
  }
  Ease(scroll_f_, scroll_px_, 18);
  Ease(focus_y_, rows[size_t(sel_row)].y, 20);
  dl_->PushClipRect(P(0, y0 - 2), P(kW, y0 + view), true);
  const float push = press_ * 4;
  Rect(x + push, y0 + focus_y_ - scroll_f_ + push * 0.5f, w - push * 2, rh - 6 - push, kText, 10);
  for (size_t k = 0; k < rows.size(); ++k) {
    const Row& row = rows[k];
    const float ry = y0 + row.y - scroll_f_;
    if (ry < y0 - rh - 20 || ry > y0 + view + 20) continue;
    if (row.item < 0) {
      Text(a_.body_bold, 15, x + 24, ry + 20, White(0.45f), row.heading, 0, 3);
      continue;
    }
    const float over = std::clamp(1 - std::fabs(focus_y_ - row.y) / (rh * 0.7f), 0.0f, 1.0f);
    if (k + 1 < rows.size() && rows[k + 1].item >= 0)
      Rect(x + 24, ry + rh - 3, w - 48, 1, White(0.06f * (1 - over)));
    draw(row.item, ry, over);
  }
  dl_->PopClipRect();
  if (total > view) {  // where the list is
    const float bh = view * view / total;
    const float by = y0 + (view - bh) * scroll_f_ / (total - view);
    Rect(x + w + 16, y0, 3, view - 6, White(0.08f), 1.5f);
    Rect(x + w + 16, by, 3, bh - 6, White(0.5f), 1.5f);
  }
}

void Hub::Panel(const std::function<void(float, float, float)>& draw) {
  const int v0 = dl_->VtxBuffer.Size;
  draw(kPanelX + (1 - Smooth(panel_in_)) * 16, kPanelY, kPanelW);
  Fade(v0, Smooth(panel_in_));
}

void Hub::DrawBackdrop() {
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  dl_->AddRectFilled(ImVec2(0, 0), size, Rgba(8, 9, 11));
  if (a_.hero) {
    // The picture covers the screen, slowly zooming in and out (a 50 s
    // breath) and drifting sideways with the pages.
    const float breath = 0.5f - 0.5f * std::cos(time_ * 6.2832f / 50.0f);
    const float zoom = 1.04f + 0.06f * breath;
    Ease(hero_x_, (float(tab_) / float(kTabCount - 1) - 0.5f) * 0.024f, 3);
    const float sa = size.x / size.y, ia = a_.hero.width / a_.hero.height;
    float uw = 1, vh = 1;
    if (ia > sa) uw = sa / ia;
    else vh = ia / sa;
    uw /= zoom, vh /= zoom;
    const float room = (1 - uw) * 0.5f;
    const float cx = 0.5f + std::clamp(hero_x_ + 0.004f * std::sin(time_ * 0.07f), -room, room);
    const ImVec2 uv0(cx - uw * 0.5f, 0.42f - vh * 0.42f), uv1(cx + uw * 0.5f, 0.42f + vh * 0.58f);
    dl_->AddImage(a_.hero.id, ImVec2(0, 0), size, uv0, uv1);
  }
  // The pages over it: the picture steps back
  Ease(shade_, tab_ == kHome ? 0.0f : 0.84f, 7);
  if (shade_ > 0.003f) dl_->AddRectFilled(ImVec2(0, 0), size, Black(shade_));
  if (shade_ < 0.6f) {
    const float k = 1 - shade_ / 0.6f;
    dl_->AddRectFilledMultiColor(ImVec2(0, P(0, 330).y), ImVec2(P(1200, 0).x, size.y), Black(0), Black(0), Black(0),
                                 Black(0.7f * k));
  }
  dl_->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(size.x, P(0, 230).y), Black(0.72f), Black(0.72f), Black(0), Black(0));
  const float fade_from = 430 + 330 * std::clamp(shade_ / 0.84f, 0.0f, 1.0f);
  dl_->AddRectFilledMultiColor(ImVec2(0, P(0, fade_from).y), ImVec2(size.x, size.y), Black(0), Black(0), Black(0.94f),
                               Black(0.94f));
}

void Hub::DrawTopBar() {
  constexpr float kSize = 26, kSpacing = 2.2f, kGap = 40;
  float x = 112;
  Glyph(kL1, x + 20, 70, White(0.55f));
  x += 70;
  float sel_x = 0, sel_w = 0;
  for (int i = 0; i < kTabCount; ++i) {
    const bool sel = i == tab_;
    const float w = TextWidth(a_.nav, kSize, kTabNames[i], kSpacing);
    Text(a_.nav, kSize, x, 52, sel ? kText : White(0.5f), kTabNames[i], 0, kSpacing);
    if (sel) sel_x = x, sel_w = w;
    x += w + kGap;
  }
  Glyph(kR1, x - kGap + 34, 70, White(0.55f));
  // the underline glides to the page, stretching on the way
  const float before = tab_x_;
  Ease(tab_x_, sel_x, 16);
  Ease(tab_w_, sel_w, 22);
  const float travel = std::min(60.0f, std::fabs(sel_x - before) * 0.25f);
  Rect(tab_x_ - travel * 0.5f, 89, tab_w_ + travel, 4, kAccent, 2);
}

void Hub::DrawHints() {
  float r = 1808;
  if (confirm_open_) {
    Hint(r, kCross, "OK");
  } else {
    if (tab_ != kHome && game_.Ready()) r = Hint(r, kOptions, "Start Game");
    if (tab_ != kHome) r = Hint(r, kCircle, "Home");
    switch (tab_) {
      case kHome: r = Hint(r, kCross, "Start Game"); break;
      case kSettingsTab: {
        const auto& set = Settings();
        const int sel = std::clamp(item_[tab_], 0, int(set.size()) - 1);
        r = set[size_t(sel)].values.empty() ? Hint(r, kCross, "Reset") : Hint(r, kLeft | kRight, "Change");
        break;
      }
    }
  }
  if (toast_time_ > 0) {
    const float a = Smooth(std::min(toast_time_ * 3, (3.0f - toast_time_) * 6));
    const float w = TextWidth(a_.body_bold, 20, toast_) + 44;
    const float y = 1002 + (1 - a) * 14;
    Rect(112, y, w, 42, White(0.14f * a), 21);
    dl_->AddCircleFilled(P(132, y + 21), 4.5f * s_, WithAlpha(kAccent, a), 16);
    Text(a_.body_bold, 20, 146, y + 8, White(0.92f * a), toast_);
  } else {
    Text(a_.body, 18, 112, 1012, White(0.32f), "Skate 3 " + version_ + "  \xC2\xB7  PlayStation 5");
  }
}

// A message over everything, rising in (Cross closes it).
void Hub::DrawConfirm() {
  Ease(confirm_in_, confirm_open_ ? 1.0f : 0.0f, 14);
  if (confirm_in_ < 0.01f) return;
  const float a = Smooth(confirm_in_);
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  dl_->AddRectFilled(ImVec2(0, 0), size, Black(0.6f * a));
  const float w = 760, x = (kW - w) * 0.5f;
  const int v0 = dl_->VtxBuffer.Size;
  const float y = 360 + (1 - a) * 30;
  int lines = 0;
  {
    std::istringstream words(confirm_text_);
    std::string word, cur;
    while (words >> word) {
      const std::string next = cur.empty() ? word : cur + " " + word;
      if (!cur.empty() && TextWidth(a_.body, 22, next) > w - 96) ++lines, cur = word;
      else cur = next;
    }
    if (!cur.empty()) ++lines;
  }
  const float h = 120 + lines * 22 * 1.4f + 48;
  Rect(x, y, w, h, Rgba(24, 25, 29), 16);
  dl_->AddRectFilled(P(x, y), P(x + 6, y + h), kAccent, 16 * s_, ImDrawFlags_RoundCornersLeft);
  Text(a_.display, 40, x + 48, y + 44, kText, confirm_title_);
  Wrapped(a_.body, 22, x + 48, y + 120, w - 96, White(0.72f), confirm_text_);
  Fade(v0, a);
}

// ------------------------------------------------------------------- pages

void Hub::DrawHome() {
  // first open: the logo rises in, then the button
  const float in_logo = Smooth((time_ - 0.15f) / 0.8f), in_btn = Smooth((time_ - 0.45f) / 0.6f);
  const float lx = 104, lw = a_.logo && a_.logo.width > a_.logo.height * 2.4f ? 700.0f : 640.0f;
  int v0 = dl_->VtxBuffer.Size;
  if (a_.logo) {
    const float lh = std::min(lw * a_.logo.height / a_.logo.width, 520.0f);
    const float lww = lh * a_.logo.width / a_.logo.height;
    const float ly = 862 - 36 - lh + (1 - in_logo) * 40;
    for (int i = 0; i < 8; ++i) {  // a soft shadow under it
      const float an = float(i) * 0.785f, d = 10;
      const float dx = std::cos(an) * d, dy = std::sin(an) * d + 8;
      dl_->AddImage(a_.logo.id, P(lx + dx, ly + dy), P(lx + lww + dx, ly + lh + dy), ImVec2(0, 0), ImVec2(1, 1),
                    Black(0.1f));
    }
    dl_->AddImage(a_.logo.id, P(lx, ly), P(lx + lww, ly + lh));
  } else {
    Text(a_.display, 120, lx, 640, kText, "SKATE 3");
  }
  Fade(v0, in_logo);

  // START GAME: breathes softly; pressed, it gives a little and flashes
  v0 = dl_->VtxBuffer.Size;
  const float push = press_ * 6;
  const float bx = 112 + push, by = 862 + push + (1 - in_btn) * 24, bw = 420 - push * 2, bh = 84 - push * 2;
  const float pulse = 0.5f + 0.5f * std::sin(time_ * 2.4f);
  const float ring = 5 + 3 * pulse;
  const bool ready = game_.Ready();
  dl_->AddRect(P(bx - ring, by - ring), P(bx + bw + ring, by + bh + ring), White((0.14f + 0.2f * pulse) * (ready ? 1 : 0.4f)),
               (12 + ring) * s_, 0, 2 * s_);
  Rect(bx, by, bw, bh, ready ? Mix(kText, kAccent, press_ * 0.9f) : White(0.35f), 12);
  const ImU32 ink = Mix(kInk, kText, press_);
  const float tx = bx + 40;
  dl_->AddTriangleFilled(P(tx, by + bh * 0.5f - 15), P(tx + 26, by + bh * 0.5f), P(tx, by + bh * 0.5f + 15), ink);
  Text(a_.display, 40, tx + 48, by + bh * 0.5f - 23, ink, "START GAME", 0, 2);
  Fade(v0, in_btn);

  // what the game folder has, at the right
  v0 = dl_->VtxBuffer.Size;
  struct Check {
    bool ok;
    const char* yes;
    const char* no;
  };
  const Check checks[] = {
      {game_.files, "Game files", "Game files missing"},
      {game_.title_update || game_.title_update_package,
       game_.title_update ? "Title Update 3" : "Title Update 3 (set up on START GAME)", "Title Update 3 missing"},
  };
  float cy = 870;
  Text(a_.body_bold, 15, 1460, cy - 44, White(0.45f), "YOUR GAME", 0, 3);
  for (const Check& c : checks) {
    if (c.ok) dl_->AddCircleFilled(P(1460, cy + 14), 6 * s_, kAccent, 20);
    else dl_->AddCircle(P(1460, cy + 14), 6 * s_, White(0.5f), 20, 2 * s_);
    Text(a_.body_bold, 21, 1480, cy, c.ok ? White(0.8f) : White(0.55f), c.ok ? c.yes : c.no);
    cy += 38;
  }
  const std::string dlc = game_.dlc ? std::to_string(game_.dlc) + (game_.dlc == 1 ? " DLC pack" : " DLC packs")
                                    : std::string("No DLC");
  Text(a_.body, 21, 1480, cy, White(0.5f), dlc);
  Fade(v0, in_btn);
}

void Hub::DrawSettings() {
  PageTitle("SETTINGS", "Saved as you change them. Most can be changed in the game too.");
  const auto& set = Settings();
  const int sel = std::clamp(item_[tab_], 0, int(set.size()) - 1);
  std::vector<Row> rows;
  for (int i = 0; i < int(set.size()); ++i) {
    if (i == 0 || std::strcmp(set[size_t(i)].section, set[size_t(i) - 1].section))
      rows.push_back({-1, set[size_t(i)].section});
    rows.push_back({i});
  }
  const float x = kListX, w = kListW;
  List(rows, sel, 54, [&](int i, float yy, float over) {
    const auto& r = set[size_t(i)];
    Text(a_.body_bold, 23, x + 24, yy + 11, Mix(White(0.92f), kInk, over), r.label);
    if (r.values.empty()) {  // an action (Reset to Defaults): Cross does it
      if (i == sel) Glyph(kCross, x + w - 46, yy + 24, Mix(White(0.9f), kInk, over));
      return;
    }
    const std::string v = r.labels[size_t(SettingIndex(r))];
    if (std::strcmp(r.keys[0], "hub_music_volume") == 0) {
      // a slider: the bar fills to the volume, the value beside it
      const float frac = float(SettingIndex(r)) / float(r.values.size() - 1);
      auto& shown = knobs_[kSettingsTab << 16 | (0x8000 + i)];
      Ease(shown, frac, 16);
      const float bw = 260, br = x + w - 24 - 74, bx = br - bw, cy = yy + 24;
      const ImU32 track = Mix(White(0.16f), Black(0.14f), over);
      Rect(bx, cy - 3, bw, 6, track, 3);
      Rect(bx, cy - 3, std::max(6.0f, bw * shown), 6, kAccent, 3);
      const float kx = bx + bw * shown;
      dl_->AddCircleFilled(P(kx, cy), (i == sel ? 11 : 8) * s_, i == sel ? Mix(White(1), kInk, over) : White(0.9f), 24);
      if (i == sel) dl_->AddCircle(P(kx, cy), 11 * s_, kAccent, 24, 2.5f * s_);
      Text(i == sel ? a_.body_bold : a_.body, 23, x + w - 24, yy + 11, Mix(White(i == sel ? 1.0f : 0.6f), kInk, over), v,
           1);
      return;
    }
    if (i == sel) {
      // the value slides in from the side it was changed toward
      const float vr = x + w - 46, cy = yy + 24;
      const float slide = (1 - Smooth(value_in_)) * 18 * value_dir_;
      const int v0 = dl_->VtxBuffer.Size;
      Text(a_.body_bold, 23, vr + slide, yy + 11, kInk, v, 1);
      Fade(v0, 0.25f + 0.75f * Smooth(value_in_));
      const float vl = vr - TextWidth(a_.body_bold, 23, v) - 18;
      const float nudge_l = value_dir_ < 0 ? (1 - Smooth(value_in_)) * 6 : 0;
      const float nudge_r = value_dir_ > 0 ? (1 - Smooth(value_in_)) * 6 : 0;
      dl_->AddTriangleFilled(P(vl - nudge_l, cy - 7), P(vl - nudge_l, cy + 7), P(vl - 9 - nudge_l, cy), Black(0.75f * over));
      dl_->AddTriangleFilled(P(vr + 14 + nudge_r, cy - 7), P(vr + 14 + nudge_r, cy + 7), P(vr + 23 + nudge_r, cy),
                             Black(0.75f * over));
    } else {
      Text(a_.body, 23, x + w - 24, yy + 11, Mix(White(0.6f), kInk, over), v, 1);
    }
  });
  // the focused setting, explained
  const auto& r = set[size_t(sel)];
  Panel([&](float px, float py, float pw) {
    Text(a_.display, 44, px, py, kText, r.label);
    py += 72;
    py += Wrapped(a_.body, 23, px, py, pw, White(0.68f), r.help);
    py += 34;
    if (r.values.empty()) return;
    const int cur = SettingIndex(r);
    if (!r.keys.empty() && std::strcmp(r.keys[0], "hub_music_volume") == 0) {
      const float shown = knobs_[kSettingsTab << 16 | (0x8000 + sel)];
      Text(a_.display, 64, px, py, kText, r.labels[size_t(cur)]);
      py += 110;
      Rect(px, py, pw, 10, White(0.14f), 5);
      Rect(px, py, std::max(10.0f, pw * shown), 10, kAccent, 5);
      const float pop = 1 + 0.4f * (1 - Smooth(value_in_));
      dl_->AddCircleFilled(P(px + pw * shown, py + 5), 15 * pop * s_, White(1), 28);
      Text(a_.body, 19, px, py + 30, White(0.45f), "Off");
      Text(a_.body, 19, px + pw, py + 30, White(0.45f), "100%", 1);
      return;
    }
    // (long lists - the languages - in two columns)
    const int n = int(r.labels.size());
    const int per_col = n > 7 ? (n + 1) / 2 : n;
    const float col_w = n > 7 ? pw * 0.5f : pw;
    const float top = py;
    for (int k = 0; k < n; ++k) {
      const float cx = px + float(k / per_col) * col_w, cyy = top + float(k % per_col) * 42;
      const bool c = k == cur;
      const float pop = c ? 1 + 0.6f * (1 - Smooth(value_in_)) : 1;
      if (c) dl_->AddCircleFilled(P(cx + 8, cyy + 14), 6 * pop * s_, kAccent, 20);
      else dl_->AddCircle(P(cx + 8, cyy + 14), 5.5f * s_, White(0.3f), 20, 1.5f * s_);
      Text(c ? a_.body_bold : a_.body, 22, cx + 30, cyy, c ? kText : White(0.45f),
           Fit(c ? a_.body_bold : a_.body, 22, r.labels[size_t(k)], col_w - 40));
    }
  });
}

void Hub::DrawAbout() {
  PageTitle("ABOUT", "Skate 3 for PlayStation 5  \xC2\xB7  " + version_);
  struct Credit {
    const char* who;
    const char* what;
  };
  const Credit people[] = {
      {"mchughalex",
       "Made Skate 3 Recomp, the native PC recompilation of Skate 3, with its native renderer, enhanced graphics "
       "and settings. This PlayStation 5 version is built on his work."},
      {"@elripalda", "Brought it to PlayStation 5: the PS5 port and this hub."},
      {"ReXGlue", "The static recompiler and runtime that turn the Xbox 360 game into a native program."},
      {"Xenia", "The Xbox 360 emulator whose research the runtime builds on."},
  };
  float y = 276;
  for (const Credit& c : people) {
    Text(a_.display, 32, 112, y, kText, c.who);
    Wrapped(a_.body, 21, 112, y + 46, 860, White(0.62f), c.what);
    y += 112;
  }
  const float px = kPanelX;
  float py = 286;
  Text(a_.body_bold, 15, px, py, White(0.45f), "BUILT WITH", 0, 3);
  py += 40;
  const char* parts[] = {"PS5 Payload SDK", "Mesa RADV (Vulkan)", "Dear ImGui", "FFmpeg",
                         "stb_image, stb_vorbis, dr_mp3 and dr_flac", "Oswald and Inter fonts (SIL Open Font License)"};
  for (const char* part : parts) {
    Text(a_.body, 21, px, py, White(0.78f), part);
    py += 36;
  }
  py += 30;
  Text(a_.body_bold, 15, px, py, White(0.45f), "PLEASE NOTE", 0, 3);
  py += 40;
  Wrapped(a_.body, 19, px, py, 680, White(0.55f),
          "An unofficial fan project, not affiliated with or endorsed by EA, Microsoft or Sony. Skate 3 and all related "
          "names belong to their owners. No game files are included: you need your own copy of the game.");
}

// --------------------------------------------------------------- behaviour

int Hub::Items() const {
  switch (tab_) {
    case kSettingsTab: return int(Settings().size());
    default: return 0;
  }
}

void Hub::Activate(int dir) {
  if (tab_ != kSettingsTab) return;
  const auto& r = Settings()[size_t(item_[tab_])];
  if (r.values.empty()) {  // Reset to Defaults (Cross only)
    if (dir != 1) return;
    if (config_.reset) config_.reset();
    if (audio_.music_volume) audio_.music_volume(MusicVolume());
    Toast("Settings reset to defaults");
    Play(Sound::kSelect);
    return;
  }
  const int n = int(r.values.size());
  const bool slider = std::strcmp(r.keys[0], "hub_music_volume") == 0;
  int idx = (SettingIndex(r) + dir + n) % n;
  if (slider) {  // (a slider stops at its ends)
    idx = std::clamp(SettingIndex(r) + dir, 0, n - 1);
    if (idx == SettingIndex(r)) {
      Play(Sound::kError);
      return;
    }
    if (audio_.music_volume) audio_.music_volume(std::atoi(r.values[size_t(idx)]));
  }
  Apply(r, idx);
  value_in_ = 0;
  value_dir_ = float(dir);
  Play(Sound::kSelect);
}

void Hub::Navigate(const Pad& pad) {
  const uint32_t p = pad.pressed;
  if (leaving_) return;
  if ((p & kR3) && audio_.next_song) audio_.next_song();  // (quietly: the music just changes)
  if (confirm_open_) {
    if ((p & (kCross | kCircle)) && confirm_in_ > 0.6f) {
      confirm_open_ = false;
      Play(Sound::kBack);
    }
    return;
  }
  auto start = [&] {
    if (!game_.files) {
      Play(Sound::kError);
      Notice("The game isn't set up",
             "Set Skate 3 up with Recomp Store, or copy the files of your own copy of the game into "
             "/data/homebrew/PPSA93003/Assets/ (default.xex and the data folder).");
    } else if (!game_.title_update && !game_.title_update_package) {
      Play(Sound::kError);
      Notice("Title Update 3 is missing",
             "This version of the game needs Title Update 3. Put the file TU_12K2276_000000C000000.00000000000O3 in "
             "/data/homebrew/PPSA93003/Assets/ (Recomp Store does it for you).");
    } else {
      press_ = 1, launch_ = true;
    }
  };
  if (p & kOptions) {
    start();
    return;
  }
  if (p & (kL1 | kR1)) {
    GoTo((tab_ + ((p & kR1) ? 1 : kTabCount - 1)) % kTabCount, (p & kR1) ? 1.0f : -1.0f);
    Play(Sound::kPage);
    return;
  }
  if ((p & kCircle) && tab_ != kHome) {
    GoTo(kHome, -1);
    Play(Sound::kBack);
    return;
  }
  if (tab_ == kHome) {
    if (p & kCross) start();
    return;
  }
  const int n = Items();
  if (n == 0) return;
  int& sel = item_[tab_];
  const int before = sel;
  if (p & kUp) sel = (sel + n - 1) % n;
  if (p & kDown) sel = (sel + 1) % n;
  if (sel != before) Play(Sound::kMove);
  if (tab_ == kSettingsTab) {
    const bool action = Settings()[size_t(sel)].values.empty();
    if ((p & kLeft) && !action) Activate(-1);
    if ((p & kCross) || ((p & kRight) && !action)) Activate(1);
    if (p & (kLeft | kRight | kCross)) press_ = 0.6f;
  }
}

void Hub::GoTo(int tab, float dir) {
  if (tab == tab_) return;
  tab_ = tab;
  scroll_f_ = -1;
  scroll_px_ = 0;
  focus_y_ = -1;
  page_in_ = 0;
  page_dir_ = dir;
  panel_in_ = 1;
  panel_item_ = item_[tab_];
}

bool Hub::Draw(const Pad& pad, float dt) {
  dt_ = std::clamp(dt, 0.0f, 0.1f);
  time_ += dt_;
  toast_time_ = std::max(0.0f, toast_time_ - dt_);
  Navigate(pad);
  if (launch_ && !leaving_) {
    leaving_ = true, press_ = 1;
    Play(Sound::kStart);
  }
  if (item_[tab_] != panel_item_) panel_item_ = item_[tab_], panel_in_ = 0;
  Ease(panel_in_, 1, 10);
  Ease(value_in_, 1, 12);
  Ease(press_, 0, 9);
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  s_ = std::min(size.x / kW, size.y / kH);
  ox_ = (size.x - kW * s_) * 0.5f;
  oy_ = (size.y - kH * s_) * 0.5f;
  dl_ = ImGui::GetForegroundDrawList();
  DrawBackdrop();
  DrawTopBar();
  // the page slides in from the side it came from, fading up
  Ease(page_in_, 1, 11);
  const int first = dl_->VtxBuffer.Size;
  const float shift = (1 - page_in_) * 48 * page_dir_;
  ox_ += shift * s_;
  switch (tab_) {
    case kHome: DrawHome(); break;
    case kSettingsTab: DrawSettings(); break;
    case kAboutTab: DrawAbout(); break;
  }
  ox_ -= shift * s_;
  Fade(first, Smooth(page_in_));
  DrawHints();
  DrawConfirm();
  // opening: up from black; START GAME: down to black
  const float open = Smooth(time_ / 0.7f);
  if (leaving_) leave_ = std::min(1.0f, leave_ + dt_ / 0.6f);
  const float black = std::max(1 - open, Smooth(leave_));
  if (black > 0.002f) dl_->AddRectFilled(ImVec2(0, 0), size, Black(black));
  return leaving_ && leave_ >= 1;
}

}  // namespace skate3ps5::hub
