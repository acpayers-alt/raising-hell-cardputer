#include "mini_games_internal.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "mg_pause_core.h"
#include "mini_game_assets.h"
#include "mini_game_return_ui.h"
#include "mini_game_runtime.h"
#include "mini_games.h"

#include "app_state.h"
#include "display.h"
#include "graphics.h"
#include "input.h"
#include "pet.h"
#include "save_manager.h"
#include "sdcard.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"

bool s_crossyShowIntro = false;
static const uint16_t kSpriteKey = 0x0841;

bool crossyIsShowingIntro() { return s_crossyShowIntro; }

static inline void crossyLogHeap(const char *tag) { mgAssetsLogHeap(tag); }

static void crossyLogState(const char *tag);

static const char *crossyGoalZonePathForPet();
static const char *crossyStartZonePathForPet();
static const char *crossyLavaZonePathForPet(uint8_t frame);
static const char *crossyStonePathForPet();
static bool sdExistsTrySlash(const char *path, const char **outUsePath = nullptr);

static inline void crossyExitMiniGameToReturnUi(bool beginLockout = true)
{
  crossyLogState("exit");
  mgmem::endSession();
  miniGameExitToReturnUi(beginLockout);
}

static bool sdExistsTrySlash(const char *path, const char **outUsePath)
{
  if (outUsePath)
    *outUsePath = path;
  if (!path || !path[0])
    return false;

  if (SD.exists(path))
  {
    if (outUsePath)
      *outUsePath = path;
    return true;
  }

  if (path[0] == '/')
  {
    const char *p2 = path + 1;
    if (SD.exists(p2))
    {
      if (outUsePath)
        *outUsePath = p2;
      return true;
    }
  }

  return false;
}

// -----------------------------------------------------------------------------
// CROSSY HELL GLOBALS (Frogger-style)
// -----------------------------------------------------------------------------
static const int kCrossyCols = 15;
static const int kCrossyRows = 7;

static const int kCrossyTileW = 16;
static const int kCrossyTileH = 19;

static const int kCrossyOriginX = 0;
static const int kCrossyOriginY = 1;

static uint8_t s_crossyLavaFrame = 0;
static uint32_t s_crossyLavaAnimMs = 0;
static uint8_t s_crossyLandingGraceFrames = 0;

static uint32_t s_crossyWinPoseStart = 0;
static bool s_crossyWinPoseActive = false;

struct CrossyStar
{
  int16_t x;
  int16_t y;
  uint8_t phase;
  uint8_t kind;
};

static constexpr int kCrossyStarCount = 28;
static CrossyStar s_crossyStars[kCrossyStarCount];

enum CrossyLaneType : uint8_t
{
  CROSSY_LANE_SAFE = 0,
  CROSSY_LANE_ROAD,
  CROSSY_LANE_WATER,
  CROSSY_LANE_GOAL
};

struct CrossyLane
{
  CrossyLaneType type;
  int8_t dir;
  uint8_t speed;
  uint8_t moverLen;
  uint16_t gapPx;
  int32_t offsetPx;
  uint8_t platSize;
};

enum CrossyFacing : uint8_t
{
  CROSSY_FACE_DOWN = 0,
  CROSSY_FACE_UP,
  CROSSY_FACE_LEFT,
  CROSSY_FACE_RIGHT
};

enum CrossyPlatformSize
{
  CROSSY_PLAT_LARGE = 0,
  CROSSY_PLAT_SMALL,
  CROSSY_PLAT_XS
};

static CrossyLane s_crossyLanes[kCrossyRows];

static int s_crossyPx = 0;
static int s_crossyPy = 0;
static int s_crossyVisualOffsetPx = 0;
static CrossyFacing s_crossyFacing = CROSSY_FACE_DOWN;

static bool s_crossyInited = false;
uint32_t s_crossyLastLaneMs = 0;

