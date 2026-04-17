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

#ifdef min
#undef min
#endif

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  // Ideal RGB palette for nearest-color lookup.
  static constexpr struct { uint8_t r, g, b, idx; } epd_palette[] = {
    {   0,   0,   0, Panel_EPD_ED2208::EPD_BLACK  },
    { 255, 255, 255, Panel_EPD_ED2208::EPD_WHITE  },
    { 255, 255,   0, Panel_EPD_ED2208::EPD_YELLOW },
    { 255,   0,   0, Panel_EPD_ED2208::EPD_RED    },
    {   0,   0, 255, Panel_EPD_ED2208::EPD_BLUE   },
    {   0, 255,   0, Panel_EPD_ED2208::EPD_GREEN  },
  };

  // 16x16 Bayer matrix, bias range [-127, 127].
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

  // --- Lab conversion infrastructure ---

  static constexpr float srgb_lut[256] = {
    0.0000000f, 0.0003035f, 0.0006071f, 0.0009106f, 0.0012141f, 0.0015176f, 0.0018212f, 0.0021247f, 0.0024282f, 0.0027317f, 0.0030353f, 0.0033465f, 0.0036765f, 0.0040247f, 0.0043914f, 0.0047770f,
    0.0051815f, 0.0056054f, 0.0060488f, 0.0065121f, 0.0069954f, 0.0074990f, 0.0080232f, 0.0085681f, 0.0091341f, 0.0097212f, 0.0103298f, 0.0109601f, 0.0116122f, 0.0122865f, 0.0129830f, 0.0137021f,
    0.0144438f, 0.0152085f, 0.0159963f, 0.0168074f, 0.0176420f, 0.0185002f, 0.0193824f, 0.0202886f, 0.0212190f, 0.0221739f, 0.0231534f, 0.0241576f, 0.0251869f, 0.0262412f, 0.0273209f, 0.0284260f,
    0.0295568f, 0.0307134f, 0.0318960f, 0.0331048f, 0.0343398f, 0.0356013f, 0.0368895f, 0.0382044f, 0.0395462f, 0.0409152f, 0.0423114f, 0.0437350f, 0.0451862f, 0.0466651f, 0.0481718f, 0.0497066f,
    0.0512695f, 0.0528606f, 0.0544803f, 0.0561285f, 0.0578054f, 0.0595112f, 0.0612461f, 0.0630100f, 0.0648033f, 0.0666259f, 0.0684782f, 0.0703601f, 0.0722719f, 0.0742136f, 0.0761854f, 0.0781874f,
    0.0802198f, 0.0822827f, 0.0843762f, 0.0865005f, 0.0886556f, 0.0908417f, 0.0930590f, 0.0953075f, 0.0975873f, 0.0998987f, 0.1022417f, 0.1046165f, 0.1070231f, 0.1094617f, 0.1119324f, 0.1144354f,
    0.1169707f, 0.1195384f, 0.1221388f, 0.1247718f, 0.1274377f, 0.1301365f, 0.1328683f, 0.1356333f, 0.1384316f, 0.1412633f, 0.1441285f, 0.1470273f, 0.1499598f, 0.1529262f, 0.1559265f, 0.1589608f,
    0.1620294f, 0.1651322f, 0.1682694f, 0.1714411f, 0.1746474f, 0.1778884f, 0.1811642f, 0.1844750f, 0.1878208f, 0.1912017f, 0.1946178f, 0.1980693f, 0.2015563f, 0.2050787f, 0.2086369f, 0.2122308f,
    0.2158605f, 0.2195262f, 0.2232280f, 0.2269659f, 0.2307400f, 0.2345506f, 0.2383976f, 0.2422811f, 0.2462013f, 0.2501583f, 0.2541521f, 0.2581829f, 0.2622507f, 0.2663556f, 0.2704978f, 0.2746773f,
    0.2788943f, 0.2831487f, 0.2874408f, 0.2917706f, 0.2961383f, 0.3005438f, 0.3049873f, 0.3094689f, 0.3139887f, 0.3185468f, 0.3231432f, 0.3277781f, 0.3324515f, 0.3371636f, 0.3419144f, 0.3467041f,
    0.3515326f, 0.3564001f, 0.3613068f, 0.3662526f, 0.3712377f, 0.3762621f, 0.3813260f, 0.3864294f, 0.3915725f, 0.3967552f, 0.4019778f, 0.4072402f, 0.4125426f, 0.4178851f, 0.4232677f, 0.4286905f,
    0.4341536f, 0.4396572f, 0.4452012f, 0.4507858f, 0.4564110f, 0.4620770f, 0.4677838f, 0.4735315f, 0.4793202f, 0.4851499f, 0.4910208f, 0.4969330f, 0.5028865f, 0.5088813f, 0.5149177f, 0.5209956f,
    0.5271151f, 0.5332764f, 0.5394795f, 0.5457245f, 0.5520114f, 0.5583404f, 0.5647115f, 0.5711248f, 0.5775804f, 0.5840784f, 0.5906188f, 0.5972018f, 0.6038273f, 0.6104956f, 0.6172066f, 0.6239604f,
    0.6307571f, 0.6375969f, 0.6444797f, 0.6514056f, 0.6583748f, 0.6653873f, 0.6724432f, 0.6795425f, 0.6866853f, 0.6938718f, 0.7011019f, 0.7083758f, 0.7156935f, 0.7230551f, 0.7304607f, 0.7379104f,
    0.7454042f, 0.7529422f, 0.7605245f, 0.7681511f, 0.7758222f, 0.7835378f, 0.7912979f, 0.7991027f, 0.8069523f, 0.8148466f, 0.8227858f, 0.8307699f, 0.8387990f, 0.8468732f, 0.8549926f, 0.8631572f,
    0.8713671f, 0.8796224f, 0.8879231f, 0.8962694f, 0.9046612f, 0.9130987f, 0.9215819f, 0.9301109f, 0.9386857f, 0.9473065f, 0.9559734f, 0.9646862f, 0.9734453f, 0.9822506f, 0.9911021f, 1.0000000f,
  };

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

  static inline void lab_to_rgb(float L, float a, float b,
                                int32_t& out_r, int32_t& out_g, int32_t& out_b)
  {
    float fy = (L + 16.0f) / 116.0f;
    float fx = a / 500.0f + fy;
    float fz = fy - b / 200.0f;

    auto f_inv = [](float t) -> float {
      return (t > 0.206893f) ? t * t * t : (t - 0.137931f) / 7.787037f;
    };

    float X = f_inv(fx) * 0.95047f;
    float Y = f_inv(fy);
    float Z = f_inv(fz) * 1.08883f;

    float R =  3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
    float G = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
    float B =  0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;

    auto to_srgb = [](float c) -> float {
      return (c <= 0.0031308f) ? 12.92f * c : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
    };

    out_r = (int32_t)(to_srgb(R) * 255.0f + 0.5f);
    out_g = (int32_t)(to_srgb(G) * 255.0f + 0.5f);
    out_b = (int32_t)(to_srgb(B) * 255.0f + 0.5f);
  }

  // Interleaved Gradient Noise (Jorge Jimenez, 2014).
  // Non-periodic 2D noise, blue-noise-like distribution.
  static inline float ign(uint_fast16_t x, uint_fast16_t y)
  {
    float f = 52.9829189f * fmodf(0.06711056f * x + 0.00583715f * y, 1.0f);
    return f - (int)f;
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

  // --- Dither: none (nearest-color only) ---

  void Panel_EPD_ED2208::_dither_row_none(const bgr888_t* src, uint8_t* dst, uint_fast16_t w, uint_fast16_t /*y*/)
  {
    for (uint_fast16_t x = 0; x < w; x += 2) {
      uint8_t c0 = _rgb_to_epd_color(src[x].r, src[x].g, src[x].b);
      uint8_t c1 = EPD_WHITE;
      if (x + 1 < w) {
        c1 = _rgb_to_epd_color(src[x + 1].r, src[x + 1].g, src[x + 1].b);
      }
      dst[x >> 1] = (c0 << 4) | c1;
    }
  }

  // --- Dither: Bayer RGB (uniform bias on all channels) ---

  void Panel_EPD_ED2208::_dither_row_bayer(const bgr888_t* src, uint8_t* dst, uint_fast16_t w, uint_fast16_t y)
  {
    auto row_b = &bayer16[(y & 15) << 4];
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

  // --- Dither: Bayer HSV (V-only bias, preserves saturation) ---

  static inline void hsv_bias_pixel(uint8_t r_in, uint8_t g_in, uint8_t b_in,
                                    int32_t bias, int32_t& r_out, int32_t& g_out, int32_t& b_out)
  {
    uint8_t max_c = r_in > g_in ? (r_in > b_in ? r_in : b_in) : (g_in > b_in ? g_in : b_in);
    uint8_t min_c = r_in < g_in ? (r_in < b_in ? r_in : b_in) : (g_in < b_in ? g_in : b_in);
    int v = max_c;
    int delta = max_c - min_c;
    int new_v = v + bias;

    if (v == 0 || delta == 0) {
      r_out = g_out = b_out = new_v;
    } else {
      r_out = (int)r_in * new_v / v;
      g_out = (int)g_in * new_v / v;
      b_out = (int)b_in * new_v / v;
    }
  }

  void Panel_EPD_ED2208::_dither_row_bayer_hsv(const bgr888_t* src, uint8_t* dst, uint_fast16_t w, uint_fast16_t y)
  {
    auto row_b = &bayer16[(y & 15) << 4];
    for (uint_fast16_t x = 0; x < w; x += 2) {
      int32_t r0, g0, b0;
      hsv_bias_pixel(src[x].r, src[x].g, src[x].b, (int32_t)row_b[x & 15], r0, g0, b0);
      uint8_t c0 = _rgb_to_epd_color(r0, g0, b0);

      uint8_t c1 = EPD_WHITE;
      if (x + 1 < w) {
        int32_t r1, g1, b1;
        hsv_bias_pixel(src[x+1].r, src[x+1].g, src[x+1].b, (int32_t)row_b[(x+1) & 15], r1, g1, b1);
        c1 = _rgb_to_epd_color(r1, g1, b1);
      }
      dst[x >> 1] = (c0 << 4) | c1;
    }
  }

  // --- Dither: Bayer Lab (L*-only bias, perceptually uniform) ---

  void Panel_EPD_ED2208::_dither_row_bayer_lab(const bgr888_t* src, uint8_t* dst, uint_fast16_t w, uint_fast16_t y)
  {
    auto row_b = &bayer16[(y & 15) << 4];
    for (uint_fast16_t x = 0; x < w; x += 2) {
      float L, a, b;
      int32_t r0, g0, b0;
      rgb_to_lab(src[x].r, src[x].g, src[x].b, L, a, b);
      L += ((float)row_b[x & 15]) / 4.0f;
      lab_to_rgb(L, a, b, r0, g0, b0);
      uint8_t c0 = _rgb_to_epd_color(r0, g0, b0);

      uint8_t c1 = EPD_WHITE;
      if (x + 1 < w) {
        rgb_to_lab(src[x + 1].r, src[x + 1].g, src[x + 1].b, L, a, b);
        L += ((float)row_b[(x + 1) & 15]) / 4.0f;
        int32_t r1, g1, b1;
        lab_to_rgb(L, a, b, r1, g1, b1);
        c1 = _rgb_to_epd_color(r1, g1, b1);
      }
      dst[x >> 1] = (c0 << 4) | c1;
    }
  }

  // --- Dither: IGN Lab (L*-only bias via Interleaved Gradient Noise) ---

  void Panel_EPD_ED2208::_dither_row_ign_lab(const bgr888_t* src, uint8_t* dst, uint_fast16_t w, uint_fast16_t y)
  {
    for (uint_fast16_t x = 0; x < w; x += 2) {
      float L, a, b;
      int32_t r0, g0, b0;
      rgb_to_lab(src[x].r, src[x].g, src[x].b, L, a, b);
      L += (ign(x, y) - 0.5f) * 64.0f;
      lab_to_rgb(L, a, b, r0, g0, b0);
      uint8_t c0 = _rgb_to_epd_color(r0, g0, b0);
      uint8_t c1 = EPD_WHITE;
      if (x + 1 < w) {
        rgb_to_lab(src[x + 1].r, src[x + 1].g, src[x + 1].b, L, a, b);
        L += (ign(x + 1, y) - 0.5f) * 64.0f;
        int32_t r1, g1, b1;
        lab_to_rgb(L, a, b, r1, g1, b1);
        c1 = _rgb_to_epd_color(r1, g1, b1);
      }
      dst[x >> 1] = (c0 << 4) | c1;
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

    dither_fn_t dither_fn;
    switch (_epd_mode) {
      case epd_mode_t::epd_fastest: dither_fn = _dither_row_bayer;     break;
      case epd_mode_t::epd_fast:    dither_fn = _dither_row_bayer_hsv; break;
      case epd_mode_t::epd_text:    dither_fn = _dither_row_bayer_lab; break;
      default:                      dither_fn = _dither_row_ign_lab;   break;
    }

    _bus->beginTransaction();
    cs_control(false);

    _send_command(0x10);    // Data start transmission

    for (uint_fast16_t y = 0; y < h; ++y) {
      uint8_t* dst = row_buf[y & 1];
      const bgr888_t* src = reinterpret_cast<const bgr888_t*>(_lines_buffer[y]);
      dither_fn(src, dst, w, y);
      _bus->writeBytes(dst, row_bytes, true, true);
    }
    _bus->wait();

    cs_control(true);
    _bus->endTransaction();

    if (row_buf[0]) heap_free(row_buf[0]);
    if (row_buf[1]) heap_free(row_buf[1]);
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
