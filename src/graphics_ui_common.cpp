#include "graphics_ui_common.h"

#include "display.h"
#include "graphics.h"

extern Pet pet;

uint16_t uiPillOutline(PetType t)
{
  switch (t)
  {
  case PET_ALIEN:
    return 0x07E0;

  case PET_ELDRITCH:
    return 0x0010;

  case PET_DEVIL:
  default:
    return 0xF800;
  }
}

uint16_t uiPillFillSelected(PetType t)
{
  switch (t)
  {
  case PET_DEVIL:
    return 0x2000; // dark red tint

  case PET_ELDRITCH:
    return 0x0008; // dark blue tint

  case PET_ALIEN:
    return 0x0200; // dark green tint
  }
}

uint16_t uiModalOutline(PetType t)
{
  switch (t)
  {
  case PET_ALIEN:
    return 0x07E0;

  case PET_ELDRITCH:
    return 0x001F;

  case PET_DEVIL:
  default:
    return 0xF800;
  }
}

void drawButton(int x, int y, int w, int h, const char *label, bool selected)
{
  const uint16_t outline = selected ? uiPillOutline(pet.type) : TFT_DARKGREY;
  const uint16_t fill = selected ? uiPillFillSelected(pet.type) : TFT_BLACK;
  const uint16_t textCol = selected ? TFT_WHITE : TFT_LIGHTGREY;

  spr.fillRoundRect(x, y, w, h, 8, fill);
  spr.drawRoundRect(x, y, w, h, 8, outline);

  spr.setTextDatum(MC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(textCol, fill);
  spr.drawString(label ? label : "", x + (w / 2), y + (h / 2));
  spr.setTextDatum(TL_DATUM);
}