static void crossyLogState(const char *tag)
{
  Serial.printf("[CROSSY] %s px=%d py=%d voff=%d facing=%d intro=%d winPose=%d gameOver=%d free=%u largest=%u\n",
                tag ? tag : "state", s_crossyPx, s_crossyPy, s_crossyVisualOffsetPx, (int)s_crossyFacing,
                s_crossyShowIntro ? 1 : 0, s_crossyWinPoseActive ? 1 : 0, g_app.gameOver ? 1 : 0,
                (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static M5Canvas *s_crossyLava0 = nullptr;
static M5Canvas *s_crossyLava1 = nullptr;

static const char *crossyStoneSmallPathForPet();
static const char *crossyStoneXSPathForPet();
static const char *crossyImpPathForPet(CrossyFacing facing);

static bool ensureCrossyStartZoneSprite();
static bool ensureCrossyGoalZoneSprite();
static bool ensureCrossyLavaZoneSprite(uint8_t frame);
static bool ensureCrossyStoneSprite();
static bool ensureCrossyStoneSmallSprite();
static bool ensureCrossyStoneXSSprite();

static bool crossyRowIsWater(int row) { return row >= 1 && row <= 5; }

static bool crossyRowIsGoal(int row) { return row == 0; }

static bool crossyPlayerOverlapsMoverInRow(int row);

static bool crossyPlayerOverlapsMoverInRow(int row)
{
  if (row < 0 || row >= kCrossyRows)
    return false;

  const CrossyLane &L = s_crossyLanes[row];
  if (L.type != CROSSY_LANE_WATER)
    return false;

  const int playerLeft = s_crossyPx * kCrossyTileW + s_crossyVisualOffsetPx;
  const int playerRight = playerLeft + kCrossyTileW - 1;
  const int playerCenter = playerLeft + (kCrossyTileW / 2);

  const int moverLenPx = (int)L.moverLen * kCrossyTileW;
  const int periodPx = moverLenPx + (int)L.gapPx;
  const int laneW = kCrossyCols * kCrossyTileW;

  if (moverLenPx <= 0 || periodPx <= 0)
    return false;

  int offset = (int)(L.offsetPx % periodPx);
  if (offset < 0)
    offset += periodPx;

  for (int x0 = -periodPx * 2; x0 < laneW + periodPx * 2; x0 += periodPx)
  {
    const int start = x0 - offset;
    const int end = start + moverLenPx - 1;

    if (playerCenter >= start && playerCenter <= end)
      return true;

    if (playerLeft >= start && playerLeft <= end)
      return true;

    if (playerRight >= start && playerRight <= end)
      return true;

    if (playerLeft <= start && playerRight >= end)
      return true;
  }

  return false;
}

static bool ensureCrossyGoalZoneSprite()
{
  M5Canvas *sprPtr = nullptr;
  return mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "goal_zone", crossyGoalZonePathForPet(), 8, kSpriteKey, sprPtr);
}

static bool ensureCrossyStartZoneSprite()
{
  M5Canvas *sprPtr = nullptr;
  return mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "start_zone", crossyStartZonePathForPet(), 8, kSpriteKey, sprPtr);
}

static bool ensureCrossyLavaZoneSprite(uint8_t frame)
{
  if (pet.type == PET_ELDRITCH)
    return false;

  const uint8_t i = frame & 1;
  const char *assetId = (i == 0) ? "lava_zone_0" : "lava_zone_1";

  M5Canvas *&target = (i == 0) ? s_crossyLava0 : s_crossyLava1;
  return mgmem::ensureSprite(MiniGame::CROSSY_ROAD, assetId, crossyLavaZonePathForPet(i), 16, TFT_BLACK, target);
}

static void crossyInitStars()
{
  const int gW = kCrossyCols * kCrossyTileW;
  const int gH = kCrossyRows * kCrossyTileH;

  for (int i = 0; i < kCrossyStarCount; ++i)
  {
    s_crossyStars[i].x = (int16_t)random(0, gW);
    s_crossyStars[i].y = (int16_t)random(0, gH);
    s_crossyStars[i].phase = (uint8_t)random(0, 64);
    s_crossyStars[i].kind = (uint8_t)random(0, 4);
  }
}

static void crossyInitLanes()
{
  memset(s_crossyLanes, 0, sizeof(s_crossyLanes));

  for (int r = 0; r < kCrossyRows; ++r)
  {
    CrossyLane &L = s_crossyLanes[r];

    if (crossyRowIsGoal(r))
    {
      L.type = CROSSY_LANE_GOAL;
      L.dir = 0;
      L.speed = 0;
      L.moverLen = 0;
      L.gapPx = 0;
      L.offsetPx = 0;
      L.platSize = CROSSY_PLAT_LARGE;
      continue;
    }

    if (crossyRowIsWater(r))
    {
      L.type = CROSSY_LANE_WATER;
      L.dir = (r % 2 == 1) ? -1 : +1;

      switch (r)
      {
      case 1: // top water row
        L.speed = 4;
        L.platSize = CROSSY_PLAT_XS;
        L.moverLen = 2;
        L.gapPx = kCrossyTileW * 5;
        break;

      case 2:
        L.speed = 3;
        L.platSize = CROSSY_PLAT_LARGE;
        L.moverLen = 3;
        L.gapPx = kCrossyTileW * 4;
        break;

      case 3:
        L.speed = 4;
        L.platSize = CROSSY_PLAT_SMALL;
        L.moverLen = 2;
        L.gapPx = kCrossyTileW * 5;
        break;

      case 4:
        L.speed = 3;
        L.platSize = CROSSY_PLAT_LARGE;
        L.moverLen = 3;
        L.gapPx = kCrossyTileW * 3;
        break;

      case 5: // bottom water row
      default:
        L.speed = 2;
        L.platSize = CROSSY_PLAT_SMALL;
        L.moverLen = 2;
        L.gapPx = kCrossyTileW * 4;
        break;
      }

      const int moverLenPx = (int)L.moverLen * kCrossyTileW;
      const int periodPx = moverLenPx + (int)L.gapPx;
      L.offsetPx = (periodPx > 0) ? (int32_t)random((long)periodPx) : 0;
      continue;
    }

    L.type = CROSSY_LANE_SAFE;
    L.dir = 0;
    L.speed = 0;
    L.moverLen = 0;
    L.gapPx = 0;
    L.offsetPx = 0;
    L.platSize = CROSSY_PLAT_LARGE;
  }
}

void freeCrossyZoneSprites()
{
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "goal_zone");
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "start_zone");
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "intro_goal");
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "lava_zone_0");
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "lava_zone_1");

  s_crossyLava0 = nullptr;
  s_crossyLava1 = nullptr;
}

