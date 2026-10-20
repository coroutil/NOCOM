#pragma once
#include <windows.h>
#include <cmath>
#include <cstdint>

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float f) const { return {x * f, y * f, z * f}; }
    float Length() const { return sqrtf(x * x + y * y + z * z); }
    float Length2D() const { return sqrtf(x * x + y * y); }
    bool IsZero() const { return x == 0 && y == 0 && z == 0; }
};
using QAngle = Vec3;

template <typename R, typename... A>
inline R VCall(void* obj, int index, A... args) {
    using Fn = R(__thiscall*)(void*, A...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(obj))[index])(obj, args...);
}

struct CUserCmd {
    void* vptr;
    int command_number;
    int tick_count;
    QAngle viewangles;
    float forwardmove;
    float sidemove;
    float upmove;
    int buttons;
    uint8_t impulse;
    uint8_t pad41[3];
    int weaponselect;
    int weaponsubtype;
    int random_seed;
    short mousedx;
    short mousedy;
    bool hasbeenpredicted;
    uint8_t pad[23];
};
static_assert(sizeof(CUserCmd) == 84, "CUserCmd size");

enum : int {
    IN_ATTACK = 1 << 0,
    IN_JUMP = 1 << 1,
    IN_DUCK = 1 << 2,
    IN_FORWARD = 1 << 3,
    IN_BACK = 1 << 4,
    IN_USE = 1 << 5,
    IN_LEFT = 1 << 7,
    IN_RIGHT = 1 << 8,
    IN_MOVELEFT = 1 << 9,
    IN_MOVERIGHT = 1 << 10,
};

constexpr unsigned MASK_SHOT = 0x46004003;

struct ClientClass;
struct RecvTable;

struct RecvProp {
    const char* m_pVarName;
    int m_RecvType;
    int m_Flags;
    int m_StringBufferSize;
    bool m_bInsideArray;
    const void* m_pExtraData;
    RecvProp* m_pArrayProp;
    void* m_ArrayLengthProxy;
    void* m_ProxyFn;
    void* m_DataTableProxyFn;
    RecvTable* m_pDataTable;
    int m_Offset;
    int m_ElementStride;
    int m_nElements;
    const char* m_pParentArrayPropName;
};
static_assert(sizeof(RecvProp) == 0x3C, "RecvProp size");

struct RecvTable {
    RecvProp* m_pProps;
    int m_nProps;
    void* m_pDecoder;
    const char* m_pNetTableName;
    bool m_bInitialized;
    bool m_bInMainList;
};

struct ClientClass {
    void* m_pCreateFn;
    void* m_pCreateEventFn;
    const char* m_pNetworkName;
    RecvTable* m_pRecvTable;
    ClientClass* m_pNext;
    int m_ClassID;
};

inline int g_RayVariant = 0;
struct Ray_t {
    alignas(16) Vec3 start; float pad0;
    alignas(16) Vec3 delta; float pad1;
    alignas(16) Vec3 startOffset; float pad2;
    alignas(16) Vec3 extents; float pad3;
    unsigned char tail[16];
    void init(const Vec3& a, const Vec3& b) {
        delta = b - a; start = a; startOffset = Vec3(); extents = Vec3();
        for (auto& c : tail) c = 0;
        bool swept = (delta.x != 0 || delta.y != 0 || delta.z != 0);
        int o = g_RayVariant ? 4 : 0;
        tail[o] = 1; tail[o + 1] = swept;
    }
};

struct Plane_t { Vec3 normal; float dist; uint8_t type, signbits, pad[2]; };
struct Surface_t { const char* name; short surfaceProps; unsigned short flags; };

struct Trace_t {
    Vec3 startpos, endpos;
    Plane_t plane;
    float fraction;
    int contents;
    unsigned short dispFlags;
    bool allsolid, startsolid;
    float fractionleftsolid;
    Surface_t surface;
    int hitgroup;
    short physicsbone;
    void* ent;
    int hitbox;
};

struct TraceFilter {
    virtual bool ShouldHitEntity(void* ent, int) { return ent != skip; }
    virtual int GetTraceType() const { return 0; }
    void* skip = nullptr;
};
