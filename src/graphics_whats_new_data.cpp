#include "graphics_whats_new_data.h"

const HelpLine kWhatsNew[] = {

    {HelpLineType::SECTION, "What's New"},
    {HelpLineType::BODY, "Version 2.1"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "New Features"},
    {HelpLineType::BODY, "- Timezone auto-detection"},
    {HelpLineType::BODY, "- Improved console with scrollback"},
    {HelpLineType::BODY, "- Better controls manual"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Changes"},
    {HelpLineType::BODY, "- UI cleanup and polish"},
    {HelpLineType::BODY, "- Sound poish"},
    {HelpLineType::BODY, "- Improved stability"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Enjoy"},
    {HelpLineType::BODY, "Thanks for playing Raising Hell"},
    {HelpLineType::BODY, "More chaos coming soon"},
    {HelpLineType::GAP, nullptr},
};

const int kWhatsNewCount = sizeof(kWhatsNew) / sizeof(kWhatsNew[0]);