#include "audio/devicebackend.h"

#include "audio/devicesink.h"
#include "playback/audioengine.h"

namespace linuxbee::ui {

linuxbee::AudioBackend *makeDeviceBackend()
{
    // The engine is handed its sink, so the sink is made first. Once the engine exists the sink
    // becomes its child, and the two are deleted together.
    auto *sink = new DeviceSink;
    auto *engine = new FfmpegAudioBackend(sink);
    sink->setParent(engine);
    return engine;
}

}
