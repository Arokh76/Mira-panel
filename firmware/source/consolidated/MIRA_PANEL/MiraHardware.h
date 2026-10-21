#pragma once
#include "MiraDisplayProfile.h"

#if defined(MIRA_TARGET_WAVESHARE_43C)
  #include "Waveshare43CDisplay.h"
  using MiraDisplay = Waveshare43CDisplay;
#elif defined(MIRA_TARGET_NEXTION_28)
  #include "MiraNextion28Display.h"
  using MiraDisplay = MiraNextion28Display;
#else
  #include "SC01PlusDisplay.h"
  using MiraDisplay = SC01PlusDisplay;
#endif
