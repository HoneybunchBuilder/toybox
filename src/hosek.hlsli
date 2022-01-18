#pragma once

#define HOSEK_COEFF_MAX 1080
#define HOSEK_RAD_MAX 120

typedef struct SkyHosekData {
  float coeffsX[HOSEK_COEFF_MAX];
  float radX[HOSEK_RAD_MAX];
  float coeffsY[HOSEK_COEFF_MAX];
  float radY[HOSEK_RAD_MAX];
  float coeffsZ[HOSEK_COEFF_MAX];
  float radZ[HOSEK_RAD_MAX];
} SkyHosekData;
