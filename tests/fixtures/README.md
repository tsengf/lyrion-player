# Generated codec fixtures

These one-second stereo tones are synthetic test data generated for this project
(440 Hz at amplitude 5000 on the left, 660 Hz at amplitude 3000 on the right,
44.1 kHz signed 16-bit PCM). They contain no recorded or third-party audio.
They may be used under the project's GPL-3.0-or-later license.

To regenerate, create a WAV using Python and encode it using FFmpeg:

```sh
python3 - <<'PY'
import math, struct, wave
with wave.open('/tmp/lyrion-codec-tone.wav', 'wb') as f:
    f.setparams((2, 2, 44100, 0, 'NONE', 'not compressed'))
    f.writeframes(b''.join(struct.pack('<hh',
        int(5000 * math.sin(i * math.tau * 440 / 44100)),
        int(3000 * math.sin(i * math.tau * 660 / 44100)))
        for i in range(44100)))
PY
ffmpeg -v error -y -i /tmp/lyrion-codec-tone.wav -c:a flac tests/fixtures/tone.flac
ffmpeg -v error -y -i /tmp/lyrion-codec-tone.wav -c:a libmp3lame -b:a 96k tests/fixtures/tone.mp3
ffmpeg -v error -y -i /tmp/lyrion-codec-tone.wav -c:a libvorbis -q:a 2 tests/fixtures/tone.ogg
```

FFmpeg is only needed to regenerate these fixtures, not to build or run tests.
