#pragma once

#include "input.h"

void openPhotoGalleryFromTitle(InputState &in);
void uiPhotoGalleryOnEnter(InputState &in);
void uiPhotoGalleryHandle(InputState &in);

int photoGalleryCount();
int photoGallerySelected();
int photoGalleryWindowStart();
int photoGalleryVisibleCount();
const char *photoGalleryVisibleName(int visibleIndex);
const char *photoGallerySelectedPath();
bool photoGalleryViewingPhoto();