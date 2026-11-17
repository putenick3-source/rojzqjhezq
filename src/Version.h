// Version.h - Build guard for the dumped GameAssembly.dll.
//
// The overlay refuses to draw ESP when the live GameAssembly.dll build differs
// from sdk::kDumpedVersion (offsets would be meaningless). Detection uses the
// DLL's version resource, read from the RobRun.exe directory; it never touches
// the game process beyond OpenProcess query rights.

#pragma once
#include "Process.h"

#include <string>

namespace buildguard {

    struct Status {
        bool         compatible = false;
        std::wstring current;    // empty when the version resource is unreadable
        std::wstring modulePath;
    };

    // Reads the live build version and compares it with sdk::kDumpedVersion.
    Status check(const Process& proc);

    // Persists the observed version in RobRunOverlay.ini and logs the verdict.
    void record(const Status& status);

}
