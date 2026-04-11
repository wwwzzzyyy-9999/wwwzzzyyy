import Phaser from 'phaser'
import Player from '../entities/Player.js'
import { WAVES, ENEMY_TYPES, SCORES } from '../utils/constants.js'
import { randomInt, createExplosion } from '../utils/helpers.js'

export default class GameScene extends Phaser.Scene {
  constructor() {
    super('GameScene')
  }

  init() {
    this.score = 0
    this.lives = 3
    this.wave = 0
    this.enemiesRemaining = 0
    this.enemiesKilled = 0
    this.combo = 0
    this.maxCombo = 0
    this.isGameOver = false
  }

  create() {
    this.add.image(240, 360, 'background')
    
    this.createGroups()
    this.createPlayer()
    this.createUI()
    this.setupControls()
    this.setupCollisions()
    this.startWave()
  }

  createGroups() {
    this.bullets = this.add.group()
    this.enemyBullets = this.add.group()
    this.enemies = this.add.group()
    this.items = this.add.group()
  }

  createPlayer() {
    this.player = new Player(this, 240, 600)
  }

  createUI() {
    this.scoreText = this.add.text(10, 10, '得分：0', {
      font: '24px Arial',
      color: '#ffffff'
    })

    this.livesText = this.add.text(10, 40, '生命：3', {
      font: '24px Arial',
      color: '#ff0000'
    })

    this.waveText = this.add.text(10, 70, '波次：1/6', {
      font: '24px Arial',
      color: '#00ffff'
    })

    this.comboText = this.add.text(470, 10, '连击：0', {
      font: '20px Arial',
      color: '#ffff00'
    }).setOrigin(1, 0)

    this.bossHealthBar = this.add.graphics()
  }

  setupControls() {
    this.cursors = this.input.keyboard.createCursorKeys()
    this.wasd = this.input.keyboard.addKeys({
      up: Phaser.Input.Keyboard.KeyCodes.W,
      down: Phaser.Input.Keyboard.KeyCodes.S,
      left: Phaser.Input.Keyboard.KeyCodes.A,
      right: Phaser.Input.Keyboard.KeyCodes.D,
      fire: Phaser.Input.Keyboard.KeyCodes.SPACE
    })

    this.input.keyboard.on('keydown-SPACE', () => {
      if (!this.isGameOver && this.player.active) {
        this.player.fire()
      }
    })
  }

  setupCollisions() {
    this.physics.add.overlap(this.bullets, this.enemies, this.hitEnemy, null, this)
    this.physics.add.overlap(this.enemyBullets, this.player, this.playerHit, null, this)
    this.physics.add.overlap(this.enemies, this.player, this.playerHit, null, this)
    this.physics.add.overlap(this.items, this.player, this.collectItem, null, this)
  }

  startWave() {
    if (this.wave >= WAVES.length) {
      this.completeGame()
      return
    }

    const waveConfig = WAVES[this.wave]
    this.wave++
    this.enemiesRemaining = 0
    this.enemiesKilled = 0

    this.waveText.setText(`波次：${this.wave}/6`)

    waveConfig.enemies.forEach(enemyConfig => {
      this.spawnEnemies(enemyConfig.type, enemyConfig.count)
    })

    if (waveConfig.enemies.some(e => e.type === ENEMY_TYPES.BOSS)) {
      this.showBossWarning()
    }
  }

  spawnEnemies(type, count) {
    let spawned = 0
    const spawnInterval = this.time.addEvent({
      delay: 1000,
      callback: () => {
        if (spawned >= count) {
          spawnInterval.remove()
          return
        }

        let enemy
        const x = randomInt(50, 430)
        const y = -50

        switch (type) {
          case ENEMY_TYPES.SMALL:
            enemy = this.createSmallEnemy(x, y)
            break
          case ENEMY_TYPES.MEDIUM:
            enemy = this.createMediumEnemy(x, y)
            break
          case ENEMY_TYPES.FAST:
            enemy = this.createFastEnemy(x, y)
            break
          case ENEMY_TYPES.BOSS:
            enemy = this.createBoss(240, -100)
            break
        }

        if (enemy) {
          this.enemies.add(enemy)
          spawned++
          this.enemiesRemaining++
        }
      },
      repeat: count - 1
    })
  }

  createSmallEnemy(x, y) {
    const enemy = this.physics.add.sprite(x, y, 'small_enemy')
    enemy.setCollideWorldBounds(true)
    enemy.health = 1
    enemy.type = ENEMY_TYPES.SMALL
    enemy.speed = 100
    
    this.time.addEvent({
      delay: 2000,
      callback: this.enemyFire,
      callbackScope: { enemy, scene: this },
      loop: true
    })

    return enemy
  }

