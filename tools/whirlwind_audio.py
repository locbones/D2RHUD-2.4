"""Build loop-ready Whirlwind FLAC overrides from an extracted D2R data folder.

Requires numpy and soundfile. Reads originals without modifying them; writes
edited assets, a repeated WAV preview, and measurements under --output.
"""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import soundfile as sf


ASSETS = (
    "global/sfx/skill/barbarian/whirlwind1.flac",
    "hd/global/sfx/skill/barbarian/whirlwind1_hd.flac",
)
TRIM_SECONDS = 0.350
CROSSFADE_SECONDS = 0.500


def rms(samples):
    return float(np.sqrt(np.mean(samples * samples)))


def loop_audio(original, rate):
    # The opening is a cast attack. Keep the established spinning texture.
    sustain = original[round(TRIM_SECONDS * rate):].copy()
    overlap = round(CROSSFADE_SECONDS * rate)
    if len(sustain) <= 3 * overlap:
        raise ValueError("Recording is too short for the sustain/crossfade settings")
    t = np.linspace(0.0, 1.0, overlap)
    # Smoothstep gives the equal-power blend a flat slope at both joins.
    angle = (3 * t * t - 2 * t * t * t) * (np.pi / 2)
    blend = (sustain[-overlap:] * np.cos(angle)[:, None]
             + sustain[:overlap] * np.sin(angle)[:, None])
    loop = np.concatenate((sustain[overlap:-overlap], blend))

    # Process the whole period circularly: ordinary filters or fades at the
    # file edges would introduce a new discontinuity or a recurring silence.
    loop -= np.mean(loop, axis=0)
    frequency = np.fft.rfftfreq(len(loop), 1.0 / rate)
    # Gentle rumble reduction and a soft treble roll-off, retaining the wind.
    highpass = frequency / np.sqrt(frequency * frequency + 25.0 ** 2)
    lowpass = 1.0 / np.sqrt(1.0 + (frequency / 5500.0) ** 4)
    loop = np.fft.irfft(np.fft.rfft(loop, axis=0)
                        * (highpass * lowpass)[:, None], n=len(loop), axis=0)

    # A shared stereo envelope preserves the image. Limit leveling to 2 dB
    # in either direction so the natural swirl remains audible.
    energy = np.mean(loop * loop, axis=1)
    smoothing = np.exp(-0.5 * (2 * np.pi * frequency * 0.100) ** 2)
    envelope = np.sqrt(np.maximum(
        np.fft.irfft(np.fft.rfft(energy) * smoothing, n=len(loop)), 1e-12))
    gain = np.clip(np.median(envelope) / envelope,
                   10 ** (-2.0 / 20), 10 ** (2.0 / 20))
    loop *= gain[:, None]
    # Slightly quieter than the established original, with clipping headroom.
    loop *= (rms(sustain) * 10 ** (-1.5 / 20)) / rms(loop)
    loop *= min(1.0, 0.89 / float(np.max(np.abs(loop))))

    # Rotate the period to a quiet crossing. This changes only its starting
    # point; the circular crossfade and all sample adjacency are preserved.
    previous = np.roll(loop, 1, axis=0)
    score = np.mean(loop * loop + previous * previous
                    + 4.0 * (loop - previous) ** 2, axis=1)
    crossing = int(np.argmin(score))
    return np.roll(loop, -crossing, axis=0)


def measurements(samples, rate):
    # Compare the wrap to normal adjacent samples, not just a zero endpoint.
    differences = samples - np.roll(samples, 1, axis=0)
    wrap = float(np.max(np.abs(differences[0])))
    typical_step = rms(differences[1:])
    window = round(0.100 * rate)
    levels = np.array([rms(samples[i:i + window])
                       for i in range(0, len(samples) - window + 1, window)])
    around_wrap = np.concatenate((samples[-window // 2:], samples[:window // 2]))
    return {
        "frames": len(samples),
        "sample_rate": rate,
        "channels": samples.shape[1],
        "seconds": len(samples) / rate,
        "peak_dbfs": float(20 * np.log10(np.max(np.abs(samples)))),
        "rms_dbfs": float(20 * np.log10(rms(samples))),
        "wrap_step": wrap,
        "wrap_step_vs_typical": wrap / typical_step,
        "wrap_rms_vs_body_db": float(20 * np.log10(rms(around_wrap) / rms(samples))),
        "100ms_level_range_db": float(20 * np.log10(levels.max() / levels.min())),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", required=True, type=Path,
                        help="Extracted original D2R data folder (not the mod folder)")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.output.resolve() == args.data_root.resolve():
        parser.error("Output must be separate from the original data folder")
    args.output.mkdir(parents=True, exist_ok=True)
    report = {
        "trim_seconds": TRIM_SECONDS,
        "crossfade_seconds": CROSSFADE_SECONDS,
        "numpy_version": np.__version__,
        "soundfile_version": sf.__version__,
        "assets": [],
    }
    for relative in ASSETS:
        source = args.data_root / relative
        info = sf.info(source)
        if info.format != "FLAC" or info.subtype != "PCM_16":
            raise ValueError(f"Unexpected original encoding: {source}: {info}")
        original, rate = sf.read(source, always_2d=True, dtype="float64")
        edited = loop_audio(original, rate)
        destination = args.output / "data" / relative
        if source.resolve() == destination.resolve():
            raise ValueError("Refusing to replace the input recording")
        destination.parent.mkdir(parents=True, exist_ok=True)
        sf.write(destination, edited, rate, format="FLAC", subtype=info.subtype)
        decoded, decoded_rate = sf.read(destination, always_2d=True)
        if decoded_rate != rate or decoded.shape != edited.shape:
            raise ValueError("Encoded format or length changed")
        after = measurements(decoded, rate)
        if (not np.all(np.isfinite(decoded)) or after["peak_dbfs"] > -1
                or after["wrap_step_vs_typical"] > 1
                or abs(after["wrap_rms_vs_body_db"]) > 3):
            raise ValueError(f"Loop verification failed: {after}")
        report["assets"].append({
            "file": "data/" + relative,
            "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
            "output_sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
            "original": measurements(original, rate),
            "loop": after,
        })
        if relative.startswith("hd/"):
            # Four exact decoded periods, with fades only on the outer edges
            # of the preview. The three interior repeats are untouched.
            preview = np.tile(decoded, (4, 1))
            fade = np.linspace(0, 1, round(rate * .025))[:, None]
            preview[:len(fade)] *= fade
            preview[-len(fade):] *= fade[::-1]
            sf.write(args.output / "whirlwind-loop-preview.wav", preview,
                     rate, subtype="PCM_16")
            before = np.tile(original, (4, 1))
            sf.write(args.output / "whirlwind-original-repeated.wav", before,
                     rate, subtype="PCM_16")
    (args.output / "loop-report.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
