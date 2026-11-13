# Playback fixtures

Copies of three plan-08 conversion fixtures, kept here so that the audio
engine's test does not depend on the converter being merged.

| File | What |
|---|---|
| `tone.flac` | 1 s, 440 Hz sine, 16-bit, 44100 Hz stereo, with tags and a picture |
| `hires.flac` | 0.5 s, 1000 Hz sine, 24-bit, 96000 Hz stereo |
| `tone.mp3` | `tone.flac` encoded at 128 kbps |

They were generated with ffmpeg by `tests/data/convert/make_fixtures.py`
of plan 08 and are not copied from any recording.
