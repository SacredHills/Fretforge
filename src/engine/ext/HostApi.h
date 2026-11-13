#pragma once
#include "ravenyr_host_api.h"
#include <juce_core/juce_core.h>

namespace ff
{
class AudioEngine;
namespace hostapi
{
/** Message thread: the engine the API talks to (nullptr detaches; every call then does nothing). */
void attach (AudioEngine*) noexcept;
/** The function table handed to plugins (exported by the app as ravenyr_host_api_v1). */
const RavenyrHostApiV1* table() noexcept;
/** Audio thread (GraphMixer): a plugin on this track asked to keep running without input. */
bool wakeRequested (juce::int64 trackKey) noexcept;
} // namespace hostapi
} // namespace ff
