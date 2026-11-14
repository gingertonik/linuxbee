# Fixtures for the loudness tests

`tone.flac` (1 s, 440 Hz sine, 16-bit, 44100 Hz, stereo) and `hires.flac`
(0.5 s, 1000 Hz sine, 24-bit, 96000 Hz, stereo) are copies of the converter's
fixtures, made by `tests/data/convert/make_fixtures.py`. `notaudio.mp3` is a
text file.

`mono.flac` (2 s, 440 Hz sine at half scale, one channel), `quietmono.flac`
(2 s, 660 Hz sine at one eighth, one channel), `quietstereo.flac` (the
same in each of two channels) and `surround.flac` (2 s,
the 440 Hz sine at half scale in each of six channels, layout 5.1) are
16-bit, 44100 Hz, made with ffmpeg's `sine` source. With
`[0:a:0]ebur128=peak=true` and nothing else, ffmpeg n9.0.2 reports:

| File | Loudness | True peak |
|---|---|---|
| `mono.flac` | -27.8 LUFS | -24.1 dBFS |
| `quietmono.flac` | -39.7 LUFS | -36.1 dBFS |
| `quietstereo.flac` | -36.7 LUFS | -36.1 dBFS |
| `surround.flac` | -20.1 LUFS | -24.1 dBFS |
