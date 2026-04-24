#pragma once

// ---- MASTER STRUCT DEFINITION ----
struct PetVisualProfile {
    int spriteWidth;
    int spriteHeight;

    // Eyes
    int eyeOffsetX;
    int eyeOffsetY;
    int eyeDistance;
    int eyeRadius;
    int pupilRadius;

    // Mouth
    int mouthOffsetX;
    int mouthOffsetY;
};

// ---- Array indexed by PetType ----
extern const PetVisualProfile PET_PROFILES[];