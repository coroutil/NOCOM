#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

// Restore hooks before unloading the DLL

class ShadowVTable final {
public:
    ShadowVTable() = default;
    ShadowVTable(const ShadowVTable&) = delete;
    ShadowVTable& operator=(const ShadowVTable&) = delete;

    bool Prepare(void* instance, std::size_t entryCount) {
        if (prepared_ || !instance || entryCount == 0 || entryCount > 256)
            return false;
        if (!ReadableRange(instance, sizeof(void*)))
            return false;

        void** table = *reinterpret_cast<void***>(instance);
        if (!table || !ReadableRange(table, sizeof(void*)))
            return false;

        // Keep the full instance vtable; the engine calls entries by slot

        const std::size_t detected = DetectVTableLength(table, 256);
        if (detected > entryCount)
            entryCount = detected;
        if (entryCount == 0 || entryCount > 256)
            return false;
        if (!ReadableRange(table, entryCount * sizeof(void*)))
            return false;

        std::unique_ptr<void*[]> copy(new (std::nothrow) void*[entryCount]);
        if (!copy)
            return false;

        std::memcpy(copy.get(), table, entryCount * sizeof(void*));
        instance_ = instance;
        original_ = table;
        shadow_ = std::move(copy);
        entryCount_ = entryCount;
        prepared_ = true;
        return true;
    }

    void* Original(std::size_t index) const {
        if (!prepared_ || index >= entryCount_)
            return nullptr;
        return original_[index];
    }

    bool Set(std::size_t index, void* detour) {
        if (!prepared_ || committed_ || index >= entryCount_ || !detour)
            return false;
        shadow_[index] = detour;
        return true;
    }

    bool Commit() {
        if (!prepared_ || committed_)
            return false;
        if (!ReadableRange(instance_, sizeof(void*)))
            return false;

        if (*reinterpret_cast<void***>(instance_) != original_)
            return false;

        DWORD oldProtect = 0;
        if (!VirtualProtect(instance_, sizeof(void*), PAGE_READWRITE, &oldProtect))
            return false;

        *reinterpret_cast<void***>(instance_) = shadow_.get();
        DWORD ignored = 0;
        VirtualProtect(instance_, sizeof(void*), oldProtect, &ignored);
        committed_ = true;
        return true;
    }

    bool Restore() {
        if (!prepared_)
            return true;

        bool restored = true;
        bool safeToFree = !committed_;
        if (committed_) {
            if (!ReadableRange(instance_, sizeof(void*))) {
                restored = false;
            } else {
                void** current = *reinterpret_cast<void***>(instance_);
                if (current == shadow_.get()) {
                    DWORD oldProtect = 0;
                    if (!VirtualProtect(instance_, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
                        restored = false;
                    } else {
                        *reinterpret_cast<void***>(instance_) = original_;
                        DWORD ignored = 0;
                        VirtualProtect(instance_, sizeof(void*), oldProtect, &ignored);
                        safeToFree = true;
                    }
                } else if (current == original_) {
                    safeToFree = true;
                } else {

                    restored = false;
                }
            }
        }

        if (safeToFree) {
            shadow_.reset();
            instance_ = nullptr;
            original_ = nullptr;
            entryCount_ = 0;
            prepared_ = false;
            committed_ = false;
        }
        return restored;
    }

    bool IsCommitted() const { return committed_; }
    bool IsPrepared() const { return prepared_; }
    std::size_t EntryCount() const { return entryCount_; }
    void* Instance() const { return instance_; }
    void** OriginalTable() const { return original_; }
    void** ShadowTable() const { return shadow_.get(); }

private:
    static bool isExecutableAddress(const void* address) {
        if (!address)
            return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(address, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;
        const DWORD protection = mbi.Protect & 0xFF;
        return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    }

    // Keep the full instance vtable; the engine calls entries by slot

    static std::size_t DetectVTableLength(void** table, std::size_t cap) {
        std::size_t count = 0;
        for (; count < cap; ++count) {
            if (!ReadableRange(table + count, sizeof(void*)))
                break;
            if (!isExecutableAddress(table[count]))
                break;
        }
        return count;
    }

    static bool ReadableRange(const void* address, std::size_t bytes) {
        if (!address || bytes == 0)
            return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(address, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;

        const auto begin = reinterpret_cast<std::uintptr_t>(address);
        const auto regionBegin = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const auto regionEnd = regionBegin + mbi.RegionSize;
        if (begin < regionBegin || begin > regionEnd || bytes > regionEnd - begin)
            return false;
        return true;
    }

    void* instance_ = nullptr;
    void** original_ = nullptr;
    std::unique_ptr<void*[]> shadow_;
    std::size_t entryCount_ = 0;
    bool prepared_ = false;
    bool committed_ = false;
};
