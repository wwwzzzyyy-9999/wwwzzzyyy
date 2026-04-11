import Phaser from 'phaser'

export default class MenuScene extends Phaser.Scene {
  constructor() {
    super('MenuScene')
  }

  create() {
    this.add.image(240, 360, 'background')

    const title = this.add.text(240, 150, '飞机大战', {
      font: 'bold 48px Arial',
      color: '#00ff00',
      stroke: '#ffffff',
      strokeThickness: 6
    }).setOrigin(0.5)

    this.tweens.add({
      targets: title,
      scaleX: 1.1,
      scaleY: 1.1,
      yoyo: true,
      repeat: -1,
      duration: 1000
    })

    const startButton = this.add.text(240, 300, '开始游戏', {
      font: 'bold 32px Arial',
      color: '#ffffff',
      backgroundColor: '#00aa00',
      padding: { x: 40, y: 15 }
    }).setOrigin(0.5).setInteractive({ useHandCursor: true })

    startButton.on('pointerover', () => {
      startButton.setStyle({ backgroundColor: '#00ff00' })
    })

    startButton.on('pointerout', () => {
      startButton.setStyle({ backgroundColor: '#00aa00' })
    })

    startButton.on('pointerdown', () => {
      this.scene.start('GameScene')
    })

    const instructions = this.add.text(240, 450, '操作说明:\nWASD 或 方向键移动\n空格键射击', {
      font: '20px Arial',
      color: '#ffffff',
      align: 'center'
    }).setOrigin(0.5)

    const highScore = localStorage.getItem('airplane_highscore') || 0
    this.add.text(240, 550, `最高分：${highScore}`, {
      font: '24px Arial',
      color: '#ffff00'
    }).setOrigin(0.5)
  }
}