static const char *crossyIntroLine1ForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "Escape the Moon! Use Arrow Keys";
  case PET_DEVIL:
  default:
    return "Escape Hell! Use Arrow Keys";
  }
}

static const char *crossyIntroLine2ForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "Cross the cosmos, enter the portal!";
  case PET_DEVIL:
  default:
    return "Avoid Lava, Exit between Torches!";
  }
}

static const char *crossyIntroGoalPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/crossy/eld/intro_goal.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/crossy/dev/intro_goal.png";
  }
}

static const char *crossyStartZonePathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/crossy/eld/eld_start_zone.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/crossy/dev/dev_start_zone.png";
  }
}

static const char *crossyGoalZonePathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/crossy/eld/eld_goal_zone.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/crossy/dev/dev_goal_zone.png";
  }
}

static const char *crossyLavaZonePathForPet(uint8_t frame)
{
  return (frame & 1) ? "/raising_hell/graphics/mini_games/crossy/dev/dev_lava_zone2.png"
                     : "/raising_hell/graphics/mini_games/crossy/dev/dev_lava_zone1.png";
}

void freeCrossyActorSprites()
{
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "stone_lg");
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "stone_sm");
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "stone_xs");
}

static bool ensureCrossyStoneSprite()
{
  M5Canvas *sprPtr = nullptr;
  return mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "stone_lg", crossyStonePathForPet(), 8, kSpriteKey, sprPtr);
}

static bool ensureCrossyStoneSmallSprite()
{
  M5Canvas *sprPtr = nullptr;
  return mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "stone_sm", crossyStoneSmallPathForPet(), 8, kSpriteKey, sprPtr);
}

static bool ensureCrossyStoneXSSprite()
{
  M5Canvas *sprPtr = nullptr;
  return mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "stone_xs", crossyStoneXSPathForPet(), 8, kSpriteKey, sprPtr);
}

