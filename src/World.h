// World.h - Snapshot of the game world read from the attached process.
//
// Pulls local player + entity list each tick into plain structs so the render
// thread never touches ReadProcessMemory directly. Decouples timing: reads can
// run on their own cadence (see Config::readIntervalMs) without jittering the
// overlay FPS.

#pragma once
#include "Process.h"
#include "SDK.h"
#include "VecMath.h"
#include "Config.h"

#include <vector>
#include <string>
#include <mutex>

struct EntitySnap {
    uintptr_t address   = 0;
    Vec3      pos       {};
    Vec3      head      {};
    float     hp        = 0.f;
    float     maxHp     = 1.f;
    int       team      = 0;
    bool      alive     = false;
    bool      visible   = true;
    bool      isLocal   = false;
    std::string name;
};

struct WorldSnap {
    bool           valid          = false;
    bool           inMatch        = true;
    Matrix4        viewMatrix     {};
    Vec3           localPos       {};
    int            localTeam      = sdk::kFallbackLocalTeam;
    uintptr_t      localAddress   = 0;
    std::vector<EntitySnap> entities;
};

class World {
public:
    // Read a complete snapshot. Returns true if the snapshot is usable (at
    // least the view matrix + local player resolved).
    bool refresh(const Process& proc) {
        WorldSnap s{};

        if (!proc.isAttached()) {
            std::lock_guard<std::mutex> lk(mtx_);
            snap_ = s;
            return false;
        }

        // View matrix
        if (sdk::off::kViewMatrix) {
            proc.read<Matrix4>(proc.base() + sdk::off::kViewMatrix, s.viewMatrix);
        }

        // Game state (optional) — bail early if clearly not in match
        if (sdk::off::kGameState) {
            int state = 0;
            if (proc.read<int>(proc.base() + sdk::off::kGameState, state)) {
                s.inMatch = (state != 0); // tune this predicate once state semantics are known
            }
        }

        // Local player
        uintptr_t localPtr = 0;
        if (sdk::off::kLocalPlayer) {
            proc.read<uintptr_t>(proc.base() + sdk::off::kLocalPlayer, localPtr);
        }
        if (localPtr) {
            s.localAddress = localPtr;
            proc.read<Vec3>(localPtr + sdk::ent::kPosition, s.localPos);
            proc.read<int>(localPtr + sdk::ent::kTeam, s.localTeam);
        }

        // Entity list
        uintptr_t listPtr = 0;
        if (sdk::off::kEntityList) {
            proc.read<uintptr_t>(proc.base() + sdk::off::kEntityList, listPtr);
        }
        int count = sdk::kMaxEntities;
        if (sdk::off::kEntityCount) {
            proc.read<int>(proc.base() + sdk::off::kEntityCount, count);
        }
        if (count < 0 || count > 4096) count = sdk::kMaxEntities;

        if (listPtr && count > 0) {
            s.entities.reserve(count);
            for (int i = 0; i < count; ++i) {
                uintptr_t ent = 0;
                if (!proc.read<uintptr_t>(listPtr + i * sdk::kEntityStride, ent) || !ent) {
                    continue;
                }
                EntitySnap e;
                e.address = ent;

                proc.read<Vec3>(ent + sdk::ent::kPosition, e.pos);
                proc.read<float>(ent + sdk::ent::kHealth, e.hp);
                proc.read<float>(ent + sdk::ent::kMaxHealth, e.maxHp);
                if (e.maxHp <= 0.f) e.maxHp = 100.f;

                proc.read<int>(ent + sdk::ent::kTeam, e.team);

                int alive = 0;
                proc.read<int>(ent + sdk::ent::kIsAlive, alive);
                e.alive = (alive != 0) && (e.hp > 0.f);

                int vis = 1;
                if (sdk::ent::kVisible) proc.read<int>(ent + sdk::ent::kVisible, vis);
                e.visible = vis != 0;

                if (sdk::ent::kBonePosHead) {
                    proc.read<Vec3>(ent + sdk::ent::kBonePosHead, e.head);
                } else {
                    e.head = { e.pos.x, e.pos.y, e.pos.z + 1.8f }; // fallback: assume ~1.8m tall
                }

                if (sdk::ent::kName) {
                    e.name = proc.readInlineString(ent + sdk::ent::kName, 32);
                }

                e.isLocal = (ent == s.localAddress);
                s.entities.push_back(std::move(e));
            }
        }

        s.valid = (sdk::off::kViewMatrix != 0 && localPtr != 0);

        {
            std::lock_guard<std::mutex> lk(mtx_);
            snap_ = std::move(s);
        }
        return snap_.valid;
    }

    WorldSnap get() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return snap_;
    }

private:
    mutable std::mutex mtx_;
    WorldSnap          snap_;
};
