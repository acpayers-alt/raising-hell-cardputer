#include "pet_visuals.h"
#include "pet.h"

#include "graphics_assets.h" // for your actual sprite dimensions

// -------------------------------------------
// VISUAL PROFILES FOR EACH PET TYPE
// -------------------------------------------
// These define:
//   • sprite width/height
//   • eye position offsets
//   • pupil radius
//   • eye spacing
//
// They ensure pet_renderer() + pet_eyes() work correctly
// -------------------------------------------

const PetVisualProfile PET_PROFILES[PET_TYPE_COUNT] = {

    // PET_DEVIL
    {96, 96, 0, -6, 24, 7, 3, 0, 20},

    // PET_ELDRITCH
    {96, 96, 0, -8, 22, 6, 3, 0, 18},

    // PET_ALIEN
    {96, 96, 0, -20, 22, 6, 3, 0, 18},
};