#pragma once
#include "MiraDisplayProfile.h"

#if defined(MIRA_TARGET_WAVESHARE_43C)
  #include "Waveshare43CDisplay.h"
  using MiraDisplay = Waveshare43CDisplay;
#else
  #include "SC01PlusDisplay.h"
  using MiraDisplay = SC01PlusDisplay;
#endif