  createMediumEnemy(x, y) {
    const enemy = this.physics.add.sprite(x, y, 'medium_enemy')
    enemy.setCollideWorldBounds(true)
    enemy.health = 3
    enemy.type = ENEMY_TYPES.MEDIUM
    enemy.speed = 80
    enemy.baseX = x

    this.time.addEvent({
      delay: 1500,
      callback: this.enemyFire,
      callbackScope: { enemy, scene: this },
      loop: true
    })

    return enemy
  }

  createFastEnemy(x, y) {
    const enemy = this.physics.add.sprite(x, y, 'fast_enemy')
    enemy.setCollideWorldBounds(true)
    enemy.health = 1
    enemy.type = ENEMY_TYPES.FAST
    enemy.speed = 200
    enemy.baseX = x
    enemy.timeOffset = Math.random() * Math.PI * 2

    this.time.addEvent({
      delay: 1000,
      callback: this.enemyFire,
      callbackScope: { enemy, scene: this },
      loop: true
    })

    return enemy
  }

  createBoss(x, y) {
    const boss = this.physics.add.sprite(x, y, 'boss')
    boss.setCollideWorldBounds(true)
    boss.health = 100
    boss.maxHealth = 100
    boss.type = ENEMY_TYPES.BOSS
    boss.speed = 50
    boss.phase = 1
    boss.isBoss = true

    this.time.addEvent({
      delay: 1000,
      callback: this.bossFire,
      callbackScope: { boss, scene: this },
      loop: true
    })

    return boss
  }

  enemyFire() {
    if (!this.enemy.active) return

    const bullet = this.physics.add.sprite(
      this.enemy.x,
      this.enemy.y + 20,
      'enemy_bullet'
    )
    bullet.setVelocityY(300)
    this.enemyBullets.add(bullet)

    this.time.delayedCall(3000, () => {
      if (bullet.active) {
        bullet.destroy()
      }
    })
  }

  bossFire() {
    if (!this.boss.active) return

    const healthPercent = this.boss.health / this.boss.maxHealth

    if (healthPercent > 0.7) {
      this.bossAttackPattern1()
    } else if (healthPercent > 0.3) {
      this.bossAttackPattern2()
    } else {
      this.bossAttackPattern3()
    }
  }

  bossAttackPattern1() {
    for (let i = -2; i <= 2; i++) {
      const bullet = this.physics.add.sprite(
        this.boss.x,
        this.boss.y + 30,
        'enemy_bullet'
      )
      bullet.setVelocity(i * 50, 300)
      this.enemyBullets.add(bullet)
    }
  }

  bossAttackPattern2() {
    this.bossAttackPattern1()
    
    const trackingBullet = this.physics.add.sprite(
      this.boss.x,
      this.boss.y + 30,
      'enemy_bullet'
    )
    
    this.time.addEvent({
      delay: 50,
      callback: () => {
        if (trackingBullet.active && this.player.active) {
          const angle = Phaser.Math.Angle.Between(
            trackingBullet.x,
            trackingBullet.y,
            this.player.x,
            this.player.y
          )
          trackingBullet.setVelocity(
            Math.cos(angle) * 200,
            Math.sin(angle) * 200
          )
        }
      },
      repeat: 10
    })
  }

  bossAttackPattern3() {
    this.bossAttackPattern2()
    
    for (let i = 0; i < 360; i += 30) {
      const bullet = this.physics.add.sprite(
        this.boss.x,
        this.boss.y,
        'enemy_bullet'
      )
      const rad = Phaser.Math.DegToRad(i)
      bullet.setVelocity(Math.cos(rad) * 200, Math.sin(rad) * 200)
      this.enemyBullets.add(bullet)
    }
  }

  showBossWarning() {
    const warning = this.add.text(240, 360, 'BOSS 来袭!', {
      font: 'bold 48px Arial',
      color: '#ff0000',
      stroke: '#ffffff',
      strokeThickness: 6
    }).setOrigin(0.5)

    this.tweens.add({
      targets: warning,
      alpha: 0,
      delay: 2000,
      duration: 500,
      onComplete: () => warning.destroy()
    })
  }

  hitEnemy(bullet, enemy) {
    if (!bullet.active || !enemy.active) return

    bullet.destroy()
    enemy.health--

    if (enemy.health <= 0) {
      this.destroyEnemy(enemy)
    } else {
      this.tweens.add({
        targets: enemy,
        alpha: 0.5,
        duration: 50,
        yoyo: true
      })
    }
  }

