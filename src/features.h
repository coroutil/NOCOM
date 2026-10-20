#pragma once
#include "sdk.h"
#include "combat_feedback.h"

void runCreateMove(float ft, CUserCmd* cmd);
void drawEsp();
void drawTrajectories(int sw, int sh);
void updatePredictionAngles(const QAngle& angles);
void drawPrediction(int sw, int sh);
void drawEdgebugHitbox();
extern volatile LONG g_EdgebugDetected;
extern volatile LONG g_EdgebugTicksToLanding;
