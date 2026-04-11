"""Console airplane battle — entry, input, scenes, render."""

from __future__ import annotations

import os
import sys
import time
from typing import Optional

import msvcrt

from config import COLS, FRAME_DT, QUIT_KEY, ROWS, BOMB_KEY
from entities import BulletOwner
from systems import World, build_world


def clear_screen() -> None:
    os.system("cls" if os.name == "nt" else "clear")


def read_keys() -> bytes:
    """Drain non-blocking key buffer; return last key (arrow pairs kept as 2 bytes)."""
    last = b""
    while msvcrt.kbhit():
        ch = msvcrt.getch()
        if ch in (b"\xe0", b"\x00") and msvcrt.kbhit():
            last = ch + msvcrt.getch()
        else:
            last = ch
    return last


def render(world: World) -> None:
    grid = [[" " for _ in range(COLS)] for _ in range(ROWS)]

    # Laser warning row (boss phase 3)
    b = world.boss
    warn_row: Optional[int] = None
    if b is not None and b.laser_warn > 0 and b.laser_active_row is not None:
        warn_row = b.laser_active_row
        for x in range(COLS):
            grid[warn_row][x] = "~"

    for e in world.enemies:
        if 0 <= e.row < ROWS and 0 <= e.col < COLS:
            grid[e.row][e.col] = {"SMALL": "v", "MEDIUM": "V", "FAST": "w"}[e.kind.name]

    if world.boss is not None:
        for bx, by in world.boss.cells():
            if 0 <= by < ROWS and 0 <= bx < COLS:
                grid[by][bx] = "#"

    for bul in world.bullets:
        r, c = int(round(bul.row)), bul.col
        if 0 <= r < ROWS and 0 <= c < COLS:
            ch = "|" if bul.owner == BulletOwner.PLAYER else "*"
            if grid[r][c] == " " or grid[r][c] == "~":
                grid[r][c] = ch

    for it in world.items:
        r, c = int(it.row), it.col
        if 0 <= r < ROWS and 0 <= c < COLS:
            mark = {"FIRE": "F", "SHIELD": "S", "BOMB": "O", "BONUS_SCORE": "P"}[it.kind.name]
            if grid[r][c] == " " or grid[r][c] == "~":
                grid[r][c] = mark

    pr, pc = world.player.row, world.player.col
    blink_inv = world.tick < world.invincible_until and (world.tick // 2) % 2 == 0
    if 0 <= pr < ROWS and 0 <= pc < COLS and not blink_inv:
        grid[pr][pc] = "^"

    lines = ["".join(row) for row in grid]
    hud = (
        f"Score: {world.score}  Lives: {world.lives}  Wave: {min(world.wave_idx + 1, 6)}/6"
        f"  S:{world.player.shield_charges} B:{world.player.bombs}  {world.status_msg}"
    )
    clear_screen()
    print(hud[: COLS + 40])
    print("+" + "-" * COLS + "+")
    for line in lines:
        print("|" + line + "|")
    print("+" + "-" * COLS + "+")
    print("A/D move  W/S vertical  SPACE shoot  B bomb  Q quit")


def handle_playing_keys(world: World, key: bytes) -> None:
    if not key:
        return
    if len(key) == 1:
        low = key.lower()
        if low == QUIT_KEY:
            world.game_over = True
            world.status_msg = "Quit"
            return
        if low == BOMB_KEY:
            world.use_bomb()
            return
    k = key.lower() if len(key) == 1 else key
    p = world.player
    if k == b"a":
        p.col = max(0, p.col - 1)
    if k == b"d":
        p.col = min(COLS - 1, p.col + 1)
    if k == b"w":
        p.row = max(ROWS // 2, p.row - 1)
    if k == b"s":
        p.row = min(ROWS - 2, p.row + 1)
    if len(key) == 2 and key[0:1] in (b"\xe0", b"\x00"):
        arrow = key[1:2]
        if arrow == b"K":
            p.col = max(0, p.col - 1)
        elif arrow == b"M":
            p.col = min(COLS - 1, p.col + 1)
        elif arrow == b"H":
            p.row = max(ROWS // 2, p.row - 1)
        elif arrow == b"P":
            p.row = min(ROWS - 2, p.row + 1)
    if key == b" ":
        world.player_shoot()


def menu_loop() -> str:
    clear_screen()
    print("=== AIRPLANE BATTLE (console) ===\n")
    print("1) Start game")
    print("2) Quit")
    print("\nChoice: ", end="", flush=True)
    while True:
        ch = msvcrt.getch()
        if ch == b"1":
            return "play"
        if ch == b"2" or ch.lower() == QUIT_KEY:
            return "quit"
    return "quit"


def boot_screen() -> None:
    clear_screen()
    print("Loading...\n")
    print("6 waves. Boss on wave 6. Three lives. Good luck!")
    time.sleep(1.2)


def play_game(world: World) -> None:
    world.reset()
    while not world.game_over and not world.victory:
        key = read_keys()
        handle_playing_keys(world, key)
        world.update()
        render(world)
        time.sleep(FRAME_DT)
    render(world)
    print("\nPress any key...")
    msvcrt.getch()


def end_screen(title: str, world: World) -> None:
    clear_screen()
    print(title)
    print(f"Final score: {world.score}")
    print("\nPress any key for menu...")
    msvcrt.getch()


def main() -> None:
    if os.name != "nt":
        print("This build uses msvcrt; run on Windows or adapt input.", file=sys.stderr)
        sys.exit(1)

    boot_screen()
    world = build_world()

    while True:
        choice = menu_loop()
        if choice == "quit":
            clear_screen()
            print("Bye!")
            break
        play_game(world)
        if world.victory:
            end_screen("MISSION CLEAR!", world)
        else:
            end_screen("GAME OVER", world)


if __name__ == "__main__":
    main()
