export const GAME_CONFIG = {
  width: 480,
  height: 720,
  playerSpeed: 300,
  playerHealth: 3,
  bulletSpeed: 500,
  enemyBulletSpeed: 300
}

export const ENEMY_TYPES = {
  SMALL: 'small',
  MEDIUM: 'medium',
  FAST: 'fast',
  BOSS: 'boss'
}

export const ITEM_TYPES = {
  POWER_UP: 'powerup',
  SHIELD: 'shield',
  BOMB: 'bomb',
  EXTRA_LIFE: 'extralife'
}

export const WAVES = [
  { wave: 1, enemies: [{ type: ENEMY_TYPES.SMALL, count: 10 }] },
  { wave: 2, enemies: [{ type: ENEMY_TYPES.MEDIUM, count: 8 }] },
  { wave: 3, enemies: [{ type: ENEMY_TYPES.SMALL, count: 6 }, { type: ENEMY_TYPES.MEDIUM, count: 6 }] },
  { wave: 4, enemies: [{ type: ENEMY_TYPES.FAST, count: 10 }] },
  { wave: 5, enemies: [{ type: ENEMY_TYPES.SMALL, count: 8 }, { type: ENEMY_TYPES.MEDIUM, count: 4 }, { type: ENEMY_TYPES.FAST, count: 3 }] },
  { wave: 6, enemies: [{ type: ENEMY_TYPES.BOSS, count: 1 }] }
]

export const SCORES = {
  [ENEMY_TYPES.SMALL]: 100,
  [ENEMY_TYPES.MEDIUM]: 200,
  [ENEMY_TYPES.FAST]: 300,
  [ENEMY_TYPES.BOSS]: 5000
}
