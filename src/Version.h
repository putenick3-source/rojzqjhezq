// Version.h - Build guard for the dumped game build.
//
// The reference version is the game's own build number (e.g. "0.7.0"), read
// from the process executable's FileVersion resource. If that resource is
// missing, the GameAssembly.dll resource is tried as a fallback. Only the
// "major.minor.build" prefix is compared; the revision is ignored.
//
// Three explicit verdicts:
//   Unknown  - version unreadable (ESP stays enabled, orange badge)
//   Match    - prefix equals sdk::kDumpedVersion (green badge)
//   Mismatch - prefix differs (WorldSnap::valid forced false, ESP cut)

#pragma once
#include "Process.h"

#include <string>

namespace buildguard {

    enum class Verdict {
        Unknown,
        Match,
        Mismatch,
    };

    struct Status {
        Verdict      verdict = Verdict::Unknown;
        std::wstring current;    // empty when no version resource is readable
        std::wstring sourcePath; // file the version was read from
    };

    // Reads the live build version (EXE first, anchor module fallback) and
    // classifies it against sdk::kDumpedVersion.
    Status check(const Process& proc);

    // Persists the observed version and verdict in RobRunOverlay.ini and logs.
    void record(const Status& status);

    // Stable INI/log spelling for a verdict.
    const wchar_t* verdictName(Verdict verdict);

}

