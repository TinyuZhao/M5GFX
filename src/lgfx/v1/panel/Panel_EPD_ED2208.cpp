/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)

Author:
 [lovyan03](https://twitter.com/lovyan03)
/----------------------------------------------------------------------------*/
#include "Panel_EPD_ED2208.hpp"
#include "lgfx/v1/Bus.hpp"
#include "lgfx/v1/platforms/common.hpp"
#include "lgfx/v1/misc/pixelcopy.hpp"
#include "lgfx/v1/misc/colortype.hpp"

#include <cmath>
#include <cfloat>

#ifdef min
#undef min
#endif

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  // --- Ideal RGB palette (for Bayer / none / floyd_steinberg) ---

  static constexpr struct { uint8_t r, g, b, idx; } epd_palette[] = {
    {   0,   0,   0, Panel_EPD_ED2208::EPD_BLACK  },
    { 255, 255, 255, Panel_EPD_ED2208::EPD_WHITE  },
    { 255, 255,   0, Panel_EPD_ED2208::EPD_YELLOW },
    { 255,   0,   0, Panel_EPD_ED2208::EPD_RED    },
    {   0,   0, 255, Panel_EPD_ED2208::EPD_BLUE   },
    {   0, 255,   0, Panel_EPD_ED2208::EPD_GREEN  },
  };

  // --- Spectra6 Lab-space infrastructure ---
  // Adjusted ideal palette for Lab-space dithering (converter.py fallback).
  // Black/White are pure endpoints; chromatic colors are tuned for this panel.

  static constexpr struct { uint8_t r, g, b, idx; } epd_palette_s6[] = {
    {   0,   0,   0, Panel_EPD_ED2208::EPD_BLACK  },
    { 255, 255, 255, Panel_EPD_ED2208::EPD_WHITE  },
    { 255, 221,   0, Panel_EPD_ED2208::EPD_YELLOW },
    { 206,  38,  54, Panel_EPD_ED2208::EPD_RED    },
    {   0,  64, 255, Panel_EPD_ED2208::EPD_BLUE   },
    {   0, 128,   0, Panel_EPD_ED2208::EPD_GREEN  },
  };

  // --- 16x16 Bayer matrix (shared by bayer and pair modes) ---

  static constexpr int8_t bayer16[256] = {
    -127,    0, -95,   32,-119,   8, -87,  40,-125,   2, -93,  34,-117,  10, -85,  42,
      64,  -63,  96,  -31,  72, -55, 104, -23,  66, -61,  98, -29,  74, -53, 106, -21,
     -79,   48,-111,   16, -71,  56,-103,  24, -77,  50,-109,  18, -69,  58,-101,  26,
     112,  -15,  80,  -47, 120,  -7,  88, -39, 114, -13,  82, -45, 122,  -5,  90, -37,
    -115,   12, -83,   44,-123,   4, -91,  36,-113,  14, -81,  46,-121,   6, -89,  38,
      76,  -51, 108,  -19,  68, -59, 100, -27,  78, -49, 110, -17,  70, -57, 102, -25,
     -67,   60, -99,   28, -75,  52,-107,  20, -65,  62, -97,  30, -73,  54,-105,  22,
     124,   -3,  92,  -35, 116, -11,  84, -43, 126,  -1,  94, -33, 118,  -9,  86, -41,
    -124,    3, -92,   35,-116,  11, -84,  43,-126,   1, -94,  33,-118,   9, -86,  41,
      67,  -60,  99,  -28,  75, -52, 107, -20,  65, -62,  97, -30,  73, -54, 105, -22,
     -76,   51,-108,   19, -68,  59,-100,  27, -78,  49,-110,  17, -70,  57,-102,  25,
     115,  -12,  83,  -44, 123,  -4,  91, -36, 113, -14,  81, -46, 121,  -6,  89, -38,
    -112,   15, -80,   47,-120,   7, -88,  39,-114,  13, -82,  45,-122,   5, -90,  37,
      79,  -48, 111,  -16,  71, -56, 103, -24,  77, -50, 109, -18,  69, -58, 101, -26,
     -64,   63, -96,   31, -72,  55,-104,  23, -66,  61, -98,  29, -74,  53,-106,  21,
     127,    0,  95,  -32, 119,  -8,  87, -40, 125,  -2,  93, -34, 117, -10,  85, -42,
  };

  // Pre-processing parameters from epd6color_adjust.json.
  static constexpr float S6_CONTRAST   = 1.08f;   // 108 / 100
  static constexpr float S6_SATURATION = 0.81f;    //  81 / 100
  static constexpr float S6_GAMMA_EXP  = 1.8519f;  // 1.0 / (54 / 100)

  static float srgb_lut[256];
  struct LabEntry { float L, a, b; uint8_t idx; };
  static LabEntry lab_palette[6];
  static bool s6_initialized = false;

  static inline void rgb_to_lab(uint8_t r, uint8_t g, uint8_t b,
                                float& out_L, float& out_a, float& out_b)
  {
    float R = srgb_lut[r];
    float G = srgb_lut[g];
    float B = srgb_lut[b];

    float X = (0.4124564f * R + 0.3575761f * G + 0.1804375f * B) / 0.95047f;
    float Y =  0.2126729f * R + 0.7151522f * G + 0.0721750f * B;
    float Z = (0.0193339f * R + 0.1191920f * G + 0.9503041f * B) / 1.08883f;

    auto f = [](float t) -> float {
      return t > 0.008856f ? cbrtf(t) : 7.787037f * t + 0.137931f;
    };

    float fX = f(X), fY = f(Y), fZ = f(Z);
    out_L = 116.0f * fY - 16.0f;
    out_a = 500.0f * (fX - fY);
    out_b = 200.0f * (fY - fZ);
  }

  static void init_spectra6_tables()
  {
    if (s6_initialized) return;

    for (int i = 0; i < 256; ++i) {
      float c = i / 255.0f;
      srgb_lut[i] = (c <= 0.04045f) ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    }

    for (int i = 0; i < 6; ++i) {
      auto& p = epd_palette_s6[i];
      rgb_to_lab(p.r, p.g, p.b, lab_palette[i].L, lab_palette[i].a, lab_palette[i].b);
      lab_palette[i].idx = p.idx;
    }

    s6_initialized = true;
  }

  // Apply contrast → saturation(HSV) → gamma, matching the Python converter.
  static inline void preprocess_pixel(uint8_t r_in, uint8_t g_in, uint8_t b_in,
                                      uint8_t& r_out, uint8_t& g_out, uint8_t& b_out)
  {
    // Contrast (then quantize to uint8 as Python does)
    float rf = r_in * S6_CONTRAST;
    float gf = g_in * S6_CONTRAST;
    float bf = b_in * S6_CONTRAST;
    if (rf > 255.0f) rf = 255.0f;
    if (gf > 255.0f) gf = 255.0f;
    if (bf > 255.0f) bf = 255.0f;
    uint8_t r8 = (uint8_t)(rf + 0.5f);
    uint8_t g8 = (uint8_t)(gf + 0.5f);
    uint8_t b8 = (uint8_t)(bf + 0.5f);

    // Saturation via HSV (matching OpenCV BGR2HSV / HSV2BGR at uint8 precision)
    // RGB→HSV
    uint8_t max_c = r8 > g8 ? (r8 > b8 ? r8 : b8) : (g8 > b8 ? g8 : b8);
    uint8_t min_c = r8 < g8 ? (r8 < b8 ? r8 : b8) : (g8 < b8 ? g8 : b8);
    uint8_t delta = max_c - min_c;
    uint8_t v = max_c;
    uint8_t s = (v == 0) ? 0 : (uint8_t)((int)delta * 255 / (int)v);
    int h = 0;
    if (delta > 0) {
      if (max_c == r8)      h = 30 * (int)(g8 - b8) / (int)delta;
      else if (max_c == g8) h = 60 + 30 * (int)(b8 - r8) / (int)delta;
      else                  h = 120 + 30 * (int)(r8 - g8) / (int)delta;
      if (h < 0) h += 180;
    }

    // Scale S
    s = (uint8_t)(s * S6_SATURATION + 0.5f);

    // HSV→RGB (OpenCV-style, H:0-180, S:0-255, V:0-255)
    if (s == 0) {
      r8 = g8 = b8 = v;
    } else {
      int sector = h / 30;
      int f_int  = h - sector * 30;
      uint8_t p = (uint8_t)((int)v * (255 - (int)s) / 255);
      uint8_t q = (uint8_t)((int)v * (255 - (int)s * f_int / 30) / 255);
      uint8_t t = (uint8_t)((int)v * (255 - (int)s * (30 - f_int) / 30) / 255);
      switch (sector % 6) {
        case 0: r8 = v; g8 = t; b8 = p; break;
        case 1: r8 = q; g8 = v; b8 = p; break;
        case 2: r8 = p; g8 = v; b8 = t; break;
        case 3: r8 = p; g8 = q; b8 = v; break;
        case 4: r8 = t; g8 = p; b8 = v; break;
        case 5: r8 = v; g8 = p; b8 = q; break;
      }
    }

    // Gamma
    rf = powf(r8 / 255.0f, S6_GAMMA_EXP) * 255.0f;
    gf = powf(g8 / 255.0f, S6_GAMMA_EXP) * 255.0f;
    bf = powf(b8 / 255.0f, S6_GAMMA_EXP) * 255.0f;

    r_out = (uint8_t)(rf + 0.5f);
    g_out = (uint8_t)(gf + 0.5f);
    b_out = (uint8_t)(bf + 0.5f);
  }

  // --- Panel implementation ---

  Panel_EPD_ED2208::Panel_EPD_ED2208(void)
  {
    _cfg.dummy_read_bits = 0;
    _epd_mode = epd_mode_t::epd_quality;
  }

  Panel_EPD_ED2208::~Panel_EPD_ED2208(void)
  {
    if (_lines_buffer) {
      heap_free(_lines_buffer);
      _lines_buffer = nullptr;
    }
    if (_framebuffer) {
      heap_free(_framebuffer);
      _framebuffer = nullptr;
    }
  }

  color_depth_t Panel_EPD_ED2208::setColorDepth(color_depth_t depth)
  {
    (void)depth;
    _write_depth = color_depth_t::rgb888_3Byte;
    _read_depth = color_depth_t::rgb888_3Byte;
    return color_depth_t::rgb888_3Byte;
  }

  uint8_t Panel_EPD_ED2208::_rgb_to_epd_color(int32_t r, int32_t g, int32_t b)
  {
    uint32_t min_dist = UINT32_MAX;
    uint8_t best = EPD_WHITE;
    for (const auto& p : epd_palette) {
      int32_t dr = r - (int32_t)p.r;
      int32_t dg = g - (int32_t)p.g;
      int32_t db = b - (int32_t)p.b;
      uint32_t dist = dr * dr + dg * dg + db * db;
      if (dist < min_dist) {
        min_dist = dist;
        best = p.idx;
      }
    }
    return best;
  }

  uint8_t Panel_EPD_ED2208::_rgb_to_epd_color(int32_t r, int32_t g, int32_t b,
                                              int32_t& er, int32_t& eg, int32_t& eb)
  {
    uint32_t min_dist = UINT32_MAX;
    uint8_t best = EPD_WHITE;
    int32_t best_r = 255, best_g = 255, best_b = 255;
    for (const auto& p : epd_palette) {
      int32_t dr = r - (int32_t)p.r;
      int32_t dg = g - (int32_t)p.g;
      int32_t db = b - (int32_t)p.b;
      uint32_t dist = dr * dr + dg * dg + db * db;
      if (dist < min_dist) {
        min_dist = dist;
        best = p.idx;
        best_r = p.r; best_g = p.g; best_b = p.b;
      }
    }
    er = r - best_r;
    eg = g - best_g;
    eb = b - best_b;
    return best;
  }

  void Panel_EPD_ED2208::_send_command(uint8_t cmd)
  {
    _bus->writeCommand(cmd, 8);
  }

  void Panel_EPD_ED2208::_send_data(uint8_t data)
  {
    _bus->writeData(data, 8);
  }

  bool Panel_EPD_ED2208::_wait_busy(uint32_t timeout)
  {
    _bus->wait();
    if (_cfg.pin_busy >= 0 && !gpio_in(_cfg.pin_busy))
    {
      uint32_t start_time = millis();
      do
      {
        if (millis() - start_time > timeout) {
          return false;
        }
        lgfx::delay(10);
      } while (!gpio_in(_cfg.pin_busy));
      lgfx::delay(200);
    }
    return true;
  }

  void Panel_EPD_ED2208::_init_sequence(void)
  {
    _bus->beginTransaction();
    cs_control(false);

    for (uint8_t i = 0; auto cmds = getInitCommands(i); i++)
    {
      _wait_busy();
      command_list(cmds);
    }

    _wait_busy();
    _send_command(0x61);    // Resolution
    _send_data((_cfg.panel_width >> 8) & 0xFF);
    _send_data(_cfg.panel_width & 0xFF);
    _send_data((_cfg.panel_height >> 8) & 0xFF);
    _send_data(_cfg.panel_height & 0xFF);

    _bus->wait();
    cs_control(true);
    _bus->endTransaction();
  }

  void Panel_EPD_ED2208::_turn_on_display(void)
  {
    _bus->beginTransaction();
    cs_control(false);

    _send_command(0x04);    // POWER_ON
    _wait_busy();
    lgfx::delay(200);

    _send_command(0x06);
    _send_data(0x6F);
    _send_data(0x1F);
    _send_data(0x17);
    _send_data(0x27);
    lgfx::delay(200);

    _send_command(0x12);    // DISPLAY_REFRESH
    _send_data(0x00);
    _wait_busy();

    _send_command(0x02);    // POWER_OFF
    _send_data(0x00);
    _wait_busy();
    lgfx::delay(200);

    _bus->wait();
    cs_control(true);
    _bus->endTransaction();
  }

  bool Panel_EPD_ED2208::init(bool use_reset)
  {
    pinMode(_cfg.pin_busy, pin_mode_t::input_pullup);

    setColorDepth(color_depth_t::rgb888_3Byte);

    uint32_t pw = _cfg.panel_width;
    uint32_t ph = _cfg.panel_height;
    uint32_t bytes_per_line = pw * sizeof(bgr888_t);

    if (_framebuffer) { heap_free(_framebuffer); _framebuffer = nullptr; }
    if (_lines_buffer) { heap_free(_lines_buffer); _lines_buffer = nullptr; }

    _framebuffer = static_cast<uint8_t*>(heap_alloc_psram(bytes_per_line * ph));
    if (!_framebuffer) { return false; }

    _lines_buffer = static_cast<uint8_t**>(heap_alloc(ph * sizeof(uint8_t*)));
    if (!_lines_buffer) { heap_free(_framebuffer); _framebuffer = nullptr; return false; }

    for (uint32_t y = 0; y < ph; ++y) {
      _lines_buffer[y] = _framebuffer + y * bytes_per_line;
    }

    memset(_framebuffer, 0xFF, bytes_per_line * ph);

    init_spectra6_tables();

    if (!Panel_FrameBufferBase::init(false))
    {
      return false;
    }

    _after_wake();
    setRotation(_rotation);

    return true;
  }

  void Panel_EPD_ED2208::_after_wake(void)
  {
    _init_sequence();
  }

  void Panel_EPD_ED2208::waitDisplay(void)
  {
    _wait_busy();
  }

  bool Panel_EPD_ED2208::displayBusy(void)
  {
    return _cfg.pin_busy >= 0 && !gpio_in(_cfg.pin_busy);
  }

  void Panel_EPD_ED2208::display(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h)
  {
    if (0 < w && 0 < h)
    {
      uint_fast8_t r = _internal_rotation;
      if (r)
      {
        if ((1u << r) & 0b10010110) { y = _height - (y + h); }
        if (r & 2)                  { x = _width  - (x + w); }
        if (r & 1) { std::swap(x, y);  std::swap(w, h); }
      }
      _range_mod.left   = std::min<int_fast16_t>(_range_mod.left  , x        );
      _range_mod.right  = std::max<int_fast16_t>(_range_mod.right , x + w - 1);
      _range_mod.top    = std::min<int_fast16_t>(_range_mod.top   , y        );
      _range_mod.bottom = std::max<int_fast16_t>(_range_mod.bottom, y + h - 1);
    }
    if (!_range_mod.empty()) {
      _exec_transfer();
      _turn_on_display();
      _range_mod.top = INT16_MAX;
      _range_mod.left = INT16_MAX;
      _range_mod.right = 0;
      _range_mod.bottom = 0;
    }
  }

  // --- Dither: none ---

  void Panel_EPD_ED2208::_dither_row_none(const bgr888_t* src, uint8_t* dst, uint_fast16_t /*y*/)
  {
    uint_fast16_t w = _cfg.panel_width;
    for (uint_fast16_t x = 0; x < w; x += 2) {
      uint8_t c0 = _rgb_to_epd_color(src[x].r, src[x].g, src[x].b);
      uint8_t c1 = EPD_WHITE;
      if (x + 1 < w) {
        c1 = _rgb_to_epd_color(src[x + 1].r, src[x + 1].g, src[x + 1].b);
      }
      dst[x >> 1] = (c0 << 4) | c1;
    }
  }

  // --- Dither: Bayer 16x16 ---

  void Panel_EPD_ED2208::_dither_row_bayer(const bgr888_t* src, uint8_t* dst, uint_fast16_t y)
  {
    auto row_b = &bayer16[(y & 15) << 4];
    uint_fast16_t w = _cfg.panel_width;
    for (uint_fast16_t x = 0; x < w; x += 2) {
      int32_t bias0 = (int32_t)row_b[x & 15];
      uint8_t c0 = _rgb_to_epd_color((int32_t)src[x].r + bias0,
                                     (int32_t)src[x].g + bias0,
                                     (int32_t)src[x].b + bias0);
      uint8_t c1 = EPD_WHITE;
      if (x + 1 < w) {
        int32_t bias1 = (int32_t)row_b[(x + 1) & 15];
        c1 = _rgb_to_epd_color((int32_t)src[x + 1].r + bias1,
                               (int32_t)src[x + 1].g + bias1,
                               (int32_t)src[x + 1].b + bias1);
      }
      dst[x >> 1] = (c0 << 4) | c1;
    }
  }


  // --- Dither: Floyd-Steinberg (RGB space, ideal palette, serpentine) ---

  void Panel_EPD_ED2208::_dither_row_floyd(const bgr888_t* src, uint8_t* dst, uint_fast16_t w,
                                           int16_t* err_curr, int16_t* err_next, bool serpentine)
  {
    memset(err_next, 0, w * 3 * sizeof(int16_t));

    uint8_t* idx_buf = static_cast<uint8_t*>(alloca(w));

    int x_start = serpentine ? (int)w - 1 : 0;
    int x_end   = serpentine ? -1 : (int)w;
    int x_step  = serpentine ? -1 : 1;
    for (int x = x_start; x != x_end; x += x_step) {
      int32_t r = (int32_t)src[x].r + err_curr[x * 3 + 0];
      int32_t g = (int32_t)src[x].g + err_curr[x * 3 + 1];
      int32_t b = (int32_t)src[x].b + err_curr[x * 3 + 2];

      if (r < 0) r = 0; else if (r > 255) r = 255;
      if (g < 0) g = 0; else if (g > 255) g = 255;
      if (b < 0) b = 0; else if (b > 255) b = 255;

      int32_t er, eg, eb;
      idx_buf[x] = _rgb_to_epd_color(r, g, b, er, eg, eb);

      auto distribute = [&](int dx, int dy, int num) {
        int nx = x + dx;
        if (nx < 0 || nx >= (int)w) return;
        int16_t* row = (dy == 0) ? err_curr : err_next;
        row[nx * 3 + 0] += (int16_t)(er * num / _diffusion_div);
        row[nx * 3 + 1] += (int16_t)(eg * num / _diffusion_div);
        row[nx * 3 + 2] += (int16_t)(eb * num / _diffusion_div);
      };

      distribute( 1, 0, 7);
      distribute(-1, 1, 3);
      distribute( 0, 1, 5);
      distribute( 1, 1, 1);
    }

    for (uint_fast16_t x = 0; x < w; x += 2) {
      uint8_t hi = idx_buf[x];
      uint8_t lo = (x + 1 < w) ? idx_buf[x + 1] : EPD_WHITE;
      dst[x >> 1] = (hi << 4) | lo;
    }
  }

  // --- Dither: Spectra6 (Lab space, measured palette, pre-processing, serpentine) ---

  void Panel_EPD_ED2208::_dither_row_spectra6(const bgr888_t* src, uint8_t* dst, uint_fast16_t w,
                                              float* err_curr, float* err_next, float* lab_row,
                                              bool serpentine)
  {
    // Pre-process + convert to Lab
    for (uint_fast16_t x = 0; x < w; ++x) {
      uint8_t pr, pg, pb;
      preprocess_pixel(src[x].r, src[x].g, src[x].b, pr, pg, pb);
      rgb_to_lab(pr, pg, pb, lab_row[x * 3], lab_row[x * 3 + 1], lab_row[x * 3 + 2]);
    }

    memset(err_next, 0, w * 3 * sizeof(float));

    uint8_t* idx_buf = static_cast<uint8_t*>(alloca(w));

    // Python reference uses fixed FS kernel without mirroring on serpentine rows.
    // On reverse-scan rows, the (1,0) 7/16 distribution goes to an already-
    // processed pixel and is effectively discarded, providing ~44% implicit
    // error damping that prevents Lab-space error runaway.
    int x_start = serpentine ? (int)w - 1 : 0;
    int x_end   = serpentine ? -1 : (int)w;
    int x_step  = serpentine ? -1 : 1;

    for (int x = x_start; x != x_end; x += x_step) {
      float L = lab_row[x * 3 + 0] + err_curr[x * 3 + 0];
      float a = lab_row[x * 3 + 1] + err_curr[x * 3 + 1];
      float b = lab_row[x * 3 + 2] + err_curr[x * 3 + 2];

      float min_dist = FLT_MAX;
      int best = 0;
      for (int i = 0; i < 6; ++i) {
        float dL = L - lab_palette[i].L;
        float da = a - lab_palette[i].a;
        float db = b - lab_palette[i].b;
        float dist = dL * dL + da * da + db * db;
        if (dist < min_dist) {
          min_dist = dist;
          best = i;
        }
      }

      idx_buf[x] = lab_palette[best].idx;

      float eL = L - lab_palette[best].L;
      float ea = a - lab_palette[best].a;
      float eb = b - lab_palette[best].b;

      auto distribute = [&](int dx, int dy, float weight) {
        int nx = x + dx;
        if (nx < 0 || nx >= (int)w) return;
        float* row = (dy == 0) ? err_curr : err_next;
        row[nx * 3 + 0] += eL * weight;
        row[nx * 3 + 1] += ea * weight;
        row[nx * 3 + 2] += eb * weight;
      };

      float div = (float)_diffusion_div;
      distribute( 1, 0, 7.0f / div);
      distribute(-1, 1, 3.0f / div);
      distribute( 0, 1, 5.0f / div);
      distribute( 1, 1, 1.0f / div);
    }

    for (uint_fast16_t x = 0; x < w; x += 2) {
      uint8_t hi = idx_buf[x];
      uint8_t lo = (x + 1 < w) ? idx_buf[x + 1] : EPD_WHITE;
      dst[x >> 1] = (hi << 4) | lo;
    }
  }

  // --- Transfer ---

  void Panel_EPD_ED2208::_exec_transfer(void)
  {
    uint_fast16_t w = _cfg.panel_width;
    uint_fast16_t h = _cfg.panel_height;
    uint32_t row_bytes = (w + 1) >> 1;

    uint8_t* row_buf[2];
    row_buf[0] = static_cast<uint8_t*>(heap_alloc_dma(row_bytes));
    row_buf[1] = static_cast<uint8_t*>(heap_alloc_dma(row_bytes));

    // FS (RGB) error buffers
    int16_t* fs_err_a = nullptr;
    int16_t* fs_err_b = nullptr;
    // Spectra6 (Lab) error + conversion buffers
    float* s6_err_a = nullptr;
    float* s6_err_b = nullptr;
    float* s6_lab   = nullptr;

    size_t ch3 = (size_t)w * 3;

    if (_epd_mode == epd_mode_t::epd_text) {
      fs_err_a = static_cast<int16_t*>(heap_alloc(ch3 * sizeof(int16_t)));
      fs_err_b = static_cast<int16_t*>(heap_alloc(ch3 * sizeof(int16_t)));
      if (fs_err_a) memset(fs_err_a, 0, ch3 * sizeof(int16_t));
      if (fs_err_b) memset(fs_err_b, 0, ch3 * sizeof(int16_t));
    }
    else if (_epd_mode == epd_mode_t::epd_quality) {
      s6_err_a = static_cast<float*>(heap_alloc(ch3 * sizeof(float)));
      s6_err_b = static_cast<float*>(heap_alloc(ch3 * sizeof(float)));
      s6_lab   = static_cast<float*>(heap_alloc(ch3 * sizeof(float)));
      if (s6_err_a) memset(s6_err_a, 0, ch3 * sizeof(float));
      if (s6_err_b) memset(s6_err_b, 0, ch3 * sizeof(float));
    }

    _bus->beginTransaction();
    cs_control(false);

    _send_command(0x10);    // Data start transmission

    bool serpentine = false;
    for (uint_fast16_t y = 0; y < h; ++y) {
      uint8_t* dst = row_buf[y & 1];
      const bgr888_t* src = reinterpret_cast<const bgr888_t*>(_lines_buffer[y]);

      switch (_epd_mode) {
        case epd_mode_t::epd_fastest:
          _dither_row_none(src, dst, y);
          break;
        case epd_mode_t::epd_fast:
          _dither_row_bayer(src, dst, y);
          break;
        case epd_mode_t::epd_text:
          if (fs_err_a && fs_err_b) {
            int16_t* curr = (y & 1) ? fs_err_b : fs_err_a;
            int16_t* next = (y & 1) ? fs_err_a : fs_err_b;
            _dither_row_floyd(src, dst, w, curr, next, serpentine);
            serpentine = !serpentine;
          } else {
            _dither_row_bayer(src, dst, y);
          }
          break;
        case epd_mode_t::epd_quality:
          if (s6_err_a && s6_err_b && s6_lab) {
            float* curr = (y & 1) ? s6_err_b : s6_err_a;
            float* next = (y & 1) ? s6_err_a : s6_err_b;
            _dither_row_spectra6(src, dst, w, curr, next, s6_lab, serpentine);
            serpentine = !serpentine;
          } else {
            _dither_row_bayer(src, dst, y);
          }
          break;
      }

      _bus->writeBytes(dst, row_bytes, true, true);
    }
    _bus->wait();

    cs_control(true);
    _bus->endTransaction();

    if (row_buf[0]) heap_free(row_buf[0]);
    if (row_buf[1]) heap_free(row_buf[1]);
    if (fs_err_a) heap_free(fs_err_a);
    if (fs_err_b) heap_free(fs_err_b);
    if (s6_err_a) heap_free(s6_err_a);
    if (s6_err_b) heap_free(s6_err_b);
    if (s6_lab)   heap_free(s6_lab);
  }

  void Panel_EPD_ED2208::setSleep(bool flg)
  {
    if (flg)
    {
      startWrite();
      _send_command(0x07);  // DEEP_SLEEP
      _send_data(0xA5);
      endWrite();
    }
    else
    {
      _after_wake();
    }
  }

  void Panel_EPD_ED2208::setPowerSave(bool flg)
  {
    if (flg) {
      setSleep(true);
    }
  }

//----------------------------------------------------------------------------
 }
}
