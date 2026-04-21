#include "graphics_bio_status_paths.h"

#include "pet.h"

const char *getBioStatusImagePath()
{
  const PetMood mood = petResolveMood(pet);

  // --------------------------------------------------------------------------
  // DEVIL BIOS
  // --------------------------------------------------------------------------
  if (pet.type == PET_DEVIL)
  {
    // ---------------- BABY ----------------
    if (pet.evoStage == 0)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/dev/bb/dev_bb_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/dev/bb/dev_bb_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/dev/bb/dev_bb_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/dev/bb/dev_bb_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/dev/bb/dev_bb_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/dev/bb/dev_bb_hpy_bio.png";
      }
    }

    // ---------------- TEEN ----------------
    if (pet.evoStage == 1)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/dev/tn/dev_tn_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/dev/tn/dev_tn_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/dev/tn/dev_tn_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/dev/tn/dev_tn_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/dev/tn/dev_tn_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/dev/tn/dev_tn_hpy_bio.png";
      }
    }

    // ---------------- ADULT ----------------
    if (pet.evoStage == 2)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/dev/ad/dev_ad_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/dev/ad/dev_ad_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/dev/ad/dev_ad_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/dev/ad/dev_ad_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/dev/ad/dev_ad_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/dev/ad/dev_ad_hpy_bio.png";
      }
    }

    // ---------------- ELDER ----------------
    if (pet.evoStage >= 3)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/dev/ed/dev_ed_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/dev/ed/dev_ed_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/dev/ed/dev_ed_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/dev/ed/dev_ed_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/dev/ed/dev_ed_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/dev/ed/dev_ed_hpy_bio.png";
      }
    }
  }

  // --------------------------------------------------------------------------
  // ELDRITCH BIOS
  // --------------------------------------------------------------------------
  if (pet.type == PET_ELDRITCH)
  {
    // ---------------- BABY ----------------
    if (pet.evoStage == 0)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/eld/bb/eld_baby_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/eld/bb/eld_baby_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/eld/bb/eld_baby_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/eld/bb/eld_baby_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/eld/bb/eld_baby_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/eld/bb/eld_baby_hpy_bio.png";
      }
    }

    // ---------------- TEEN ----------------
    if (pet.evoStage == 1)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/eld/tn/eld_teen_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/eld/tn/eld_teen_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/eld/tn/eld_teen_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/eld/tn/eld_teen_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/eld/tn/eld_teen_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/eld/tn/eld_teen_hpy_bio.png";
      }
    }

    // ---------------- ADULT ----------------
    if (pet.evoStage == 2)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/eld/ad/eld_ad_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/eld/ad/eld_ad_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/eld/ad/eld_ad_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/eld/ad/eld_ad_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/eld/ad/eld_ad_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/eld/ad/eld_ad_hpy_bio.png";
      }
    }

    // ---------------- ELDER ----------------
    if (pet.evoStage >= 3)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/eld/ed/eld_ed_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/eld/ed/eld_ed_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/eld/ed/eld_ed_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/eld/ed/eld_ed_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/eld/ed/eld_ed_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/eld/ed/eld_ed_hpy_bio.png";
      }
    }
  }

  // Fallback for future stages
  return "/raising_hell/graphics/pet/bio/eld/eld_baby_hpy_bio.png";
}
