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
#include "Camera.h"
#include "Config.h"
#include "Version.h"

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
    Camera         cam            {};
    Vec3           localPos       {};
    int            localTeam      = sdk::kFallbackLocalTeam;
    uintptr_t      localAddress   = 0;
    std::vector<EntitySnap> entities;
};

class World {
public:
    // Verdict set by the build guard. Mismatch cuts the snapshot; Match and
    // Unknown both allow it.
    void setBuildVerdict(buildguard::Verdict verdict) {
        buildVerdict_ = verdict;
    }

    // Read a complete snapshot. Returns true if the snapshot is usable (camera
    // usable + local player resolved). `vp` is the view-projection found by the
    // caller; it is copied into the camera and validated per snapshot.
    bool refresh(const Process& proc, const Projection& vp) {
        WorldSnap s{};

        if (!proc.isAttached()) {
            std::lock_guard<std::mutex> lk(mtx_);
            snap_ = s;
            return false;
        }

        s.cam.vp   = vp;
        s.cam.mode = gConfig().manualCam ? CamMode::Manual : CamMode::ViewProj;

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
        bool localPosOk = false;
        if (localPtr) {
            s.localAddress = localPtr;
            localPosOk = readPosition(proc, localPtr, s.localPos);
            proc.read<int>(localPtr + sdk::ent::kTeam, s.localTeam);
        }
        // Manual camera sits on the local player; yaw/pitch stay caller-driven.
        if (localPosOk) s.cam.pos = s.localPos;

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

                if (!readPosition(proc, ent, e.pos)) continue;

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

                e.head = readHead(proc, ent, e.pos);

                if (sdk::ent::kName) {
                    e.name = proc.readInlineString(ent + sdk::ent::kName, 32);
                }

                e.isLocal = (ent == s.localAddress);
                s.entities.push_back(std::move(e));
            }
        }

        s.valid = buildVerdict_ != buildguard::Verdict::Mismatch && localPtr != 0 &&
                  localPosOk &&
                  (s.cam.mode == CamMode::Manual
                       ? sane(s.cam.pos, kSaneWorld)
                       : (vp.addr != 0 && s.cam.vp.valid()));

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
    static constexpr float kSaneWorld = 100000.f;

    // Reads an entity position: transform indirection when both offsets are
    // known, otherwise the direct position offset. Rejects insane vectors.
    static bool readPosition(const Process& proc, uintptr_t ent, Vec3& out) {
        if (sdk::ent::kTransform && sdk::ent::kTransformPos) {
            uintptr_t transform = 0;
            if (proc.read<uintptr_t>(ent + sdk::ent::kTransform, transform) &&
                transform &&
                proc.read<Vec3>(transform + sdk::ent::kTransformPos, out) &&
                sane(out, kSaneWorld)) {
                return true;
            }
        }
        return sdk::ent::kPosition &&
               proc.read<Vec3>(ent + sdk::ent::kPosition, out) &&
               sane(out, kSaneWorld);
    }

    // Head position: kHead, then the legacy bone offset, then feet + boxHeight.
    static Vec3 readHead(const Process& proc, uintptr_t ent, const Vec3& pos) {
        Vec3 head{};
        if (sdk::ent::kHead &&
            proc.read<Vec3>(ent + sdk::ent::kHead, head) && sane(head, kSaneWorld)) {
            return head;
        }
        if (sdk::ent::kBonePosHead &&
            proc.read<Vec3>(ent + sdk::ent::kBonePosHead, head) &&
            sane(head, kSaneWorld)) {
            return head;
        }
        head = pos;
        head.z += gConfig().boxHeight;
        return head;
    }

    mutable std::mutex mtx_;
    WorldSnap          snap_;
    buildguard::Verdict buildVerdict_ = buildguard::Verdict::Unknown;
};
