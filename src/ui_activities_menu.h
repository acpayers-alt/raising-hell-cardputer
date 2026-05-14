#pragma once

struct InputState;

int uiActivitiesMenuCount();
const char *uiActivitiesMenuLabel(int idx);
bool uiActivitiesMenuActivate(int idx, InputState &in);