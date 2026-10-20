#pragma once
#include "sdk.h"
#include <string>
#include <vector>

void updateCombatFeedback(CUserCmd* cmd);
void previewNocomSound(const std::string& selection, int volumePercent);
void ensureGameSoundCatalog();
const std::vector<std::string>& gameSoundCatalog();
int findGameSoundIndex(const std::string& name);
