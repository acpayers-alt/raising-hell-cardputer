#include "graphics_whats_new_data.h"

const HelpLine kWhatsNew[] = {

    {HelpLineType::SECTION, "What's New"},
    {HelpLineType::BODY, "Version 2.2"},
    {HelpLineType::BODY, "- Pet Behavior + QOL"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "New Features"},
    {HelpLineType::BODY, "- Autonomous Pet Actions"},
    {HelpLineType::BODY, "- Passive XP gain"},
    {HelpLineType::BODY, "- War Walking/Step counter"},
    {HelpLineType::BODY, "- Sleep Quality score"},
    {HelpLineType::BODY, "- 24 hour time option"},
    {HelpLineType::BODY, "- Dirty time indicators"},
    {HelpLineType::BODY, "- Multiple Wi-Fi profiles"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Improvements"},
    {HelpLineType::BODY, "- Hold to scroll in menus"},
    {HelpLineType::BODY, "- Scroll indicators in submenus"},
    {HelpLineType::BODY, "- Care Guide (manual renamed)"},
    {HelpLineType::BODY, "- Cleaner system settings"},
    {HelpLineType::BODY, "- More reliable saves (auto heal)"},
    {HelpLineType::BODY, "- Improved console + scrollback"},
    {HelpLineType::BODY, "- Provisioning reliability"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Gameplay"},
    {HelpLineType::BODY, "- Pets now react to conditions"},
    {HelpLineType::BODY, "- Reduced repetitive rewards"},
    {HelpLineType::BODY, "- Better event feedback"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Fixes"},
    {HelpLineType::BODY, "- Fixed popup lock issues"},
    {HelpLineType::BODY, "- Fixed menu selection bugs"},
    {HelpLineType::BODY, "- Improved rendering stability"},
    {HelpLineType::BODY, "- General bug fixes"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Enjoy"},
    {HelpLineType::BODY, "Thanks for playing Raising Hell"},
    {HelpLineType::BODY, "More chaos coming soon"},
    {HelpLineType::GAP, nullptr},
};

const int kWhatsNewCount = sizeof(kWhatsNew) / sizeof(kWhatsNew[0]);