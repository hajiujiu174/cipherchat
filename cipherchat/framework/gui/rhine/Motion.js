// Adapted from RhineLabUI/src/motion.ts, MIT, Copyright (c) 2026 LBEILC.
// The original and license are retained in third_party/rhinelab.
.pragma library
function smooth(t) {
    t = Math.max(0, Math.min(1, t));
    return t * t * t * (10 + t * (-15 + 6 * t));
}
function wave(distance, age) {
    if (age < 0 || age > 3.2) return 0;
    var phase = distance - age * 8;
    return 0.8 * smooth(age / 0.2) * Math.exp(-age * 1.15)
        * Math.cos(phase * 0.58) * Math.exp(-0.5 * Math.pow(phase / 3.4, 2));
}
function idle(row, lane, time) {
    return 0.075 * Math.sin(time * Math.PI * 2 / 8 + row * 0.3 - lane * 0.45)
        + 0.027 * Math.sin(time * Math.PI * 2 / 13 - row * 0.17 + lane * 0.3);
}
function damp(value, velocity, target, rate, dt) {
    var delta = value - target;
    var impulse = velocity + rate * delta;
    var decay = Math.exp(-rate * dt);
    return [target + (delta + impulse * dt) * decay, (velocity - rate * impulse * dt) * decay];
}
