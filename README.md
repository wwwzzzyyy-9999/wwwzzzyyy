# 飞机大战 (Airplane Battle)

经典像素风格飞机大战游戏，使用 Phaser.js 框架开发。

## 游戏特性

- 🎮 经典街机射击玩法
- 🚀 6 个波次，难度递增
- 👾 BOSS 战，三阶段攻击模式
- 💪 多种道具：火力增强、护盾、全屏炸弹、额外生命
- 🎯 连击系统
- 🏆 最高分记录

## 操作说明

- **移动**: WASD 或 方向键
- **射击**: 空格键

## 技术栈

- Phaser.js 3.x
- Vite
- JavaScript ES6+

## 快速开始

### 安装依赖

```bash
npm install
```

### 开发模式

```bash
npm run dev
```

游戏将在 http://localhost:3000 运行

### 构建生产版本

```bash
npm run build
```

## 游戏系统

### 得分系统
- 小型敌机：100 分
- 中型敌机：200 分
- 快速敌机：300 分
- BOSS: 5000 分
- 连击奖励：每 10 连击增加 10% 得分

### 波次配置
1. 波次 1: 10 个小型敌机
2. 波次 2: 8 个中型敌机
3. 波次 3: 6 个小型 + 6 个中型敌机
4. 波次 4: 10 个快速敌机
5. 波次 5: 8 个小型 + 4 个中型 + 3 个快速敌机
6. 波次 6: BOSS 战

### BOSS 战
- **阶段 1** (100%-70% 血量): 正面扫射
- **阶段 2** (70%-30% 血量): 追踪导弹 + 扫射
- **阶段 3** (30%-0% 血量): 全屏激光 + 追踪导弹 + 扫射

## 项目结构

```
airplane-battle/
├── index.html
├── package.json
├── vite.config.js
└── src/
    ├── main.js
    ├── scenes/
    │   ├── BootScene.js
    │   ├── MenuScene.js
    │   ├── GameScene.js
    │   ├── LevelCompleteScene.js
    │   └── GameOverScene.js
    ├── entities/
    │   └── Player.js
    └── utils/
        ├── constants.js
        └── helpers.js
```

## 许可证

MIT
