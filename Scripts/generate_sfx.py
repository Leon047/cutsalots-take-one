"""Generate The Saga Cutter sound effects with the ElevenLabs Sound Effects API.

Usage:
    py Scripts/generate_sfx.py                    # all sounds (2 takes of ocean_loop, 1 of the rest)
    py Scripts/generate_sfx.py --takes 3          # 3 takes per sound
    py Scripts/generate_sfx.py --only gull_1 boat_land
    py Scripts/generate_sfx.py --force            # regenerate files that already exist (spends credits)

Files land in Docs/Audio/SFX/ as 48 kHz 16-bit WAV (outside UE/Content so Unreal
doesn't auto-import them). Sounds that already exist on disk are skipped.
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path

from dotenv import load_dotenv

ROOT = Path(__file__).resolve().parent.parent
# Must run before importing elevenlabs: the SDK reads ELEVENLABS_API_KEY as a
# default argument, which Python evaluates at import time.
# override=True: a stale ELEVENLABS_API_KEY in the shell must not shadow the file.
load_dotenv(ROOT / ".env.local", override=True)

from elevenlabs import ElevenLabs  # noqa: E402

OUT_DIR = ROOT / "Docs" / "Audio" / "SFX"
MODEL_ID = "eleven_text_to_sound_v2"
OUTPUT_FORMAT = "mp3_44100_128"
FFMPEG = "ffmpeg"
CROSSFADE_S = 1.5  # fallback loop seam when the API rejects loop=True

GULL = "A single distant seagull calling {}, far away, natural outdoor reverb, nothing else."
CREAK = (
    "Wooden ship hull creaking slowly as it rocks at its mooring, rope strain, "
    "close and dry, no water splash, no voices."
)
WADE = "One single footstep wading through knee-deep calm water, a small soft splash, close, nothing else."
RAVEN = "A single raven croaking {}, far away across a mountain valley, natural outdoor echo, nothing else."
OWL = "A single owl hooting {} in a quiet night forest, distant, natural outdoor reverb, nothing else."

SOUNDS = {
    "dog_attack_bark": {
        "seconds": 1.2, "loop": False, "channels": 1,
        "prompt": (
            "One sharp, deep, aggressive bark from a medium Siberian husky lunging to defend its owner. "
            "A single forceful chesty bark, up close and dry with a short natural tail. "
            "No playful yip, voices, other animals, footsteps, ambience, music, or echo."
        ),
    },
    "dog_growl": {
        "seconds": 1.5, "loop": False, "channels": 1,
        "prompt": (
            "One short, low, throaty warning growl from a medium Siberian husky about to charge an intruder. "
            "Tense canine snarl that rises briefly and cuts off, no bark. Dry isolated recording with no "
            "voices, other animals, footsteps, ambience, music, or echo."
        ),
    },
    "dog_bark": {
        "seconds": 1.8, "loop": False, "channels": 1, "takes": 2,
        "prompt": (
            "Isolated close recording of one medium-sized Siberian husky giving two quick, bright, "
            "playful greeting barks, friendly and excited with a natural voice. Clean silence before "
            "and after. No snarls, human voices, ambience, music, or echo."
        ),
    },
    "dog_pant": {
        "seconds": 3, "loop": False, "channels": 1,
        "prompt": (
            "Isolated close recording of a medium-sized husky panting heavily after running, "
            "several rapid, soft open-mouth breaths with natural variation. No bark, whine, "
            "footsteps, collar, wind, music, or reverb."
        ),
    },
    "dog_howl": {
        "seconds": 5, "loop": False, "channels": 1,
        "prompt": (
            "One lonely Siberian husky gives a single long, mournful howl. A soft breath into a "
            "rising wavering ah-woo, sustained and then gently trailing off into silence. "
            "Natural domestic dog voice, isolated dry recording, no pack, wind, music, or echo."
        ),
    },
    "dog_howl_soft": {
        "seconds": 4, "loop": False, "channels": 1,
        "prompt": (
            "One lone Siberian husky, close and dry. A small, broken whimper rises into a soft, "
            "high mournful howl, wavers once, then falls into silence. Intimate and vulnerable, "
            "as if calling for a missing owner. No barks, other animals, voices, wind, or music."
        ),
    },
    "dog_howl_shaky": {
        "seconds": 4.5, "loop": False, "channels": 1,
        "prompt": (
            "Close dry recording of one lonely Siberian husky. Two small, trembling canine "
            "whimpers, then a deeply sad, shaky howl that loses strength and ends in a little "
            "whine. Natural domestic dog vocalization. No speech, barking, music, or background."
        ),
    },
    "dog_howl_broken": {
        "seconds": 5.5, "loop": False, "channels": 1,
        "prompt": (
            "One grieving Siberian husky howls alone. A plaintive low-to-high call cracks in the "
            "middle, pauses for a breath, then tries again with a shorter, weaker cry. Very sad, "
            "realistic canine voice. Isolated dry sound, no barking, people, music, or other animals."
        ),
    },
    "dog_howl_quiet": {
        "seconds": 5, "loop": False, "channels": 1,
        "prompt": (
            "One Siberian husky quietly calling for its missing owner. A soft nasal whine slips "
            "into one low, mournful descending howl, wavering and fading into silence. "
            "Close dry animal recording. No bark, growl, voices, wind, reverb, or music."
        ),
    },
    "dog_howl_long": {
        "seconds": 6, "loop": False, "channels": 1,
        "prompt": (
            "One natural Siberian husky gives a long, low, full-throated sorrowful howl. "
            "An audible inhale, a slow rounded rise to one sustained ringing note, then a long "
            "downward tail that fades away. Isolated and dry, no pack, barking, wind, echo, or music."
        ),
    },
    "ocean_loop": {
        "seconds": 30, "loop": True, "channels": 2, "takes": 2,
        "prompt": (
            "Calm cold northern fjord, small waves washing steadily onto a pebble shore, "
            "soft continuous water movement, faint breeze. No birds, no voices, no music, "
            "no thunder. Even level, seamless loop."
        ),
    },
    "gull_1": {"seconds": 4, "loop": False, "channels": 1, "prompt": GULL.format("two or three times, one long cry")},
    "gull_2": {"seconds": 4, "loop": False, "channels": 1, "prompt": GULL.format("two or three times, short chattering cries")},
    "gull_3": {"seconds": 4, "loop": False, "channels": 1, "prompt": GULL.format("two or three times over open water")},
    "boat_creak_1": {"seconds": 3, "loop": False, "channels": 1, "prompt": CREAK},
    "boat_creak_2": {"seconds": 3, "loop": False, "channels": 1, "prompt": CREAK},
    "boat_land": {
        "seconds": 4, "loop": False, "channels": 1,
        "prompt": (
            "A wooden boat keel sliding onto a gravel beach with a soft splash of "
            "shallow water, then settling. No voices."
        ),
    },
    "wade_1": {"seconds": 2, "loop": False, "channels": 1, "prompt": WADE},
    "wade_2": {"seconds": 2, "loop": False, "channels": 1, "prompt": WADE},
    "river_loop": {
        "seconds": 30, "loop": True, "channels": 2, "takes": 2,
        "prompt": (
            "A small shallow river flowing steadily over stones, gentle continuous babbling water, "
            "close, cold northern valley. No birds, no voices, no music, no waves. Even level, seamless loop."
        ),
    },
    "wind_loop": {
        "seconds": 30, "loop": True, "channels": 2, "takes": 2,
        "prompt": (
            "Cold wind blowing steadily across a high mountain slope and through distant pines, "
            "soft airy whoosh with slow gentle swells. No gusty roar, no birds, no voices, no music. "
            "Even level, seamless loop."
        ),
    },
    "birds_loop": {
        "seconds": 30, "loop": True, "channels": 2, "takes": 2,
        "prompt": (
            "Quiet northern pine forest in the morning, a few small songbirds calling sparsely at a distance, "
            "light air. No water, no wind noise, no voices, no music. Even level, seamless loop."
        ),
    },
    "raven_1": {"seconds": 4, "loop": False, "channels": 1, "prompt": RAVEN.format("two or three times")},
    "raven_2": {"seconds": 4, "loop": False, "channels": 1, "prompt": RAVEN.format("one long hoarse croak")},
    "owl_1": {"seconds": 4, "loop": False, "channels": 1, "prompt": OWL.format("softly twice")},
    "owl_2": {"seconds": 4, "loop": False, "channels": 1, "prompt": OWL.format("three slow hoots")},
}


def request(client: ElevenLabs, snd: dict, fancy: bool) -> bytes:
    kwargs = dict(
        text=snd["prompt"],
        duration_seconds=snd["seconds"],
        prompt_influence=0.5,
        output_format=OUTPUT_FORMAT,
    )
    if fancy:
        kwargs.update(model_id=MODEL_ID, loop=snd["loop"])
    return b"".join(client.text_to_sound_effects.convert(**kwargs))


def to_wav(mp3: Path, wav: Path, channels: int, crossfade: bool) -> None:
    cmd = [FFMPEG, "-y", "-loglevel", "error", "-i", str(mp3)]
    if crossfade:
        # Drop the first d seconds from the front and crossfade them back onto the end,
        # so the end flows into the start.
        d = CROSSFADE_S
        flt = (
            f"[0:a]asplit=2[h][m];"
            f"[h]atrim=0:{d},asetpts=PTS-STARTPTS[head];"
            f"[m]atrim=start={d},asetpts=PTS-STARTPTS[rest];"
            f"[rest][head]acrossfade=d={d}[o]"
        )
        cmd += ["-filter_complex", flt, "-map", "[o]"]
    cmd += ["-ar", "48000", "-ac", str(channels), "-c:a", "pcm_s16le", str(wav)]
    subprocess.run(cmd, check=True)


def generate(client: ElevenLabs, name: str, snd: dict, take: int) -> Path:
    crossfade = False
    try:
        data = request(client, snd, fancy=True)
    except Exception as e:
        status = getattr(e, "status_code", None)
        if status in (401, 403, 429) or status is None or status >= 500:
            raise
        # loop / model_id rejected: plain request, build the loop ourselves.
        print(f"  loop/model rejected ({status}), retrying plain", file=sys.stderr)
        data = request(client, snd, fancy=False)
        crossfade = snd["loop"]
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    mp3 = OUT_DIR / f"{name}_take{take}.mp3"
    wav = OUT_DIR / f"{name}_take{take}.wav"
    mp3.write_bytes(data)
    try:
        to_wav(mp3, wav, snd["channels"], crossfade)
    finally:
        mp3.unlink(missing_ok=True)
    return wav


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--takes", type=int, help="takes per sound (default 2 for ocean_loop, 1 otherwise)")
    parser.add_argument("--only", nargs="+", choices=SOUNDS, help="subset of sounds")
    parser.add_argument("--force", action="store_true", help="regenerate files that already exist")
    args = parser.parse_args()

    client = ElevenLabs()  # reads ELEVENLABS_API_KEY from the environment

    failures = 0
    for name in args.only or list(SOUNDS):
        snd = SOUNDS[name]
        takes = args.takes or snd.get("takes", 1)
        for take in range(1, takes + 1):
            wav = OUT_DIR / f"{name}_take{take}.wav"
            if wav.exists() and not args.force:
                print(f"[{name}] take {take} exists, skipped")
                continue
            print(f"[{name}] take {take}/{takes} ...", flush=True)
            start = time.time()
            try:
                path = generate(client, name, snd, take)
                print(f"  saved {path.relative_to(ROOT)} ({time.time() - start:.0f}s)")
            except Exception as e:  # SDK raises ApiError with status/body
                failures += 1
                print(f"  failed: {e}", file=sys.stderr)
                if getattr(e, "status_code", None) in (401, 403):
                    return 1  # bad key / plan doesn't allow it: stop early

    print(f"Done. Output folder: {OUT_DIR.relative_to(ROOT)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
