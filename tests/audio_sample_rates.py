#!/usr/bin/env python3
"""Generate known tones at many input rates, then run the native audio check."""
import pathlib
import shutil
import subprocess
import sys

if len(sys.argv) != 3:
    raise SystemExit("Usage: python3 tests/audio_sample_rates.py NATIVE_TEST_EXECUTABLE FIXTURE_DIRECTORY")
ffmpeg = shutil.which("ffmpeg")
if not ffmpeg:
    raise SystemExit("FFmpeg is required to generate the test files.")
output = pathlib.Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
cases = []
for rate in (8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 176400, 192000):
    cases.append((rate, "wav", "pcm_s24le", rate in (8000, 22050)))
for extension, codec, rates in (
    ("flac", "flac", (22050, 44100, 48000, 96000, 192000)),
    ("aiff", "pcm_s24be", (44100, 48000, 96000, 192000)),
    ("ogg", "libvorbis", (22050, 44100, 48000, 96000, 192000)),
    ("mp3", "libmp3lame", (8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000)),
    ("m4a", "aac", (22050, 44100, 48000, 96000)),
):
    cases.extend((rate, extension, codec, extension == "mp3" and rate in (8000, 16000)) for rate in rates)
paths = []
for rate, extension, codec, mono in cases:
    path = output / f"{rate}-{'mono' if mono else 'stereo'}.{extension}"
    tones = "0.3*sin(2*PI*997*t)" + ("" if mono else "|0.3*sin(2*PI*1499*t)")
    subprocess.run([ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                    f"aevalsrc={tones}:s={rate}:d=1", "-c:a", codec, "-y", str(path)], check=True)
    paths.append(str(path))
print(f"Generated {len(paths)} fixtures at {output}", flush=True)
with (output / "sample-rate-results.txt").open("w") as report:
    process = subprocess.Popen([str(pathlib.Path(sys.argv[1]).resolve()), *paths], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, bufsize=1)
    for line in process.stdout:
        print(line, end="", flush=True)
        report.write(line)
    raise SystemExit(process.wait())
