#include "netvar.h"
#include <cstring>
#include <cctype>

namespace {
ClientClass* g_head;

int search(RecvTable* t, const char* prop) {
    for (int i = 0; i < t->m_nProps; i++) {
        RecvProp& p = t->m_pProps[i];
        if (!p.m_pVarName || isdigit((unsigned char)p.m_pVarName[0])) continue;
        if (!strcmp(p.m_pVarName, prop)) return p.m_Offset;
        if (p.m_pDataTable) {
            int o = search(p.m_pDataTable, prop);
            if (o >= 0) return o + p.m_Offset;
        }
    }
    return -1;
}

RecvTable* findTable(RecvTable* t, const char* name) {
    if (!t) return nullptr;
    if (t->m_pNetTableName && !strcmp(t->m_pNetTableName, name)) return t;
    for (int i = 0; t->m_pProps && i < t->m_nProps; i++)
        if (t->m_pProps[i].m_pDataTable)
            if (RecvTable* r = findTable(t->m_pProps[i].m_pDataTable, name)) return r;
    return nullptr;
}

RecvProp* searchPropPtr(RecvTable* t, const char* prop) {
    if (!t || !prop || !t->m_pProps) return nullptr;
    for (int i = 0; i < t->m_nProps; i++) {
        RecvProp& p = t->m_pProps[i];
        if (p.m_pVarName && !strcmp(p.m_pVarName, prop)) return &p;
        if (p.m_pDataTable) {
            if (RecvProp* found = searchPropPtr(p.m_pDataTable, prop)) return found;
        }
    }
    return nullptr;
}

int searchWithOffset(RecvTable* t, const char* prop, int baseOffset, int depth) {
    if (!t || !prop || !t->m_pProps || depth > 32 || t->m_nProps < 0 || t->m_nProps > 4096)
        return -1;

    for (int i = 0; i < t->m_nProps; ++i) {
        const RecvProp& p = t->m_pProps[i];
        if (p.m_pVarName && std::strcmp(p.m_pVarName, prop) == 0)
            return baseOffset + p.m_Offset;
    }

    for (int i = 0; i < t->m_nProps; ++i) {
        const RecvProp& p = t->m_pProps[i];
        if (!p.m_pDataTable) continue;
        const int found = searchWithOffset(p.m_pDataTable, prop,
            baseOffset + p.m_Offset, depth + 1);
        if (found >= 0) return found;
    }
    return -1;
}

}

void netvar::init(ClientClass* head) { g_head = head; }

int netvar::Find(const char* table, const char* prop) {
    for (ClientClass* c = g_head; c; c = c->m_pNext) {
        if (!c->m_pRecvTable) continue;
        RecvTable* t = findTable(c->m_pRecvTable, table);
        if (t) {
            int o = search(t, prop);
            if (o >= 0) return o;
        }
    }
    return -1;
}

int netvar::FindForClass(ClientClass* cc, const char* prop) {
    if (!cc || !cc->m_pRecvTable || !prop || !prop[0]) return -1;
    return searchWithOffset(cc->m_pRecvTable, prop, 0, 0);
}

RecvProp* netvar::FindProp(const char* table, const char* prop) {
    if (!table || !prop) return nullptr;
    for (ClientClass* c = g_head; c; c = c->m_pNext) {
        if (!c->m_pRecvTable) continue;
        RecvTable* t = findTable(c->m_pRecvTable, table);
        if (t) {
            if (RecvProp* found = searchPropPtr(t, prop)) return found;
        }
    }
    return nullptr;
}

bool netvar::Inherits(ClientClass* cc, const char* tableName) {
    if (!cc || !cc->m_pRecvTable) return false;
    RecvTable* t = cc->m_pRecvTable;
    while (t) {
        if (t->m_pNetTableName && !strcmp(t->m_pNetTableName, tableName)) return true;
        RecvTable* next = nullptr;
        for (int i = 0; i < t->m_nProps; i++)
            if (t->m_pProps[i].m_pVarName && !strcmp(t->m_pProps[i].m_pVarName, "baseclass")) {
                next = t->m_pProps[i].m_pDataTable;
                break;
            }
        t = next;
    }
    return false;
}
