// LOCKED INTERFACE. Written by the reviewer, not the builder.
// PS-MAIN (docs/spec/playback-shell.md), AT-OWN (docs/spec/audio-thread.md): the program's real
// sound as one thing that can be made on the audio thread.
#pragma once

namespace linuxbee {
class AudioBackend;
}

namespace linuxbee::ui {

// A new FfmpegAudioBackend over a new DeviceSink. The sink is the backend's child, so it goes
// with it. Both belong to the thread that calls this; call it on the thread they are to live
// on (it is a ThreadedAudioBackend::Factory). The caller owns what is returned.
linuxbee::AudioBackend *makeDeviceBackend();

}
