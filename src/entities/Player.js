import Phaser from 'phaser'

export default class Player extends Phaser.Physics.Arcade.Sprite {
  constructor(scene, x, y) {
    super(scene, x, y, 'player')
    scene.physics.world.enable(this)
    scene.add.existing(this)
    
    this.setCollideWorldBounds(true)
    this.health = 3
    this.isInvincible = false
    this.weaponLevel = 1
    this.hasShield = false
    this.canShoot = true
    this.shootDelay = 200
  }

  update(cursors, wasd) {
    this.setVelocity(0)

    if (cursors.left.isDown || wasd.left.isDown) {
      this.setVelocityX(-300)
    } else if (cursors.right.isDown || wasd.right.isDown) {
      this.setVelocityX(300)
    }

    if (cursors.up.isDown || wasd.up.isDown) {
      this.setVelocityY(-300)
    } else if (cursors.down.isDown || wasd.down.isDown) {
      this.setVelocityY(300)
    }

    this.x = Phaser.Math.Clamp(this.x, 16, 464)
    this.y = Phaser.Math.Clamp(this.y, 16, 704)
  }

  fire() {
    if (!this.canShoot || !this.active) return

    this.canShoot = false

    if (this.weaponLevel === 1) {
      this.fireSingleBullet()
    } else if (this.weaponLevel === 2) {
      this.fireDoubleBullet()
    } else {
      this.fireTripleBullet()
    }

    this.scene.time.delayedCall(this.shootDelay, () => {
      this.canShoot = true
    })
  }

  fireSingleBullet() {
    const bullet = this.scene.physics.add.sprite(this.x, this.y - 20, 'player_bullet')
    bullet.setVelocityY(-500)
    this.scene.bullets.add(bullet)
  }

  fireDoubleBullet() {
    const leftBullet = this.scene.physics.add.sprite(this.x - 10, this.y - 15, 'player_bullet')
    leftBullet.setVelocityY(-500)
    this.scene.bullets.add(leftBullet)

    const rightBullet = this.scene.physics.add.sprite(this.x + 10, this.y - 15, 'player_bullet')
    rightBullet.setVelocityY(-500)
    this.scene.bullets.add(rightBullet)
  }

  fireTripleBullet() {
    this.fireDoubleBullet()
    
    const centerBullet = this.scene.physics.add.sprite(this.x, this.y - 20, 'player_bullet')
    centerBullet.setVelocityY(-500)
    this.scene.bullets.add(centerBullet)
  }

  upgradeWeapon() {
    this.weaponLevel = Math.min(this.weaponLevel + 1, 3)
  }

  activateShield() {
    if (this.hasShield) return
    
    this.hasShield = true
    this.shield = this.scene.add.circle(this.x, this.y, 25, 0x0000ff, 0.5)
    
    this.scene.time.delayedCall(10000, () => {
      this.removeShield()
    })
  }

  removeShield() {
    this.hasShield = false
    if (this.shield) {
      this.shield.destroy()
      this.shield = null
    }
  }

  respawn() {
    this.isInvincible = true
    this.setAlpha(0.5)
    
    const flashTween = this.scene.tweens.add({
      targets: this,
      alpha: 0,
      duration: 100,
      yoyo: true,
      repeat: 9,
      onComplete: () => {
        this.setAlpha(1)
        this.isInvincible = false
      }
    })

    this.scene.time.delayedCall(2000, () => {
      this.isInvincible = false
      this.setAlpha(1)
      if (flashTween.isPlaying()) {
        flashTween.stop()
      }
    })
  }

  destroy() {
    this.removeShield()
    super.destroy()
  }
}
