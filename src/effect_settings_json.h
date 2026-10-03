#pragma once

#include <string.h>

#include <ArduinoJson.h>
#include "effect_settings.h"

inline const char *effectName(DisplayEffect e) {
  return e == DisplayEffect::Fade ? "fade" : "none";
}

inline const char *fadeDirectionName(FadeDirection d) {
  switch (d) {
    case FadeDirection::CenterInward:
      return "center-inward";
    case FadeDirection::Down:
      return "down";
    case FadeDirection::Up:
      return "up";
    case FadeDirection::Left:
      return "left";
    case FadeDirection::Right:
      return "right";
    default:
      return "center-outward";
  }
}

inline const char *fadeTypeName(FadeType t) {
  switch (t) {
    case FadeType::Slowfade:
      return "slowfade";
    case FadeType::Mix:
      return "mix";
    default:
      return "quickfade";
  }
}

inline void effectSettingsToJson(JsonObject out, const EffectSettings &e) {
  out["effect"] = effectName(e.effect);
  out["direction"] = fadeDirectionName(e.direction);
  out["duration"] = e.durationMs;
  out["type"] = fadeTypeName(e.type);
}

inline bool effectSettingsFromJson(JsonObjectConst in, EffectSettings &out) {
  if (in.isNull() || !in["effect"].is<const char *>() ||
      !in["direction"].is<const char *>() ||
      !in["type"].is<const char *>() ||
      !in["duration"].is<uint32_t>()) {
    return false;
  }
  const char *effect = in["effect"] | "";
  const char *direction = in["direction"] | "";
  const char *type = in["type"] | "";
  if (!strcmp(effect, "none")) {
    out.effect = DisplayEffect::None;
  } else if (!strcmp(effect, "fade")) {
    out.effect = DisplayEffect::Fade;
  } else {
    return false;
  }
  if (!strcmp(direction, "center-outward")) {
    out.direction = FadeDirection::CenterOutward;
  } else if (!strcmp(direction, "center-inward")) {
    out.direction = FadeDirection::CenterInward;
  } else if (!strcmp(direction, "down")) {
    out.direction = FadeDirection::Down;
  } else if (!strcmp(direction, "up")) {
    out.direction = FadeDirection::Up;
  } else if (!strcmp(direction, "left")) {
    out.direction = FadeDirection::Left;
  } else if (!strcmp(direction, "right")) {
    out.direction = FadeDirection::Right;
  } else {
    return false;
  }
  if (!strcmp(type, "quickfade")) {
    out.type = FadeType::Quickfade;
  } else if (!strcmp(type, "slowfade")) {
    out.type = FadeType::Slowfade;
  } else if (!strcmp(type, "mix")) {
    out.type = FadeType::Mix;
  } else {
    return false;
  }
  out.durationMs = in["duration"].as<uint32_t>();
  return out.durationMs >= MIN_FADE_DURATION_MS &&
         out.durationMs <= MAX_FADE_DURATION_MS;
}
