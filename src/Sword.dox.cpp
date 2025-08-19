// src/Sword.dox.cpp
/**
 * @file Sword.dox.cpp
 * @brief Implementation-only TU to anchor the Doxygen "Sword" docs.
 *
 * This unit compiles to a no-op and should not export any symbols.
 * It includes Sword.dox.hpp to make sure documentation groups are
 * visible to tooling even when headers are not otherwise included.
 */

#include "Sword.dox.hpp"

namespace MB {
    namespace Docs {
        // Local internal anchor prevents some compilers from dropping this TU entirely.
        static void _sword_docs_anchor() {}
    }
} // namespace MB::Docs
