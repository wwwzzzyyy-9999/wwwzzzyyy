"""Collisions, waves, boss AI, items, scoring."""

from __future__ import annotations

import random
from dataclasses import dataclass, field
from typing import List, Optional, Set

from config import (
    BASE_KILL_SCORE,
    BOSS_LASER_WARN_TICKS,
    BOSS_MAX_HP,
    BOSS_MOVE_INTERVAL,
    BOSS_PHASE_HP_FRACTIONS,
    BOSS_SHOOT_INTERVAL_P1,
    BOSS_SHOOT_INTERVAL_P2,
    BOSS_SHOOT_INTERVAL_P3,
    BOSS_WIDTH,
    COLS,
    COMBO_MULTIPLIER_STEP,
    COMBO_WINDOW_TICKS,
    ENEMY_BULLET_SPEED,
    ENEMY_HP,
    ENEMY_MOVE_INTERVAL,
    ENEMY_SHOOT_CHANCE,
    ENEMY_SPAWN_MAX_COL,
    ENEMY_SPAWN_TOP_ROW,
    EnemyKind,
    FIRE_DOUBLE_TIER_THRESHOLD,
    FIRE_UPGRADE_SHOOT_COOLDOWN,
    INVINCIBLE_TICKS,
    ITEM_DROP_CHANCE,
    ItemKind,
    PLAYER_BULLET_SPEED,
    PLAYER_SHOOT_COOLDOWN_BASE,
    ROWS,
    SHIELD_HITS_PER_PICKUP,
    STARTING_LIVES,
    WAVES,
)
from entities import Boss, Bullet, BulletOwner, Enemy, Item, Player


def _boss_phase_from_hp(hp: int, max_hp: int) -> int:
    f1, f2 = BOSS_PHASE_HP_FRACTIONS
    if hp > max_hp * f1:
        return 1
    if hp > max_hp * f2:
        return 2
    return 3


