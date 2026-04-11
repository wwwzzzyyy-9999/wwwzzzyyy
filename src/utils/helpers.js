export function randomRange(min, max) {
  return Math.random() * (max - min) + min
}

export function randomInt(min, max) {
  return Math.floor(Math.random() * (max - min + 1)) + min
}

export function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max)
}

export function checkBounds(sprite, bounds) {
  return sprite.x >= bounds.left && 
         sprite.x <= bounds.right && 
         sprite.y >= bounds.top && 
         sprite.y <= bounds.bottom
}

export function createExplosion(scene, x, y, color = 0xff6600, count = 10) {
  const particles = scene.add.particles(x, y, 'particle', {
    speed: { min: 50, max: 150 },
    angle: { min: 0, max: 360 },
    scale: { start: 0.5, end: 0 },
    tint: color,
    lifespan: 300,
    gravityY: 0,
    quantity: count
  })
  
  setTimeout(() => particles.destroy(), 300)
  return particles
}
