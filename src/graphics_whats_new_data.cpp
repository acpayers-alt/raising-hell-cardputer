#include "graphics_whats_new_data.h"

const HelpLine kWhatsNew[] = {

    {HelpLineType::SECTION, "What's New"},
    {HelpLineType::BODY, "Version 2.1"},
    {HelpLineType::BODY, "- Major stability improvements"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "New Features"},
    {HelpLineType::BODY, "- Timezone auto-detection"},
    {HelpLineType::BODY, "- Auto Clock idle mode"},
    {HelpLineType::BODY, "- Improved console + scrollback"},
    {HelpLineType::BODY, "- What's New screen"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Improvements"},
    {HelpLineType::BODY, "- Better boot + time setup"},
    {HelpLineType::BODY, "- UI cleanup and polish"},
    {HelpLineType::BODY, "- Improved sound handling"},
    {HelpLineType::BODY, "- Factory reset hold indicator"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Fixes"},
    {HelpLineType::BODY, "- Fixed title screen background bug"},
    {HelpLineType::BODY, "- Fixed missing pet name issue"},
    {HelpLineType::BODY, "- Improved rendering stability"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Enjoy"},
    {HelpLineType::BODY, "Thanks for playing Raising Hell"},
    {HelpLineType::BODY, "More chaos coming soon"},
    {HelpLineType::GAP, nullptr},
};

const int kWhatsNewCount = sizeof(kWhatsNew) / sizeof(kWhatsNew[0]);