@dataclass
class World:
    player: Player = field(default_factory=lambda: Player(col=COLS // 2, row=ROWS - 3))
    enemies: List[Enemy] = field(default_factory=list)
    bullets: List[Bullet] = field(default_factory=list)
    items: List[Item] = field(default_factory=list)
    boss: Optional[Boss] = None
    wave_idx: int = 0
    spawn_queue: List[EnemyKind] = field(default_factory=list)
    spawn_timer: int = 0
    score: int = 0
    lives: int = STARTING_LIVES
    tick: int = 0
    invincible_until: int = -1
    combo_timer: int = 0
    combo_kills: int = 0
    game_over: bool = False
    victory: bool = False
    status_msg: str = ""

    def reset(self) -> None:
        self.player = Player(col=COLS // 2, row=ROWS - 3)
        self.enemies.clear()
        self.bullets.clear()
        self.items.clear()
        self.boss = None
        self.wave_idx = 0
        self.spawn_queue.clear()
        self.spawn_timer = 0
        self.score = 0
        self.lives = STARTING_LIVES
        self.tick = 0
        self.invincible_until = -1
        self.combo_timer = 0
        self.combo_kills = 0
        self.game_over = False
        self.victory = False
        self.status_msg = ""
        self._begin_wave()

    def _begin_wave(self) -> None:
        self.spawn_queue.clear()
        if self.wave_idx >= len(WAVES):
            # Boss wave (wave 6, index 5 in 0-based counting of "sixth wave")
            self._spawn_boss()
            return
        spec = WAVES[self.wave_idx]
        for kind, count in spec.segments:
            self.spawn_queue.extend([kind] * count)
        random.shuffle(self.spawn_queue)
        self.spawn_timer = 3

    def _spawn_boss(self) -> None:
        center = max(1, (COLS - BOSS_WIDTH) // 2)
        self.boss = Boss(
            col=center,
            row=ENEMY_SPAWN_TOP_ROW,
            hp=BOSS_MAX_HP,
            max_hp=BOSS_MAX_HP,
            phase=1,
            shoot_timer=BOSS_SHOOT_INTERVAL_P1,
        )

    def _maybe_spawn_enemy(self) -> None:
        if self.boss is not None or not self.spawn_queue:
            return
        if self.spawn_timer > 0:
            self.spawn_timer -= 1
            return
        kind = self.spawn_queue.pop(0)
        col = random.randint(1, ENEMY_SPAWN_MAX_COL)
        hp = ENEMY_HP[kind]
        self.enemies.append(
            Enemy(col=col, row=ENEMY_SPAWN_TOP_ROW, kind=kind, hp=hp, move_timer=0)
        )
        self.spawn_timer = 4 if kind != EnemyKind.FAST else 3

    def _wave_cleared(self) -> bool:
        if self.boss is not None:
            return False
        return not self.spawn_queue and not self.enemies

    def _advance_wave(self) -> None:
        self.wave_idx += 1
        # wave_idx 0..4 = WAVES entries; wave_idx 5 = boss; no index 6 in normal flow
        if self.wave_idx > len(WAVES):
            return
        self._begin_wave()

    def _add_score_for_kill(self) -> None:
        if self.combo_timer > 0:
            self.combo_kills += 1
        else:
            self.combo_kills = 1
        self.combo_timer = COMBO_WINDOW_TICKS
        mult = 1.0 + COMBO_MULTIPLIER_STEP * max(0, self.combo_kills - 1)
        self.score += int(BASE_KILL_SCORE * mult)

    def _damage_player(self) -> None:
        if self.tick < self.invincible_until:
            return
        if self.player.shield_charges > 0:
            self.player.shield_charges -= 1
            self.status_msg = "Shield blocked hit"
            self.invincible_until = self.tick + INVINCIBLE_TICKS // 2
            return
        self.lives -= 1
        self.invincible_until = self.tick + INVINCIBLE_TICKS
        self.status_msg = "Hit!"
        if self.lives <= 0:
            self.game_over = True
            self.status_msg = "GAME OVER"

    def _try_drop_item(self, col: int, row: int) -> None:
        if random.random() > ITEM_DROP_CHANCE:
            return
        kind = random.choice(list(ItemKind))
        self.items.append(Item(col=col, row=float(row), kind=kind))

    def use_bomb(self) -> None:
        """Clears non-boss enemies and enemy bullets; does not damage boss."""
        if self.player.bombs <= 0:
            return
        self.player.bombs -= 1
        self.enemies.clear()
        self.bullets = [b for b in self.bullets if b.owner == BulletOwner.PLAYER]
        self.status_msg = "BOMB!"

    def player_shoot(self) -> None:
        p = self.player
        if p.shoot_cooldown > 0:
            return
        cd = (
            FIRE_UPGRADE_SHOOT_COOLDOWN
            if p.fire_level >= 1
            else PLAYER_SHOOT_COOLDOWN_BASE
        )
        p.shoot_cooldown = cd
        row = float(p.row)
        if p.fire_level >= FIRE_DOUBLE_TIER_THRESHOLD:
            for dc in (-1, 1):
                c = max(0, min(COLS - 1, p.col + dc))
                self.bullets.append(
                    Bullet(
                        col=c,
                        row=row - 0.1,
                        owner=BulletOwner.PLAYER,
                        dy=-PLAYER_BULLET_SPEED,
                    )
                )
        else:
            self.bullets.append(
                Bullet(
                    col=p.col,
                    row=row - 0.1,
                    owner=BulletOwner.PLAYER,
                    dy=-PLAYER_BULLET_SPEED,
                )
            )

    def update(self) -> None:
        if self.game_over or self.victory:
            return

        self.tick += 1
        if self.combo_timer > 0:
            self.combo_timer -= 1

        p = self.player
        if p.shoot_cooldown > 0:
            p.shoot_cooldown -= 1

        self._maybe_spawn_enemy()
        self._update_enemies()
        self._update_boss()
        self._update_bullets()
        self._update_items()
        self._collisions_player_vs_hazards()
        self._collisions_player_bullets()
        if self._wave_cleared():
            self._advance_wave()

    def _update_enemies(self) -> None:
        for e in self.enemies:
            e.move_timer += 1
            interval = ENEMY_MOVE_INTERVAL[e.kind]
            if e.move_timer < interval:
                continue
            e.move_timer = 0
            e.row += 1
            if self.tick % 5 == 0:
                if e.col < self.player.col:
                    e.col += 1
                elif e.col > self.player.col:
                    e.col -= 1
            e.col = max(0, min(COLS - 1, e.col))
            if random.random() < ENEMY_SHOOT_CHANCE[e.kind]:
                self.bullets.append(
                    Bullet(
                        col=e.col,
                        row=float(e.row) + 0.5,
                        owner=BulletOwner.ENEMY,
                        dy=ENEMY_BULLET_SPEED,
                    )
                )
            if e.row == self.player.row and e.col == self.player.col:
                self._damage_player()
                e.hp = 0

        self.enemies = [e for e in self.enemies if e.hp > 0 and e.row < ROWS]

    def _update_boss(self) -> None:
        b = self.boss
        if b is None:
            return

        b.phase = _boss_phase_from_hp(b.hp, b.max_hp)
        b.move_timer += 1
        if b.move_timer >= BOSS_MOVE_INTERVAL:
            b.move_timer = 0
            if random.random() < 0.5:
                b.col -= 1
            else:
                b.col += 1
            b.col = max(1, min(COLS - BOSS_WIDTH - 1, b.col))

        if b.laser_warn > 0:
            b.laser_warn -= 1
            if b.laser_warn == 0 and b.laser_active_row is not None:
                if self.player.row == b.laser_active_row and self.tick >= self.invincible_until:
                    self._damage_player()
                b.laser_active_row = None
            return

        b.shoot_timer -= 1
        if b.shoot_timer > 0:
            return

        if b.phase == 1:
            b.shoot_timer = BOSS_SHOOT_INTERVAL_P1
            base = b.col + BOSS_WIDTH // 2
            for dc in (-2, 0, 2):
                c = max(0, min(COLS - 1, base + dc))
                self.bullets.append(
                    Bullet(col=c, row=float(b.row + BOSS_HEIGHT), owner=BulletOwner.BOSS, dy=ENEMY_BULLET_SPEED)
                )
        elif b.phase == 2:
            b.shoot_timer = BOSS_SHOOT_INTERVAL_P2
            aim = self.player.col + random.randint(-1, 1)
            aim = max(0, min(COLS - 1, aim))
            self.bullets.append(
                Bullet(col=aim, row=float(b.row + BOSS_HEIGHT), owner=BulletOwner.BOSS, dy=ENEMY_BULLET_SPEED + 0.3)
            )
            self.bullets.append(
                Bullet(
                    col=max(0, aim - 2),
                    row=float(b.row + BOSS_HEIGHT),
                    owner=BulletOwner.BOSS,
                    dy=ENEMY_BULLET_SPEED,
                )
            )
        else:
            b.shoot_timer = BOSS_SHOOT_INTERVAL_P3
            # Laser: warn then full-width row damage at player area
            laser_row = random.randint(ROWS // 2, ROWS - 4)
            b.laser_warn = BOSS_LASER_WARN_TICKS
            b.laser_active_row = laser_row

    def _update_bullets(self) -> None:
        alive: List[Bullet] = []
        for bul in self.bullets:
            bul.row += bul.dy
            r = int(round(bul.row))
            c = bul.col
            if r < 0 or r >= ROWS or c < 0 or c >= COLS:
                continue
            alive.append(bul)
        self.bullets = alive

    def _update_items(self) -> None:
        alive: List[Item] = []
        for it in self.items:
            it.row += 0.35
            if int(it.row) >= ROWS:
                continue
            alive.append(it)
        self.items = alive

    def _collisions_player_vs_hazards(self) -> None:
        pc, pr = self.player.col, self.player.row
        for bul in self.bullets:
            if bul.owner == BulletOwner.PLAYER:
                continue
            if int(round(bul.row)) == pr and bul.col == pc:
                self._damage_player()
                bul.row = -999.0

        b = self.boss
        if b is not None:
            for bx, by in b.cells():
                if bx == pc and by == pr:
                    self._damage_player()

        self.bullets = [x for x in self.bullets if x.row > -500]

    def _collisions_player_bullets(self) -> None:
        new_bullets: List[Bullet] = []
        for bul in self.bullets:
            if bul.owner != BulletOwner.PLAYER:
                new_bullets.append(bul)
                continue
            br, bc = int(round(bul.row)), bul.col
            hit = False

            for e in self.enemies:
                if e.col == bc and (e.row == br or e.row - 1 == br):
                    e.hp -= 1
                    hit = True
                    if e.hp <= 0:
                        self._add_score_for_kill()
                        self._try_drop_item(e.col, e.row)
                    break

            if not hit and self.boss is not None:
                boss_cells: Set[tuple[int, int]] = set(self.boss.cells())
                if (bc, br) in boss_cells:
                    self.boss.hp -= 1
                    hit = True
                    if self.boss.hp <= 0:
                        self._add_score_for_kill()
                        self.boss = None
                        self.victory = True
                        self.status_msg = "BOSS DOWN! YOU WIN!"

            if not hit:
                new_bullets.append(bul)

        self.bullets = new_bullets

        # Item pickup
        pc, pr = self.player.col, self.player.row
        kept: List[Item] = []
        for it in self.items:
            ir = int(it.row)
            if it.col == pc and ir == pr:
                self._apply_item(it.kind)
            else:
                kept.append(it)
        self.items = kept

    def _apply_item(self, kind: ItemKind) -> None:
        if kind == ItemKind.FIRE:
            self.player.fire_level += 1
            self.status_msg = "Power up!"
        elif kind == ItemKind.SHIELD:
            self.player.shield_charges += SHIELD_HITS_PER_PICKUP
            self.status_msg = "Shield +"
        elif kind == ItemKind.BOMB:
            self.player.bombs += 1
            self.status_msg = "Bomb + (press B)"
        elif kind == ItemKind.BONUS_SCORE:
            self.score += 500
            self.status_msg = "+500"


def build_world() -> World:
    w = World()
    w.reset()
    return w
