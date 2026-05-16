#include "ui_state_photo_gallery.h"

#include <Arduino.h>
#include <SD.h>
#include <string.h>

#include "app_state.h"
#include "graphics.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"

namespace
{
constexpr int kMaxPhotos = 64;
constexpr int kVisibleRows = 5;
constexpr const char *kPhotoDir = "/raising_hell/photos";

struct PhotoEntry
{
  char name[64];
  char path[96];
};

PhotoEntry s_photos[kMaxPhotos];
int s_photoCount = 0;
int s_selected = 0;
int s_windowStart = 0;
bool s_viewing = false;

static bool isPngName(const char *name)
{
  if (!name)
    return false;

  const char *dot = strrchr(name, '.');
  return dot && strcasecmp(dot, ".png") == 0;
}

static void clampSelection()
{
  if (s_photoCount <= 0)
  {
    s_selected = 0;
    s_windowStart = 0;
    return;
  }

  if (s_selected < 0)
    s_selected = 0;
  if (s_selected >= s_photoCount)
    s_selected = s_photoCount - 1;

  if (s_windowStart > s_selected)
    s_windowStart = s_selected;
  if (s_selected >= s_windowStart + kVisibleRows)
    s_windowStart = s_selected - kVisibleRows + 1;
  if (s_windowStart < 0)
    s_windowStart = 0;
}

static void sortNewestFirst()
{
  for (int i = 0; i < s_photoCount - 1; ++i)
  {
    for (int j = i + 1; j < s_photoCount; ++j)
    {
      if (strcmp(s_photos[i].name, s_photos[j].name) < 0)
      {
        PhotoEntry tmp = s_photos[i];
        s_photos[i] = s_photos[j];
        s_photos[j] = tmp;
      }
    }
  }
}

static void reloadPhotos()
{
  s_photoCount = 0;

  if (!SD.exists(kPhotoDir))
  {
    clampSelection();
    return;
  }

  File dir = SD.open(kPhotoDir);
  if (!dir || !dir.isDirectory())
  {
    clampSelection();
    return;
  }

  while (s_photoCount < kMaxPhotos)
  {
    File f = dir.openNextFile();
    if (!f)
      break;

    if (!f.isDirectory())
    {
      const char *rawName = f.name();
      const char *base = strrchr(rawName, '/');
      base = base ? base + 1 : rawName;

      if (isPngName(base))
      {
        snprintf(s_photos[s_photoCount].name, sizeof(s_photos[s_photoCount].name), "%s", base);
        snprintf(s_photos[s_photoCount].path, sizeof(s_photos[s_photoCount].path), "%s/%s", kPhotoDir, base);
        ++s_photoCount;
      }
    }

    f.close();
  }

  dir.close();

  sortNewestFirst();
  clampSelection();
}

static void swallow(InputState &in)
{
  while (in.kbHasEvent())
    (void)in.kbPop();

  in.clearEdges();
  clearInputLatch();
}
} // namespace

void openPhotoGalleryFromTitle(InputState &in)
{
  uiActionEnterStateClean(UIState::PHOTO_GALLERY, Tab::TAB_PET, true, in, 120);
  requestFullUIRedraw();
}

void uiPhotoGalleryOnEnter(InputState &in)
{
  s_viewing = false;
  s_selected = 0;
  s_windowStart = 0;

  reloadPhotos();

  if (s_photoCount <= 0)
    ui_showMessage("No photos found");

  swallow(in);
  requestFullUIRedraw();
}

void uiPhotoGalleryHandle(InputState &in)
{
  if (s_viewing)
  {
    int move = 0;

    if (in.leftOnce || in.upOnce || in.encoderDelta < 0)
      move = -1;
    else if (in.rightOnce || in.downOnce || in.encoderDelta > 0)
      move = +1;

    if (move != 0 && s_photoCount > 0)
    {
      s_selected += move;

      if (s_selected < 0)
        s_selected = s_photoCount - 1;
      else if (s_selected >= s_photoCount)
        s_selected = 0;

      clampSelection();
      playBeep();
      requestFullUIRedraw();
      swallow(in);
      return;
    }

    if (in.escOnce || in.menuOnce || in.selectOnce || in.encoderPressOnce)
    {
      s_viewing = false;
      playBeep();
      requestFullUIRedraw();
      swallow(in);
      return;
    }

    while (in.kbHasEvent())
      (void)in.kbPop();

    return;
  }

  if (in.escOnce || in.menuOnce)
  {
    playBeep();
    uiActionEnterStateClean(UIState::TITLE_MENU, Tab::TAB_PET, true, in, 120);
    return;
  }

  int move = 0;
  if (in.upOnce || in.leftOnce || in.encoderDelta < 0)
    move = -1;
  if (in.downOnce || in.rightOnce || in.encoderDelta > 0)
    move = +1;

  if (move != 0 && s_photoCount > 0)
  {
    s_selected += move;

    if (s_selected < 0)
      s_selected = s_photoCount - 1;
    if (s_selected >= s_photoCount)
      s_selected = 0;

    clampSelection();
    playBeep();
    requestFullUIRedraw();
    swallow(in);
    return;
  }

  if (in.selectOnce || in.encoderPressOnce)
  {
    if (s_photoCount > 0)
    {
      s_viewing = true;
      playBeep();
      requestFullUIRedraw();
    }
    else
    {
      playBeep();
      ui_showMessage("No photos found");
      requestUIRedraw();
    }

    swallow(in);
    return;
  }

  while (in.kbHasEvent())
    (void)in.kbPop();
}

int photoGalleryCount() { return s_photoCount; }
int photoGallerySelected() { return s_selected; }
int photoGalleryWindowStart() { return s_windowStart; }

int photoGalleryVisibleCount()
{
  if (s_photoCount <= 0)
    return 0;

  int remaining = s_photoCount - s_windowStart;
  return remaining > kVisibleRows ? kVisibleRows : remaining;
}

const char *photoGalleryVisibleName(int visibleIndex)
{
  const int idx = s_windowStart + visibleIndex;
  if (idx < 0 || idx >= s_photoCount)
    return "";
  return s_photos[idx].name;
}

const char *photoGallerySelectedPath()
{
  if (s_selected < 0 || s_selected >= s_photoCount)
    return nullptr;
  return s_photos[s_selected].path;
}

bool photoGalleryViewingPhoto() { return s_viewing; }