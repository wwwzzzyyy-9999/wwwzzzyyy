import Phaser from 'phaser'

export default class BootScene extends Phaser.Scene {
  constructor() {
    super('BootScene')
  }

  preload() {
    const progressBar = this.add.graphics()
    const progressBox = this.add.graphics()
    progressBox.fillStyle(0x222222, 0.8)
    progressBox.fillRect(90, 340, 300, 50)

    const loadingText = this.add.text(240, 315, 'Loading...', {
      font: '28px Arial',
      color: '#ffffff'
    }).setOrigin(0.5)

    this.load.on('progress', (value) => {
      progressBar.clear()
      progressBar.fillStyle(0x00ff00, 1)
      progressBar.fillRect(100, 350, 280 * value, 30)
    })

    this.load.on('complete', () => {
      progressBar.destroy()
      progressBox.destroy()
      loadingText.destroy()
    })

    this.createAssets()
  }

  createAssets() {
    const graphics = this.make.graphics({ x: 0, y: 0, add: false })

    graphics.clear()
    graphics.fillStyle(0x00ff00)
    graphics.fillRect(0, 0, 32, 32)
    graphics.generateTexture('player', 32, 32)

    graphics.clear()
    graphics.fillStyle(0xff0000)
    graphics.fillRect(0, 0, 24, 24)
    graphics.generateTexture('small_enemy', 24, 24)

    graphics.clear()
    graphics.fillStyle(0xff6600)
    graphics.fillRect(0, 0, 32, 32)
    graphics.generateTexture('medium_enemy', 32, 32)

    graphics.clear()
    graphics.fillStyle(0xffff00)
    graphics.fillRect(0, 0, 28, 28)
    graphics.generateTexture('fast_enemy', 28, 28)

    graphics.clear()
    graphics.fillStyle(0x9900ff)
    graphics.fillRect(0, 0, 48, 48)
    graphics.generateTexture('boss', 48, 48)

    graphics.clear()
    graphics.fillStyle(0x00ffff)
    graphics.fillRect(0, 0, 8, 16)
    graphics.generateTexture('player_bullet', 8, 16)

    graphics.clear()
    graphics.fillStyle(0xff00ff)
    graphics.fillRect(0, 0, 8, 16)
    graphics.generateTexture('enemy_bullet', 8, 16)

    graphics.clear()
    graphics.fillStyle(0x00ff00)
    graphics.fillRect(0, 0, 20, 20)
    graphics.generateTexture('powerup', 20, 20)

    graphics.clear()
    graphics.fillStyle(0x0000ff)
    graphics.fillCircle(10, 10, 10)
    graphics.generateTexture('shield', 20, 20)

    graphics.clear()
    graphics.fillStyle(0xff0000)
    graphics.fillRect(0, 0, 20, 20)
    graphics.generateTexture('bomb', 20, 20)

    graphics.clear()
    graphics.fillStyle(0xffff00)
    graphics.fillRect(0, 0, 20, 20)
    graphics.generateTexture('extralife', 20, 20)

    graphics.clear()
    graphics.fillStyle(0xff6600)
    graphics.fillCircle(4, 4, 4)
    graphics.generateTexture('particle', 8, 8)

    this.createBackground()
  }

  createBackground() {
    const graphics = this.make.graphics({ x: 0, y: 0, add: false })
    graphics.fillStyle(0x0a0a2e)
    graphics.fillRect(0, 0, 480, 720)
    
    for (let i = 0; i < 50; i++) {
      const x = Math.random() * 480
      const y = Math.random() * 720
      graphics.fillStyle(Math.random() > 0.5 ? 0xffffff : 0x6666ff)
      graphics.fillCircle(x, y, Math.random() * 2 + 1)
    }
    graphics.generateTexture('background', 480, 720)
  }

  create() {
    this.scene.start('MenuScene')
  }
}
