#include "graphics_name_pet_screens.h"

#include "display.h"
#include "graphics.h"
#include "graphics_render_utils.h"
#include "graphics_ui_common.h"

#include "name_entry_state.h"
#include "pet.h"

void drawNamePetScreen(bool redrawBg)
{
  if (!isScreenOn())
    return;

  if (redrawBg)
    spr.fillSprite(TFT_BLACK);

  drawCenteredLine(g_namePetRenameMode ? "Rename Pet" : "Name Your Pet", 18, 2, 1);

  const char *name = (g_pendingPetName[0] != '\0') ? g_pendingPetName : "_";

  const int boxW = 200;
  const int boxH = 26;
  const int boxX = (screenW - boxW) / 2;
  const int boxY = 54;

  spr.fillRoundRect(boxX, boxY, boxW, boxH, 6, TFT_BLACK);
  spr.drawRoundRect(boxX, boxY, boxW, boxH, 6, uiModalOutline(pet.type));
  
  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(name, boxX + 8, boxY + 6);

  drawCenteredLine(g_namePetRenameMode ? "Edit name, press ENTER" : "Type name, press ENTER", screenH - 22, 1, 1);
}