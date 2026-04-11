"""Game entities: player, enemies, bullets, items, boss."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
from typing import List, Optional

from config import BOSS_HEIGHT, BOSS_WIDTH, EnemyKind, ItemKind


class BulletOwner(Enum):
    PLAYER = auto()
    ENEMY = auto()
    BOSS = auto()


@dataclass
class Player:
    col: int
    row: int
    shoot_cooldown: int = 0
    fire_level: int = 0  # 0 base, 1 upgraded cooldown, 2+ double shot
    shield_charges: int = 0
    bombs: int = 0


@dataclass
class Enemy:
    col: int
    row: int
    kind: EnemyKind
    hp: int
    move_timer: int = 0


@dataclass
class Bullet:
    col: int
    row: float
    owner: BulletOwner
    dy: float  # rows per tick (negative = up)


@dataclass
class Item:
    col: int
    row: float
    kind: ItemKind


@dataclass
class Boss:
    col: int
    row: int
    hp: int
    max_hp: int
    phase: int  # 1, 2, or 3
    move_timer: int = 0
    shoot_timer: int = 0
    laser_warn: int = 0  # >0 countdown warning rows
    laser_active_row: Optional[int] = None  # row that damages this tick

    def cells(self) -> List[tuple[int, int]]:
        out: List[tuple[int, int]] = []
        for dy in range(BOSS_HEIGHT):
            for dx in range(BOSS_WIDTH):
                out.append((self.col + dx, self.row + dy))
        return out
