"""Compile verified player-script inventory metadata into a small C table.

This runs on the developer's machine during build.py. It reads no game files;
the shipped EXE uses the resulting table to validate each player's own disc.
"""

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INVENTORY = ROOT / "config/player_script_baselines.json"


def require(ok, message):
    if not ok:
        raise ValueError(message)


def script_row(script):
    sound = script["original_launch_sound"]
    wait = sound["wait_immediately_before"]
    spawns = script["spawns"]
    require(1 <= len(spawns) <= 3, "unsupported projectile count")
    for field in ("file_start", "file_end", "model_preload_offset", "sprite_preload_offset"):
        require(0 <= script[field] < 2**32, f"invalid {field}")
    require(0 < script["file_start"] < script["file_end"], "invalid script range")
    values = {
        "start": script["file_start"],
        "end": script["file_end"],
        "model_preload": script["model_preload_offset"],
        "sprite_preload": script["sprite_preload_offset"],
        "sound_offset": sound["file_offset"],
        "wait_offset": wait["file_offset"] if wait else "NATIVE_NO_WAIT",
        "model": script["original_projectile_model"],
        "sprite": script["original_launch_sprite"],
        "sound": sound["id"],
        "duration": sound["duration"],
        "wait_frames": wait["frames"] if wait else 0,
        "spawn_count": len(spawns),
    }
    for field in ("model", "sprite", "sound"):
        require(0 < values[field] <= 32767, f"invalid {field}")
    require(0 <= values["duration"] <= 32767, "invalid sound duration")
    require(0 <= values["wait_frames"] <= 32767, "invalid wait")
    fields = ", ".join(f".{key} = {value}" for key, value in values.items())
    spawn_fields = ", ".join(
        f"{{{spawn['model_spawn_offset']}, {spawn['sprite_play_offset']}}}"
        for spawn in spawns
    )
    return "{ " + fields + ", .spawns = {" + spawn_fields + "} }"


def generate():
    inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
    require(inventory["schema_version"] == 1, "unsupported inventory schema")
    fighters = inventory["fighters"]
    require(len(fighters) == 44, "expected 44 fighter archives")
    require(sorted(f["kind"] for f in fighters) == list(range(9, 53)),
            "unexpected fighter kinds")
    ids = [f["effects_file_id"] for f in fighters]
    require(len(set(ids)) == 44, "duplicate archive file ID")
    rows = []
    for fighter in fighters:
        digest = bytes.fromhex(fighter["source_file_sha256"])
        require(len(digest) == 32, "invalid source SHA-256")
        hash_bytes = ", ".join(f"0x{byte:02x}" for byte in digest)
        scripts = fighter["scripts"]
        row = (f"    {{ .file_id = {fighter['effects_file_id']}, "
               f".source_sha256 = {{{hash_bytes}}},\n"
               f"      .scripts = {{{script_row(scripts['9'])},\n"
               f"                  {script_row(scripts['13'])}}} }},")
        rows.append(row)
    return ("/* Generated from config/player_script_baselines.json; do not edit. */\n"
            "#define NATIVE_LAUNCH_ARCHIVE_COUNT 44\n"
            "static const NativeLaunchArchive NATIVE_LAUNCH_ARCHIVES[44] = {\n"
            + "\n".join(rows) + "\n};\n")


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: generate_launch_manifest.py OUTPUT")
    output = Path(sys.argv[1])
    text = generate()
    if not output.exists() or output.read_text(encoding="utf-8") != text:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
