#pragma once

#include <stdint.h>

enum class DisplayEffect : uint8_t { None, Fade };
enum class FadeDirection : uint8_t {
  CenterOutward, CenterInward, Down, Up, Left, Right, Global
};
enum class FadeType : uint8_t { Quickfade, Slowfade, Mix };

struct EffectSettings {
  DisplayEffect effect = DisplayEffect::None;
  FadeDirection direction = FadeDirection::CenterOutward;
  FadeType type = FadeType::Quickfade;
  uint32_t durationMs = 1000;
};

constexpr uint32_t MIN_FADE_DURATION_MS = 100;
constexpr uint32_t MAX_FADE_DURATION_MS = 60000;
