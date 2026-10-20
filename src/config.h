#pragma once
#include <string>
#include <vector>

std::string cfgDir();
std::string sanitizeName(const std::string& in);
void refreshCfgs();
bool saveConfig(const std::string& name);
bool loadConfig(const std::string& name);
bool deleteConfig(const std::string& name);
void loadActiveName();

extern std::vector<std::string> g_Cfgs;