static const char *crossyImpPathForPet(CrossyFacing facing)
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    switch (facing)
    {
    case CROSSY_FACE_UP:
      return "/raising_hell/graphics/mini_games/crossy/eld/cult_up.png";
    case CROSSY_FACE_LEFT:
      return "/raising_hell/graphics/mini_games/crossy/eld/cult_left.png";
    case CROSSY_FACE_RIGHT:
      return "/raising_hell/graphics/mini_games/crossy/eld/cult_right.png";
    case CROSSY_FACE_DOWN:
    default:
      return "/raising_hell/graphics/mini_games/crossy/eld/cult_down.png";
    }

  case PET_DEVIL:
  default:
    switch (facing)
    {
    case CROSSY_FACE_UP:
      return "/raising_hell/graphics/mini_games/crossy/dev/imp_up.png";
    case CROSSY_FACE_LEFT:
      return "/raising_hell/graphics/mini_games/crossy/dev/imp_left.png";
    case CROSSY_FACE_RIGHT:
      return "/raising_hell/graphics/mini_games/crossy/dev/imp_right.png";
    case CROSSY_FACE_DOWN:
    default:
      return "/raising_hell/graphics/mini_games/crossy/dev/imp_down.png";
    }
  }
}

static const char *crossyStoneSmallPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/crossy/eld/stone_chunk_sm.png";

  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/crossy/dev/stone_chunk_sm.png";
  }
}

static const char *crossyStoneXSPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/crossy/eld/stone_chunk_xs.png";

  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/crossy/dev/stone_chunk_xs.png";
  }
}

static const char *crossyStonePathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/crossy/eld/stone_chunk.png";

  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/crossy/dev/stone_chunk.png";
  }
}

static void drawCrossyEldritchStarsForRow(int y, int rowH, uint32_t now)
{
  const int laneTop = y;
  const int laneBottom = y + rowH;
  const uint32_t tick = now / 90U;
  const int gW = kCrossyCols * kCrossyTileW;

  for (int i = 0; i < kCrossyStarCount; ++i)
  {
    const CrossyStar &s = s_crossyStars[i];

    if (s.y < laneTop || s.y >= laneBottom)
      continue;

    const uint8_t t = (uint8_t)((tick + s.phase) & 31U);
    const uint8_t glow = (t < 16U) ? t : (31U - t);

    if (glow < 2)
      continue;

    uint16_t c;
    if (glow < 5)
      c = spr.color565(80, 80, 96);
    else if (glow < 9)
      c = spr.color565(150, 150, 180);
    else
      c = spr.color565(235, 235, 255);

    const int sx = s.x;
    const int sy = s.y;

    if (sx < 0 || sx >= gW)
      continue;

    spr.drawPixel(sx, sy, c);

    if (s.kind >= 1 && glow >= 8)
    {
      if (sx > 0)
        spr.drawPixel(sx - 1, sy, c);
      if (sx + 1 < gW)
        spr.drawPixel(sx + 1, sy, c);
    }

    if (s.kind >= 2 && glow >= 8)
    {
      if (sy > laneTop)
        spr.drawPixel(sx, sy - 1, c);
      if (sy + 1 < laneBottom)
        spr.drawPixel(sx, sy + 1, c);
    }
  }
}

static void drawCrossyStoneChunk(int x, int y, int w, int h, uint8_t platSize)
{
  M5Canvas *stone = nullptr;

  if (platSize == CROSSY_PLAT_XS)
  {
    if (ensureCrossyStoneXSSprite())
      mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "stone_xs", crossyStoneXSPathForPet(), 8, kSpriteKey, stone);
  }
  else if (platSize == CROSSY_PLAT_SMALL)
  {
    if (ensureCrossyStoneSmallSprite())
      mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "stone_sm", crossyStoneSmallPathForPet(), 8, kSpriteKey, stone);
  }
  else
  {
    if (ensureCrossyStoneSprite())
      mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "stone_lg", crossyStonePathForPet(), 8, kSpriteKey, stone);
  }

  if (stone && stone->width() > 0 && stone->height() > 0)
  {
    const int drawX = x + (w - (int)stone->width()) / 2;
    const int drawY = y + (h - (int)stone->height()) / 2;
    stone->pushSprite(&spr, drawX, drawY, kSpriteKey);
    return;
  }

  spr.fillRoundRect(x, y + 2, w, h - 4, 3, TFT_DARKGREY);
  spr.drawFastHLine(x + 2, y + 4, w - 4, TFT_LIGHTGREY);
  spr.drawFastHLine(x + 3, y + h - 4, w - 6, TFT_BLACK);
}

