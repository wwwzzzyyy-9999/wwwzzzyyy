import Phaser from 'phaser'

export default class GameOverScene extends Phaser.Scene {
  constructor() {
    super('GameOverScene')
  }

  init(data) {
    this.score = data.score || 0
  }

  create() {
    this.add.image(240, 360, 'background')

    const title = this.add.text(240, 200, '游戏结束', {
      font: 'bold 48px Arial',
      color: '#ff0000',
      stroke: '#ffffff',
      strokeThickness: 6
    }).setOrigin(0.5)

    const scoreText = this.add.text(240, 300, `最终得分：${this.score}`, {
      font: 'bold 36px Arial',
      color: '#ffffff'
    }).setOrigin(0.5)

    const highScore = localStorage.getItem('airplane_highscore') || 0
    const isNewRecord = this.score >= highScore && this.score > 0
    
    if (isNewRecord) {
      const newRecordText = this.add.text(240, 350, '新纪录!', {
        font: 'bold 28px Arial',
        color: '#ffff00'
      }).setOrigin(0.5)

      this.tweens.add({
        targets: newRecordText,
        alpha: 0,
        duration: 500,
        yoyo: true,
        repeat: -1
      })
    }

    this.add.text(240, 400, `最高分：${highScore}`, {
      font: '28px Arial',
      color: '#ffff00'
    }).setOrigin(0.5)

    const restartButton = this.add.text(240, 480, '重新开始', {
      font: 'bold 32px Arial',
      color: '#ffffff',
      backgroundColor: '#ff6600',
      padding: { x: 40, y: 15 }
    }).setOrigin(0.5).setInteractive({ useHandCursor: true })

    restartButton.on('pointerover', () => {
      restartButton.setStyle({ backgroundColor: '#ff9900' })
    })

    restartButton.on('pointerout', () => {
      restartButton.setStyle({ backgroundColor: '#ff6600' })
    })

    restartButton.on('pointerdown', () => {
      this.scene.start('GameScene')
    })

    const menuButton = this.add.text(240, 560, '返回菜单', {
      font: 'bold 32px Arial',
      color: '#ffffff',
      backgroundColor: '#666666',
      padding: { x: 40, y: 15 }
    }).setOrigin(0.5).setInteractive({ useHandCursor: true })

    menuButton.on('pointerover', () => {
      menuButton.setStyle({ backgroundColor: '#999999' })
    })

    menuButton.on('pointerout', () => {
      menuButton.setStyle({ backgroundColor: '#666666' })
    })

    menuButton.on('pointerdown', () => {
      this.scene.start('MenuScene')
    })
  }
}
