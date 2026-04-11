"""Tunable constants for the console airplane battle game."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
from typing import List, Tuple

# Grid (portrait-ish)
COLS = 50
ROWS = 24

FRAME_DT = 0.09
STARTING_LIVES = 3
INVINCIBLE_TICKS = 18

PLAYER_SHOOT_COOLDOWN_BASE = 5
PLAYER_MOVE_EVERY_N_FRAMES = 1

# Enemy spawn layout
ENEMY_SPAWN_TOP_ROW = 2
ENEMY_SPAWN_MAX_COL = COLS - 3

COMBO_WINDOW_TICKS = 45
COMBO_MULTIPLIER_STEP = 0.25  # +25% per kill within window
BASE_KILL_SCORE = 100

ITEM_DROP_CHANCE = 0.22


class EnemyKind(Enum):
    SMALL = auto()
    MEDIUM = auto()
    FAST = auto()


class ItemKind(Enum):
    FIRE = auto()  # faster fire + optional double shot tier
    SHIELD = auto()
    BOMB = auto()  # inventory; use with key
    BONUS_SCORE = auto()


@dataclass(frozen=True)
class WaveSpec:
    """One wave: list of (kind, count). Wave 6 is BOSS-only (handled in code)."""

    segments: List[Tuple[EnemyKind, int]]


# Design doc: 6 waves — types and counts
WAVES: List[WaveSpec] = [
    WaveSpec([(EnemyKind.SMALL, 10)]),
    WaveSpec([(EnemyKind.MEDIUM, 8)]),
    WaveSpec([(EnemyKind.SMALL, 6), (EnemyKind.MEDIUM, 6)]),
    WaveSpec([(EnemyKind.FAST, 10)]),
    WaveSpec([(EnemyKind.SMALL, 5), (EnemyKind.MEDIUM, 5), (EnemyKind.FAST, 5)]),
]

BOSS_MAX_HP = 60
# Phase thresholds: HP below (phase_index+1) * segment -> advance visual phase
BOSS_PHASE_HP_FRACTIONS = (2 / 3, 1 / 3)  # phase 2 below 2/3, phase 3 below 1/3

BOSS_WIDTH = 5
BOSS_HEIGHT = 2
BOSS_MOVE_INTERVAL = 2
BOSS_SHOOT_INTERVAL_P1 = 8
BOSS_SHOOT_INTERVAL_P2 = 6
BOSS_SHOOT_INTERVAL_P3 = 14
BOSS_LASER_WARN_TICKS = 4
BOSS_LASER_DAMAGE_ROW = 3  # row index from top of playfield where laser hits (0-based in arena)

# Per-kind tuning
ENEMY_HP = {
    EnemyKind.SMALL: 1,
    EnemyKind.MEDIUM: 2,
    EnemyKind.FAST: 1,
}
ENEMY_MOVE_INTERVAL = {
    EnemyKind.SMALL: 2,
    EnemyKind.MEDIUM: 2,
    EnemyKind.FAST: 1,
}
ENEMY_SHOOT_CHANCE = {
    EnemyKind.SMALL: 0.012,
    EnemyKind.MEDIUM: 0.02,
    EnemyKind.FAST: 0.015,
}

PLAYER_BULLET_SPEED = 1  # rows per tick when fired
ENEMY_BULLET_SPEED = 1

FIRE_UPGRADE_SHOOT_COOLDOWN = 3
FIRE_DOUBLE_TIER_THRESHOLD = 2  # pickups after first upgrade enable double shot

SHIELD_HITS_PER_PICKUP = 2

BOMB_KEY = b"b"
QUIT_KEY = b"q"
