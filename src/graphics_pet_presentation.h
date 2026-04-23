void graphicsReleasePetLayerForOta();
void graphicsRecoverAfterOta();
void graphicsPrewarmPetBackgroundCache();
void graphicsReleasePetBackgroundCache();

void cachePetAreaBackgroundIfNeeded(bool force);
void restorePetAreaFromCache();

void drawPetScreen(bool redrawBg);

void tickPetIntroWalk();
void tickPetWander();

bool drawIntroWalkingPetOverride();

void resetClockModePetPresentation();
void resetPetScreenPositionToHome();
void resetPetWanderToHome();
void startPetIntroWalkFromLeft();

void getPetHomeScreenPosition(int &outX, int &outY);

int petPresentationX();
int petPresentationY();

bool petPresentationHasIntroHandoff();
void clearPetPresentationIntroHandoff();

bool petPresentationScriptedIntroActive();
bool petPresentationAnimating();
bool petWalkOverrideActive();