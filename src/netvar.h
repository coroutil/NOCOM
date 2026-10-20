#pragma once
#include "sdk.h"

namespace netvar {
void init(ClientClass* head);

int Find(const char* table, const char* prop);

int FindForClass(ClientClass* cc, const char* prop);
RecvProp* FindProp(const char* table, const char* prop);

bool Inherits(ClientClass* cc, const char* tableName);

}
