#include "glyph_drawer.h"

#include <Arduino.h>
#include <math.h>

#include "config.h"

// Store Board LED Geometry in x100 scale to use faster integer math for calculations, is also used to draw glyphs in the Web UI.
const LedPoint LED_POSITIONS[LED_COUNT] = {
    {11389,9072},{11394,10501},{11394,11949},{11386,13364},{12621,14088},
    {13869,14812},{15117,15524},{16354,14814},{17611,14087},{18847,13373},
    {18847,11928},{18847,10509},{18844,9068},{17599,8347},{16355,7639},
    {15110,6917},{13869,7640},{12640,8352},{11999,9435},{12003,10145},
    {12011,12299},{12011,13007},{14501,14439},{15745,14461},{18234,13010},
    {18222,12283},{18222,10146},{18222,9420},{15745,7982},{14488,7991},
    {12634,9791},{12621,11223},{12634,12655},{13870,13384},{14492,13727},
    {15123,14080},{15733,13735},{16355,13376},{17599,12649},{17612,11224},
    {17599,9785},{16355,9058},{15745,8712},{15110,8340},{14488,8718},
    {13866,9072},{13256,10148},{13256,10869},{13243,11577},{13256,12297},
    {13866,11938},{14488,12304},{15110,12656},{15745,12297},{16355,11927},
    {16977,12292},{16977,11577},{16977,10850},{16990,10148},{16355,10488},
    {15745,10142},{15123,9783},{14488,10136},{13878,10502},{14501,10867},
    {14488,11575},{15745,11572},{15745,10857},{15110,11209},
};

// Logical nodes 0..10 mapped to PCB LED references
// 16,13,10,7,4,1,59,56,50,47,69 (zero-based below).
static constexpr uint8_t CORE_LED[11] = {15,12,9,6,3,0,58,55,49,46,68};
static constexpr float LINE_TOLERANCE = 190.0f;  // 1.90 mm at x100 scale

int8_t GlyphDrawer::decodeNode(char c) {
  if (c == 'a' || c == 'A') return 10;
  return c >= '0' && c <= '9' ? c - '0' : -1;
}

void GlyphDrawer::drawSegment(uint8_t a, uint8_t b, const Rgbw &color, Rgbw *output) {
  const LedPoint p1 = LED_POSITIONS[CORE_LED[a]];
  const LedPoint p2 = LED_POSITIONS[CORE_LED[b]];
  const float dx = p2.x - p1.x;
  const float dy = p2.y - p1.y;
  const float lengthSquared = dx * dx + dy * dy;

  for (size_t i = 0; i < LED_COUNT; ++i) {
    const float px = LED_POSITIONS[i].x - p1.x;
    const float py = LED_POSITIONS[i].y - p1.y;
    const float t = (px * dx + py * dy) / lengthSquared;
    if (t < -0.015f || t > 1.015f) continue;
    const float nearestX = p1.x + t * dx;
    const float nearestY = p1.y + t * dy;
    const float ex = LED_POSITIONS[i].x - nearestX;
    const float ey = LED_POSITIONS[i].y - nearestY;
    if ((ex * ex + ey * ey) <= LINE_TOLERANCE * LINE_TOLERANCE) {
      output[i] = color;
    }
  }
}

void GlyphDrawer::draw(const char *encodedPattern, const Rgbw &lineColor, const Rgbw &coreColor, Rgbw *output) const {
  for (size_t i = 0; i < LED_COUNT; ++i) output[i] = {};
  if (!encodedPattern) return;

  const size_t length = strlen(encodedPattern);
  for (size_t i = 0; i + 1 < length; i += 2) {
    const int8_t a = decodeNode(encodedPattern[i]);
    const int8_t b = decodeNode(encodedPattern[i + 1]);
    if (a < 0 || b < 0 || a == b) continue;
    drawSegment(a, b, lineColor, output);
    output[CORE_LED[a]] = coreColor;
    output[CORE_LED[b]] = coreColor;
  }
}

