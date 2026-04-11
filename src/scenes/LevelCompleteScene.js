import Phaser from 'phaser'

export default class LevelCompleteScene extends Phaser.Scene {
  constructor() {
    super('LevelCompleteScene')
  }

  init(data) {
    this.score = data.score || 0
  }

  create() {
    this.add.image(240, 360, 'background')

    const title = this.add.text(240, 200, '恭喜通关!', {
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

    const scoreText = this.add.text(240, 300, `最终得分：${this.score}`, {
      font: 'bold 36px Arial',
      color: '#ffffff'
    }).setOrigin(0.5)

    const highScore = localStorage.getItem('airplane_highscore') || 0
    this.add.text(240, 360, `最高分：${highScore}`, {
      font: '28px Arial',
      color: '#ffff00'
    }).setOrigin(0.5)

    const restartButton = this.add.text(240, 450, '再玩一次', {
      font: 'bold 32px Arial',
      color: '#ffffff',
      backgroundColor: '#00aa00',
      padding: { x: 40, y: 15 }
    }).setOrigin(0.5).setInteractive({ useHandCursor: true })

    restartButton.on('pointerover', () => {
      restartButton.setStyle({ backgroundColor: '#00ff00' })
    })

    restartButton.on('pointerout', () => {
      restartButton.setStyle({ backgroundColor: '#00aa00' })
    })

    restartButton.on('pointerdown', () => {
      this.scene.start('GameScene')
    })

    const menuButton = this.add.text(240, 530, '返回菜单', {
      font: 'bold 32px Arial',
      color: '#ffffff',
      backgroundColor: '#0066aa',
      padding: { x: 40, y: 15 }
    }).setOrigin(0.5).setInteractive({ useHandCursor: true })

    menuButton.on('pointerover', () => {
      menuButton.setStyle({ backgroundColor: '#0099ff' })
    })

    menuButton.on('pointerout', () => {
      menuButton.setStyle({ backgroundColor: '#0066aa' })
    })

    menuButton.on('pointerdown', () => {
      this.scene.start('MenuScene')
    })
  }
}
