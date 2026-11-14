"""ROM set definition for Cyvern: The Dragon Weapons (MAME 0.264 'cyvern',
US version) and the Super Kaneko Nova System BIOS ('skns', USA BIOS).

Only names, sizes and checksums are stored here; the user supplies the files.
"""
from __future__ import annotations

import hashlib
import zipfile
import zlib
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class RomFile:
    name: str
    size: int
    crc: int
    sha1: str
    role: str


GAME_FILES = [
    RomFile("cv-usa.u10", 0x100000, 0x1023DDCA, "7967e3e876cdb797bdaa2eb5136a33cd43941501", "SH-2 program (even bytes)"),
    RomFile("cv-usa.u8", 0x100000, 0xF696F6BE, "d9e66173ca12693255d2bb0982da2fb96bfd155d", "SH-2 program (odd bytes)"),
    RomFile("cv100-00.u24", 0x400000, 0xCD4AE88A, "925f4ae01a6ad3633be2a61be69e163f05401cf6", "sprites (RLE)"),
    RomFile("cv101-00.u20", 0x400000, 0xA6CB3F0B, "8d83f44a096ca0a70962ca4c602c4331874c8560", "sprites (RLE)"),
    RomFile("cv200-00.u16", 0x400000, 0xDDC8C67E, "9b99e87e69e88011e6d693d19ac5e115b4fa50b0", "tilemap A tiles"),
    RomFile("cv201-00.u13", 0x400000, 0x65863321, "b8b75f50406068ffc3fca3887d2f0a653ca491c9", "tilemap A tiles"),
    RomFile("cv210-00.u18", 0x400000, 0x7486BF3A, "3b4285ca570e9c5ad396c615bfc054372d1b0162", "tilemap B tiles"),
    RomFile("cv300-00.u4", 0x400000, 0xFBEDA465, "4d5066a22f4589b6b7f85b3e77c348d900ac4bdd", "YMZ280B samples"),
]

# MAME 'skns' BIOS set; the game's default is the USA BIOS.
BIOS_FILES = {
    "usa": RomFile("sknsu1.u10", 0x80000, 0x384D21EC, "a27e8a18099d9cea64fa32db28d01101c2a78815", "SKNS BIOS (USA)"),
    "japan": RomFile("sknsj1.u10", 0x80000, 0x7E2B836C, "92c5a7a2472496028bff0e5980d41dd294f42144", "SKNS BIOS (Japan)"),
    "europe": RomFile("sknse2.u10", 0x80000, 0xE2B9D7D1, "b530a3bb9dedc8cfafcba9f1f10277590be04a15", "SKNS BIOS (Europe)"),
    "asia": RomFile("sknsa1.u10", 0x80000, 0x745E5212, "caba649ab2d83b2d7e007eecee0fc582c019df38", "SKNS BIOS (Asia)"),
    "korea": RomFile("sknsk1.u10", 0x80000, 0xFF1C9F79, "a51e598d43e76d37da69b1f094c111273bdfc94a", "SKNS BIOS (Korea)"),
}
DEFAULT_BIOS = "usa"

OTHER_REVISIONS = {"cv-jpn.u10": "cyvernj (Japan)"}

GAME_BASE = 0x04000000
GAME_SIZE = 0x200000
BIOS_BASE = 0x00000000
BIOS_SIZE = 0x80000


def _read(zip_path: Path, name: str) -> bytes | None:
    with zipfile.ZipFile(zip_path) as z:
        for info in z.infolist():
            if info.filename.split("/")[-1].lower() == name.lower():
                return z.read(info)
    return None


def check(files: list[RomFile], zip_path: Path) -> list[tuple[RomFile, str]]:
    out = []
    for f in files:
        data = _read(zip_path, f.name)
        if data is None:
            out.append((f, "MISSING"))
        elif len(data) != f.size or zlib.crc32(data) != f.crc or hashlib.sha1(data).hexdigest() != f.sha1:
            out.append((f, "BAD"))
        else:
            out.append((f, "OK"))
    return out


def build_game_program(zip_path: Path) -> bytes:
    """user1 region: ROM_LOAD16_BYTE even/odd interleave, big-endian, 2 MiB."""
    even = _read(zip_path, "cv-usa.u10")
    odd = _read(zip_path, "cv-usa.u8")
    if even is None or odd is None:
        raise SystemExit(f"{zip_path}: cv-usa.u10 / cv-usa.u8 not found (is this the cyvern set?)")
    image = bytearray(GAME_SIZE)
    image[0::2] = even
    image[1::2] = odd
    return bytes(image)


def build_bios(zip_path: Path, region: str = DEFAULT_BIOS) -> bytes:
    f = BIOS_FILES[region]
    data = _read(zip_path, f.name)
    if data is None:
        raise SystemExit(f"{zip_path}: {f.name} not found (the SKNS BIOS set skns.zip is required)")
    return data


def main(argv: list[str]) -> int:
    """Check a cyvern.zip (and skns.zip) against the expected SHA-1s."""
    if len(argv) < 2:
        print("usage: python tools/rom_set.py cyvern.zip [skns.zip]")
        return 2
    game = Path(argv[1])
    bios = Path(argv[2]) if len(argv) > 2 else game
    ok = True
    for f, status in check(GAME_FILES, game):
        print(f"{game.name}: {f.name}: {status}")
        ok = ok and status == "OK"
    for f, status in check([BIOS_FILES[DEFAULT_BIOS]], bios):
        print(f"{bios.name}: {f.name}: {status}")
        ok = ok and status == "OK"
    print("ROM set OK (cyvern + skns USA BIOS)" if ok else "ROM set check FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    import sys
    raise SystemExit(main(sys.argv))