static void drawCrossyImp(int x, int y, int w, int h, uint32_t now)
{
  const char *path = crossyImpPathForPet(s_crossyFacing);
  const char *usePath = nullptr;

  if (sdExistsTrySlash(path, &usePath))
  {
    sprDrawPngFromSD(usePath ? usePath : path, x, y);
    return;
  }

  const int bob = ((now / 120) % 2 == 0) ? 0 : 1;

  spr.fillRect(x + 5, y + 5 + bob, w - 10, h - 9, TFT_RED);
  spr.fillTriangle(x + 6, y + 6 + bob, x + 8, y + 1 + bob, x + 10, y + 6 + bob, TFT_RED);
  spr.fillTriangle(x + w - 10, y + 6 + bob, x + w - 8, y + 1 + bob, x + w - 6, y + 6 + bob, TFT_RED);

  spr.fillRect(x + 7, y + 8 + bob, 2, 2, TFT_BLACK);
  spr.fillRect(x + w - 9, y + 8 + bob, 2, 2, TFT_BLACK);

  spr.drawFastHLine(x + 8, y + h - 6 + bob, w - 16, TFT_YELLOW);
}

static bool crossyGoalEntryAllowed(int px)
{
  const int playerCenterX = kCrossyOriginX + px * kCrossyTileW + (kCrossyTileW / 2);

  const int leftTorchX = 85;
  const int rightTorchX = 85 + 68;

  return (playerCenterX >= leftTorchX && playerCenterX <= rightTorchX);
}

static void crossyReset()
{
  s_crossyPx = kCrossyCols / 2;
  s_crossyPy = kCrossyRows - 1;
  s_crossyVisualOffsetPx = 0;

  s_crossyFacing = CROSSY_FACE_DOWN;

  s_crossyLastLaneMs = millis();
  s_crossyLavaFrame = 0;
  s_crossyLavaAnimMs = millis();
  s_crossyLandingGraceFrames = 0;

  crossyInitLanes();
  crossyInitStars();
}

