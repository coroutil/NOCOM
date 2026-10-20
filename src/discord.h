#pragma once

namespace discord {
void Start();
void Stop();
void SetAppId(const char* id);
void SetStatus(const char* details, const char* state);
bool Ready();
void CopyAppId(char* out, int n);
}
