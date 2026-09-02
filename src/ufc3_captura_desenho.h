#pragma once

// Capture of the game's draw stream. See ufc3_captura_desenho.cpp for the
// rationale and for how the hook replaces the codegen-emitted function.

namespace ufc3 {
namespace captura_desenho {

// Dumps the running totals to the log. The hooks already summarise
// periodically; this closes the count at a chosen moment.
void ResumirAgora();

}  // namespace captura_desenho
}  // namespace ufc3
