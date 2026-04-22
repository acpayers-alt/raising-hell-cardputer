#ifndef RH_CONSOLE_H
#define RH_CONSOLE_H

struct InputState;

void consoleOpen();
void consoleClose();
bool consoleIsOpen();
void consoleUpdate(InputState& in);
void consoleClear();

int consoleGetLineCount();
const char* consoleGetLine(int index);
const char* consoleGetInputLine();

int consoleGetScrollOffset();
int consoleGetMaxVisibleLines();
int consoleGetFirstVisibleLine(int maxLinesVisible);
bool consoleIsScrolledUp();

#endif // RH_CONSOLE_H