  destroyEnemy(enemy) {
    const score = SCORES[enemy.type] * (1 + Math.floor(this.combo / 10) * 0.1)
    this.addScore(Math.floor(score))
    
    this.combo++
    if (this.combo > this.maxCombo) {
      this.maxCombo = this.combo
    }
    this.comboText.setText(`连击：${this.combo}`)

    createExplosion(this, enemy.x, enemy.y)

    if (Math.random() < 0.2) {
      this.spawnItem(enemy.x, enemy.y)
    }

    enemy.destroy()
    this.enemiesRemaining--
    this.enemiesKilled++

    if (enemy.isBoss) {
      this.bossHealthBar.clear()
      this.completeWave()
    } else if (this.enemiesRemaining <= 0) {
      this.time.delayedCall(1000, () => {
        if (this.enemies.countActive(true) === 0) {
          this.completeWave()
        }
      })
    }
  }

  spawnItem(x, y) {
    const types = ['powerup', 'shield', 'bomb', 'extralife']
    const type = types[Math.floor(Math.random() * types.length)]
    
    const item = this.physics.add.sprite(x, y, type)
    item.setVelocityY(100)
    item.type = type
    this.items.add(item)
  }

  collectItem(item, player) {
    if (!item.active) return

    item.destroy()

    switch (item.type) {
      case 'powerup':
        player.upgradeWeapon()
        break
      case 'shield':
        player.activateShield()
        break
      case 'bomb':
        this.activateBomb()
        break
      case 'extralife':
        this.lives++
        this.livesText.setText(`生命：${this.lives}`)
        break
    }
  }

  activateBomb() {
    this.enemies.getChildren().forEach(enemy => {
      if (!enemy.isBoss) {
        this.destroyEnemy(enemy)
      }
    })
    
    this.enemyBullets.clear(true, true)
    
    this.cameras.main.shake(200, 0.02)
  }

  playerHit(player, source) {
    if (!player.active || player.isInvincible) return

    if (player.hasShield) {
      player.removeShield()
      if (source.active) source.destroy()
      return
    }

    if (source.active) source.destroy()
    
    this.lives--
    this.livesText.setText(`生命：${this.lives}`)
    this.combo = 0
    this.comboText.setText('连击：0')

    createExplosion(this, player.x, player.y, 0xff0000, 20)

    if (this.lives <= 0) {
      this.gameOver()
    } else {
      player.respawn()
    }
  }

  addScore(points) {
    this.score += points
    this.scoreText.setText(`得分：${this.score}`)
  }

  completeWave() {
    if (this.wave >= WAVES.length) {
      this.completeGame()
    } else {
      this.time.delayedCall(2000, () => {
        this.startWave()
      })
    }
  }

  update() {
    if (this.isGameOver) return

    this.player.update(this.cursors, this.wasd)
    
    this.bullets.getChildren().forEach(bullet => {
      if (bullet.y < -50 || bullet.y > 770) {
        bullet.destroy()
      }
    })

    this.enemyBullets.getChildren().forEach(bullet => {
      if (bullet.y < -50 || bullet.y > 770) {
        bullet.destroy()
      }
    })

    this.items.getChildren().forEach(item => {
      if (item.y > 770) {
        item.destroy()
      }
    })

    this.enemies.getChildren().forEach(enemy => {
      if (!enemy.active) return

      if (enemy.type === ENEMY_TYPES.MEDIUM || enemy.type === ENEMY_TYPES.FAST) {
        enemy.x = enemy.baseX + Math.sin(this.time.now / 500 + enemy.timeOffset) * 50
      }

      if (enemy.y > 800) {
        enemy.destroy()
      }
    })

    if (this.enemies.getChildren().some(e => e.isBoss && e.active)) {
      const boss = this.enemies.getChildren().find(e => e.isBoss && e.active)
      if (boss) {
        this.updateBossHealthBar(boss)
      }
    }
  }

  updateBossHealthBar(boss) {
    this.bossHealthBar.clear()
    this.bossHealthBar.fillStyle(0xff0000)
    this.bossHealthBar.fillRect(100, 100, 280, 20)
    this.bossHealthBar.fillStyle(0x00ff00)
    this.bossHealthBar.fillRect(100, 100, 280 * (boss.health / boss.maxHealth), 20)
  }

  gameOver() {
    this.isGameOver = true
    this.player.destroy()
    
    const highScore = localStorage.getItem('airplane_highscore') || 0
    if (this.score > highScore) {
      localStorage.setItem('airplane_highscore', this.score)
    }

    this.time.delayedCall(1000, () => {
      this.scene.start('GameOverScene', { score: this.score })
    })
  }

  completeGame() {
    this.isGameOver = true
    
    const highScore = localStorage.getItem('airplane_highscore') || 0
    if (this.score > highScore) {
      localStorage.setItem('airplane_highscore', this.score)
    }

    this.time.delayedCall(1000, () => {
      this.scene.start('LevelCompleteScene', { score: this.score })
    })
  }
}
