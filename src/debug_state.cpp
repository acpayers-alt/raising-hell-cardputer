#include "debug_state.h"
#include "M5Cardputer.h"
#include <Arduino.h>
#include <type_traits>

bool g_debugEnabled = false;

bool dbgCanWrite(size_t needBytes)
{
  if (!g_debugEnabled)
    return false;
  if (!Serial)
    return false;
  return (Serial.availableForWrite() >= (int)needBytes);
}

// -----------------------------------------------------------------------------
// Keyboard debug traits (moved from app_loop_helpers)
// -----------------------------------------------------------------------------
template <typename T, typename = void> struct has_modifiers_field : std::false_type
{
};
template <typename T> struct has_modifiers_field<T, std::void_t<decltype(std::declval<T>().modifiers)>> : std::true_type
{
};

template <typename T, typename = void> struct has_modifier_field : std::false_type
{
};
template <typename T> struct has_modifier_field<T, std::void_t<decltype(std::declval<T>().modifier)>> : std::true_type
{
};

template <typename T, typename = void> struct has_data_size : std::false_type
{
};
template <typename T>
struct has_data_size<T, std::void_t<decltype(std::declval<T>().data()), decltype(std::declval<T>().size())>>
    : std::true_type
{
};

template <typename T> struct is_c_array : std::false_type
{
};
template <typename T, size_t N> struct is_c_array<T[N]> : std::true_type
{
};

template <typename T, typename = void> struct has_keys_arr : std::false_type
{
};
template <typename T> struct has_keys_arr<T, std::void_t<decltype(std::declval<T>().keys)>> : std::true_type
{
};

template <typename T, typename = void> struct has_key_arr : std::false_type
{
};
template <typename T> struct has_key_arr<T, std::void_t<decltype(std::declval<T>().key)>> : std::true_type
{
};

template <typename T, typename = void> struct has_keycode_arr : std::false_type
{
};
template <typename T> struct has_keycode_arr<T, std::void_t<decltype(std::declval<T>().keycode)>> : std::true_type
{
};

using std::decay_t;

// -----------------------------------------------------------------------------
// Debug port polling / keyboard debug (moved from app_loop_helpers)
// -----------------------------------------------------------------------------
void pollDebugPort()
{
  const uint32_t now = millis();

  // Don't even look at the port until we're armed.
  if (now < g_debugArmMs)
  {
    int drain = 8;
    while (drain-- > 0 && Serial.available() > 0)
      (void)Serial.read();
    return;
  }

  if (!Serial)
    return;

  const int MAX_BYTES_PER_TICK = 16;

  if (!g_debugEnabled)
  {
    int n = 0;
    while (n++ < MAX_BYTES_PER_TICK && Serial.available() > 0)
    {
      const int c = Serial.read();
      if (c < 0)
        break;

#if !(defined(PUBLIC_BUILD) && PUBLIC_BUILD)
      if (c == '!' || c == 'D' || c == 'd')
      {
        g_debugEnabled = true;
        if (Serial.availableForWrite() >= 48)
        {
          Serial.printf("[DBG] Debug enabled (recv '%c')\n", (char)c);
        }
        break;
      }
#endif
    }
    return;
  }

  int n = 0;
  while (n++ < MAX_BYTES_PER_TICK && Serial.available() > 0)
  {
    (void)Serial.read();
  }
}

void keyboardDebugTick()
{
  if (!g_debugEnabled)
    return;
  if (Serial.availableForWrite() < 64)
    return;

  static uint32_t nextMs = 0;
  const uint32_t now = millis();
  if (now < nextMs)
    return;
  nextMs = now + 250;

  auto ks = M5Cardputer.Keyboard.keysState();

  auto modsOf = [&](auto const &st) -> uint8_t
  {
    using K = decay_t<decltype(st)>;
    if constexpr (has_modifiers_field<K>::value)
      return (uint8_t)st.modifiers;
    else if constexpr (has_modifier_field<K>::value)
      return (uint8_t)st.modifier;
    else
      return 0;
  };

  auto keyArrayPtr = [&](auto const &st, int &n) -> const uint8_t *
  {
    using K = decay_t<decltype(st)>;
    n = 0;

    auto setFromMember = [&](auto const &member) -> const uint8_t *
    {
      using M = decay_t<decltype(member)>;
      if constexpr (has_data_size<M>::value)
      {
        n = (int)member.size();
        return (const uint8_t *)member.data();
      }
      else if constexpr (is_c_array<M>::value)
      {
        n = (int)(sizeof(member) / sizeof(member[0]));
        return (const uint8_t *)member;
      }
      else
      {
        return nullptr;
      }
    };

    if constexpr (has_keys_arr<K>::value)
      return setFromMember(st.keys);
    else if constexpr (has_key_arr<K>::value)
      return setFromMember(st.key);
    else if constexpr (has_keycode_arr<K>::value)
      return setFromMember(st.keycode);
    else
      return nullptr;
  };

  auto countNonZero = [&](auto const &st) -> int
  {
    int n = 0;
    const uint8_t *arr = keyArrayPtr(st, n);
    if (!arr || n <= 0)
      return 0;
    int c = 0;
    for (int i = 0; i < n; i++)
      if (arr[i])
        c++;
    return c;
  };

  const uint8_t mods = modsOf(ks);
  const uint8_t keysDown = (uint8_t)countNonZero(ks);

  static uint8_t lastMods = 0xFF;
  static uint8_t lastKeysDown = 0xFF;

  if (mods != lastMods || keysDown != lastKeysDown)
  {
    if (Serial.availableForWrite() >= 40)
    {
      Serial.printf("[KB] mods=0x%02X keys_down=%u\n", mods, (unsigned)keysDown);
    }
    lastMods = mods;
    lastKeysDown = keysDown;
  }
}