void startCrossyRoad()
{
  mgPauseReset();
  inputSetTextCapture(false);

  g_app.inMiniGame = true;
  g_app.gameOver = false;
  playerWon = false;
  s_resultShown = false;

  s_crossyShowIntro = true;

  mgClearRewardState();
  mgResetAcceptState();

  currentMiniGame = MiniGame::CROSSY_ROAD;
  graphicsReleaseUiCachesForMiniGame();
  mgAssetsBeginSession(currentMiniGame, "startCrossyRoad");
  mgmem::beginSession(currentMiniGame, pet.type);
  mgmem::logUsage("crossy-start-beginSession");

  UIState retUi = g_app.uiState;
  if (retUi == UIState::MINI_GAME || retUi == UIState::MG_PAUSE)
    retUi = UIState::PET_SCREEN;

  miniGameSetReturnUi(retUi, g_app.currentTab);
  uiActionEnterState(UIState::MINI_GAME, g_app.currentTab, false);

  s_crossyInited = true;
  crossyReset();

  mgmem::logUsage("crossy-before-asset-free");

  freeCrossyZoneSprites();
  freeCrossyActorSprites();

  s_crossyLava0 = nullptr;
  s_crossyLava1 = nullptr;

  mgmem::logUsage("crossy-after-asset-free");

  const bool startOk = ensureCrossyStartZoneSprite();
  mgmem::logUsage("crossy-after-start-zone");

  const bool goalOk = ensureCrossyGoalZoneSprite();
  mgmem::logUsage("crossy-after-goal-zone");

  bool lava0 = false;
  bool lava1 = false;
  const bool usesLavaSprites = (pet.type != PET_ELDRITCH);

  if (usesLavaSprites)
  {
    lava0 = ensureCrossyLavaZoneSprite(0);
    mgmem::logUsage("crossy-after-lava0");

    lava1 = ensureCrossyLavaZoneSprite(1);
    mgmem::logUsage("crossy-after-lava1");
  }
  else
  {
    mgmem::logUsage("crossy-after-lava-skip");
  }

  const bool stoneOk = ensureCrossyStoneSprite();
  mgmem::logUsage("crossy-after-stone-lg");

  const bool stoneSmOk = ensureCrossyStoneSmallSprite();
  mgmem::logUsage("crossy-after-stone-sm");

  const bool stoneXsOk = ensureCrossyStoneXSSprite();
  mgmem::logUsage("crossy-after-stone-xs");

  const bool impOk = true;
  mgmem::logUsage("crossy-after-imp");

  Serial.printf("[CROSSY] preload start=%d goal=%d lava=%s stone=%d stoneSm=%d stoneXs=%d imp=%d free=%u largest=%u\n",
                startOk ? 1 : 0, goalOk ? 1 : 0, usesLavaSprites ? ((lava0 && lava1) ? "ok" : "fail") : "skip",
                stoneOk ? 1 : 0, stoneSmOk ? 1 : 0, stoneXsOk ? 1 : 0, impOk ? 1 : 0, (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  invalidateBackgroundCache();
  requestUIRedraw();
  clearInputLatch();
  inputForceClear();
  mgBeginInputLockout(220);

  s_crossyWinPoseActive = false;
  s_crossyWinPoseStart = 0;

  mgmem::logUsage("crossy-start-complete");
  crossyLogState("start-complete");
}

static void crossyStepLanes(uint32_t now)
{
  const uint32_t kLaneStepMs = 16;
  int steps = 0;
  const int kMaxStepsPerCall = 2;

  while ((uint32_t)(now - s_crossyLastLaneMs) >= kLaneStepMs && steps < kMaxStepsPerCall)
  {
    s_crossyLastLaneMs += kLaneStepMs;
    steps++;

    for (int r = 0; r < kCrossyRows; ++r)
    {
      CrossyLane &L = s_crossyLanes[r];
      if (L.type != CROSSY_LANE_WATER)
        continue;

      const int movePx = (int)L.speed;
      if (movePx <= 0)
        continue;

      const bool carryFrog = (s_crossyPy == r) && crossyPlayerOverlapsMoverInRow(r);

      const int deltaPx = (L.dir > 0) ? movePx : -movePx;
      L.offsetPx += deltaPx;
      const int moverLenPx = L.moverLen * kCrossyTileW;
      const int periodPx = moverLenPx + L.gapPx;

      L.offsetPx %= periodPx;
      if (L.offsetPx < 0)
        L.offsetPx += periodPx;

      if (carryFrog)
      {
        s_crossyVisualOffsetPx -= deltaPx;

        while (s_crossyVisualOffsetPx <= -kCrossyTileW)
        {
          s_crossyPx -= 1;
          s_crossyVisualOffsetPx += kCrossyTileW;
        }

        while (s_crossyVisualOffsetPx >= kCrossyTileW)
        {
          s_crossyPx += 1;
          s_crossyVisualOffsetPx -= kCrossyTileW;
        }
      }
    }
  }

  if ((uint32_t)(now - s_crossyLastLaneMs) >= (kLaneStepMs * 8))
    s_crossyLastLaneMs = now;
}

void updateCrossyRoad(const InputState &input)
{
  const bool enterOnce = miniGameEnterOnce(input);
  const uint32_t now = millis();

  if (pet.type != PET_ELDRITCH)
  {
    if ((uint32_t)(now - s_crossyLavaAnimMs) >= 180)
    {
      s_crossyLavaAnimMs = now;
      s_crossyLavaFrame ^= 1;
    }
  }

  if (mgRewardShowing())
  {
    const uint32_t now = millis();
    if ((enterOnce && !mgInputLockedOut()) || mgRewardAutoDismissNow(now))
    {
      mgClearRewardState();
      mgResetAcceptState();
      crossyExitMiniGameToReturnUi(true);
    }
    return;
  }

  if (g_app.gameOver)
  {
    mgApplyResultAndShowReward(playerWon);
    mgResetAcceptState();
    mgBeginInputLockout(180);
    clearInputLatch();
    inputForceClear();
    return;
  }

  if (!s_crossyInited)
  {
    s_crossyInited = true;
    crossyReset();
  }

  if (s_crossyShowIntro)
  {
    if (input.mgQuitOnce && !mgInputLockedOut())
    {
      miniGameCancelFromIntro();
      return;
    }

    const bool startPressed = enterOnce || input.mgSelectOnce || input.mgUpOnce;

    if (startPressed && !mgInputLockedOut())
    {
      s_crossyShowIntro = false;
      crossyLogState("intro-dismissed");
      soundConfirm();
      clearInputLatch();
      inputForceClear();
      mgBeginInputLockout(120);
      requestUIRedraw();
    }

    return;
  }

  if (s_crossyWinPoseActive)
  {
    s_crossyVisualOffsetPx = ((millis() / 120) % 2) ? -2 : 0;

    if ((uint32_t)(now - s_crossyWinPoseStart) >= 800)
    {
      s_crossyVisualOffsetPx = 0;
      s_crossyWinPoseActive = false;
      playerWon = true;
      g_app.gameOver = true;
      crossyLogState("win");
      requestUIRedraw();
      s_resultShown = true;
    }
    return;
  }

  crossyStepLanes(now);

  bool moved = false;
  bool movedSfxPending = false;

  if (!mgInputLockedOut())
  {
    if (input.mgLeftOnce && s_crossyPx > 0)
    {
      s_crossyPx--;
      s_crossyFacing = CROSSY_FACE_LEFT;
      s_crossyVisualOffsetPx = 0;
      moved = true;
    }
    else if (input.mgRightOnce && s_crossyPx < (kCrossyCols - 1))
    {
      s_crossyPx++;
      s_crossyFacing = CROSSY_FACE_RIGHT;
      s_crossyVisualOffsetPx = 0;
      moved = true;
    }
    else if (input.mgUpOnce && s_crossyPy > 0)
    {
      s_crossyPy--;
      s_crossyFacing = CROSSY_FACE_UP;
      s_crossyVisualOffsetPx = 0;
      moved = true;
    }
    else if (input.mgDownOnce && s_crossyPy < (kCrossyRows - 1))
    {
      s_crossyPy++;
      s_crossyFacing = CROSSY_FACE_DOWN;
      s_crossyVisualOffsetPx = 0;
      moved = true;
    }

    if (moved)
    {
      movedSfxPending = true;
      requestUIRedraw();
    }
  }

  if (s_crossyLandingGraceFrames > 0)
    s_crossyLandingGraceFrames--;

  if (crossyRowIsGoal(s_crossyPy))
  {
    if (crossyGoalEntryAllowed(s_crossyPx))
    {
      s_crossyWinPoseActive = true;
      s_crossyWinPoseStart = now;
      s_crossyVisualOffsetPx = 0;
      crossyLogState("goal-reached");
      soundWin();
      requestUIRedraw();
      return;
    }
    else
    {
      playerWon = false;
      g_app.gameOver = true;
      crossyLogState("goal-blocked-lose");
      soundError();
      requestUIRedraw();
      s_resultShown = true;
      return;
    }
  }

  if (crossyRowIsWater(s_crossyPy))
  {
    if (!crossyPlayerOverlapsMoverInRow(s_crossyPy))
    {
      if (s_crossyLandingGraceFrames == 0)
      {
        playerWon = false;
        g_app.gameOver = true;
        crossyLogState("water-lose");
        soundError();
        requestUIRedraw();
        s_resultShown = true;
        return;
      }
    }
    else
    {
      s_crossyLandingGraceFrames = 1;
    }
  }
  if (movedSfxPending)
  {
    soundMenuTick();
  }
}

void drawCrossyRoad()
{
  if (!s_crossyInited)
  {
    s_crossyInited = true;
    crossyReset();
  }

  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;

  const bool haveGoal = ensureCrossyGoalZoneSprite();
  const bool haveStart = ensureCrossyStartZoneSprite();

  M5Canvas *goalZoneSpr = nullptr;
  M5Canvas *startZoneSpr = nullptr;

  if (haveGoal)
  {
    mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "goal_zone", crossyGoalZonePathForPet(), 8, kSpriteKey, goalZoneSpr);
  }

  if (haveStart)
  {
    mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "start_zone", crossyStartZonePathForPet(), 8, kSpriteKey, startZoneSpr);
  }

  M5Canvas *lavaZoneSpr0 = s_crossyLava0;
  M5Canvas *lavaZoneSpr1 = s_crossyLava1;

  spr.fillSprite(TFT_BLACK);

  if (mgRewardShowing())
  {
    miniGameDrawRewardModal(gW, gH);
    return;
  }

  if (s_crossyShowIntro)
  {
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(CC_DATUM);

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString(crossyIntroLine1ForPet(), gW / 2, 8, 2);
    spr.drawCentreString(crossyIntroLine2ForPet(), gW / 2, 26, 2);

    const char *introPath = crossyIntroGoalPathForPet();

    int iw = 0, ih = 0;
    const char *useIntroPath = nullptr;

    if (mgAssetsReadPngDims(introPath, &iw, &ih, &useIntroPath))
    {
      const int ix = (gW - iw) / 2;
      const int iy = 60;
      sprDrawPngFromSD(useIntroPath ? useIntroPath : introPath, ix, iy);
    }
    else
    {
      sprDrawPngFromSD(introPath, (gW - 68) / 2, 60);
    }

    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.drawCentreString("ENTER to begin", gW / 2, 120, 2);
    return;
  }

  const uint32_t now = millis();

  for (int row = 0; row < kCrossyRows; ++row)
  {
    const int y = kCrossyOriginY + row * kCrossyTileH;

    switch (s_crossyLanes[row].type)
    {
    case CROSSY_LANE_GOAL:
      if (haveGoal && goalZoneSpr)
        goalZoneSpr->pushSprite(&spr, 0, y);
      else
        spr.fillRect(0, y, gW, kCrossyTileH, TFT_RED);
      break;

    case CROSSY_LANE_SAFE:
      if (row == kCrossyRows - 1)
      {
        if (haveStart && startZoneSpr)
          startZoneSpr->pushSprite(&spr, 0, y);
        else
          spr.fillRect(0, y, 240, kCrossyTileH, TFT_DARKGREY);
      }
      else
      {
        spr.fillRect(0, y, 240, kCrossyTileH, TFT_BLACK);
      }
      break;

    case CROSSY_LANE_WATER:
    {
      if (pet.type == PET_ELDRITCH)
      {
        spr.fillRect(0, y, gW, kCrossyTileH, TFT_BLACK);
        drawCrossyEldritchStarsForRow(y, kCrossyTileH, millis());
      }
      else
      {
        M5Canvas *bgSpr = nullptr;
        const uint8_t lavaFrame = (s_crossyLavaFrame + row) & 1;
        bgSpr = (lavaFrame == 0) ? s_crossyLava0 : s_crossyLava1;

        if (bgSpr && bgSpr->width() > 0 && bgSpr->height() > 0)
        {
          const int tileW = (int)bgSpr->width();
          for (int x = 0; x < gW; x += tileW)
            bgSpr->pushSprite(&spr, x, y);
        }
        else
        {
          spr.fillRect(0, y, gW, kCrossyTileH, TFT_BLACK);
        }
      }

      break;
    }
    }
  }

  // draw movers ONCE, after all lane backgrounds
  for (int r = 0; r < kCrossyRows; ++r)
  {
    const CrossyLane &L = s_crossyLanes[r];
    if (L.type != CROSSY_LANE_WATER)
      continue;

    const int moverLenPx = (int)L.moverLen * kCrossyTileW;
    const int periodPx = moverLenPx + (int)L.gapPx;
    const int laneW = kCrossyCols * kCrossyTileW;

    if (moverLenPx <= 0 || periodPx <= 0)
      continue;

    const int y = kCrossyOriginY + r * kCrossyTileH;

    int offset = (int)(L.offsetPx % periodPx);
    if (offset < 0)
      offset += periodPx;

    for (int x0 = -periodPx * 2; x0 < laneW + periodPx * 2; x0 += periodPx)
    {
      const int x = x0 - offset;
      const int drawX = kCrossyOriginX + x;

      if (drawX + moverLenPx < kCrossyOriginX)
        continue;
      if (drawX > kCrossyOriginX + laneW)
        continue;

      drawCrossyStoneChunk(drawX, y + 2, moverLenPx - 1, kCrossyTileH - 4, L.platSize);
    }
  }
  const int playerX = kCrossyOriginX + s_crossyPx * kCrossyTileW + s_crossyVisualOffsetPx;
  const int playerY = kCrossyOriginY + s_crossyPy * kCrossyTileH - 8;

  drawCrossyImp(playerX, playerY, kCrossyTileW, kCrossyTileH, now);
}