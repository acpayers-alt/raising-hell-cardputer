#include "graphics_whats_new_data.h"

const HelpLine kWhatsNew[] = {

    {HelpLineType::SECTION, "What's New"},
    {HelpLineType::BODY, "Version 3.0"},
    {HelpLineType::BODY, "- Alien Edition"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "New Features"},
    {HelpLineType::BODY, "- Alien Pet Type"},
    {HelpLineType::BODY, "- Abduction Mini Game"},
    {HelpLineType::BODY, "- Unique Resurrection Games"},
    {HelpLineType::BODY, "for each pet"},
    {HelpLineType::BODY, "- Fishing Activity"},
    {HelpLineType::BODY, "- New Shop Items"},
    {HelpLineType::BODY, "- 24 hour time option"},
    {HelpLineType::BODY, "- Photos and Gallery"},
    {HelpLineType::BODY, "- Pace System (Difficutly)"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Improvements"},
    {HelpLineType::BODY, "- Thought bubbles for conditions"},
    {HelpLineType::BODY, "- Loading verbiage for mini games"},
    {HelpLineType::BODY, "- Consolidated Game Menu"},
    {HelpLineType::BODY, "- Loading verbiage for Splash"},
    {HelpLineType::BODY, "- Clock Mode Sleep Screen"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Gameplay"},
    {HelpLineType::BODY, "- Return to baby state item"},
    {HelpLineType::BODY, "- Pace Settings for Difficulty"},
    {HelpLineType::BODY, "- Better event feedback"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Fixes"},
    {HelpLineType::BODY, "- Improved Wifi flow for first"},
    {HelpLineType::BODY, "boot asset provisioning"},
    {HelpLineType::BODY, "- Improved provisioning fallback"},
    {HelpLineType::BODY, "behavior"},
    {HelpLineType::BODY, "- Better handling of missing asset"},
    {HelpLineType::BODY, "- Graphics and rendering fixes"},
    {HelpLineType::BODY, "- Improved save file handling"},

    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Enjoy"},
    {HelpLineType::BODY, "Thanks for playing Raising Hell"},
    {HelpLineType::BODY, "More chaos coming soon"},
    {HelpLineType::GAP, nullptr},
};

const int kWhatsNewCount = sizeof(kWhatsNew) / sizeof(kWhatsNew[0]);