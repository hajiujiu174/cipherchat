// src/boot-tracks.ts
function track(keys, frame) {
  if (frame <= keys[0][0]) return keys[0][1];
  const last = keys.length - 1;
  if (frame >= keys[last][0]) return keys[last][1];
  const slope = (i2) => (keys[i2 + 1][1] - keys[i2][1]) / (keys[i2 + 1][0] - keys[i2][0]);
  const tangent = (i2) => {
    if (i2 === 0) return slope(0);
    if (i2 === last) return slope(last - 1);
    const a = slope(i2 - 1), b = slope(i2);
    if (a * b <= 0) return 0;
    const h0 = keys[i2][0] - keys[i2 - 1][0], h1 = keys[i2 + 1][0] - keys[i2][0];
    const w0 = 2 * h1 + h0, w1 = h1 + 2 * h0;
    return (w0 + w1) / (w0 / a + w1 / b);
  };
  let i = 0;
  while (frame > keys[i + 1][0]) i++;
  const h = keys[i + 1][0] - keys[i][0], p = (frame - keys[i][0]) / h;
  return (2 * p ** 3 - 3 * p ** 2 + 1) * keys[i][1] + (p ** 3 - 2 * p ** 2 + p) * h * tangent(i) + (-2 * p ** 3 + 3 * p ** 2) * keys[i + 1][1] + (p ** 3 - p ** 2) * h * tangent(i + 1);
}
var brandX = [
  234,
  216,
  181,
  143,
  115,
  94,
  78,
  65,
  54,
  45,
  38,
  31,
  26,
  21,
  17,
  13,
  10,
  8,
  6,
  4,
  3,
  2,
  1,
  0
];
var brandKeys = brandX.map((x, i) => [i, x]);
var brandTrack = (frame, line) => {
  const local = frame - 278 - line * 2;
  return {
    x: track(brandKeys, local),
    opacity: track(
      [
        [-1, 0],
        [0, 0.4],
        [1, 0.65],
        [2, 0.88],
        [3, 1]
      ],
      local
    )
  };
};
var highlightKeys = [
  [588, 4],
  [589, 39],
  [592, 225],
  [593, 261],
  [594, 288],
  [595, 310],
  [596, 328],
  [597, 343],
  [598, 356],
  [599, 366],
  [600, 375],
  [601, 382],
  [602, 389],
  [603, 394],
  [604, 398],
  [605, 402],
  [606, 405],
  [607, 407],
  [608, 408],
  [609, 409],
  [610, 410],
  [611, 410]
];
var companyTrack = (frame) => track(highlightKeys, frame) / 410;
function scanTrack(frame) {
  const value = (keys) => track(keys, frame);
  const rad = Math.PI / 180;
  return {
    radius: value([
      [487, 2e3],
      [492, 1540],
      [497, 1095],
      [500, 895.5],
      [505, 650],
      [510, 492],
      [515, 385.5],
      [520, 320.5],
      [524, 287.5],
      [527, 275],
      [530, 270],
      [535, 265.5],
      [540, 262],
      [545, 258],
      [550, 255],
      [555, 252.5],
      [560, 250],
      [568, 246]
    ]),
    whiteRadius: value([
      [487, 1500],
      [492, 1011],
      [493, 955],
      [494, 893],
      [495, 834],
      [496, 783],
      [497, 737],
      [498, 693.5],
      [499, 655],
      [500, 619],
      [505, 470.5],
      [510, 374],
      [515, 310.5],
      [520, 272],
      [524, 253.5],
      [527, 248],
      [530, 246],
      [535, 242],
      [540, 238],
      [545, 235],
      [550, 232.5],
      [555, 230],
      [560, 227.5],
      [568, 224]
    ]),
    outerStart: rad * value([
      [487, 470],
      [492, 364],
      [497, 276],
      [500, 226],
      [505, 159.75],
      [510, 98.75],
      [515, 49.25],
      [520, 13.25],
      [524, -9],
      [527, -23],
      [530, -36.25],
      [535, -53],
      [540, -66.25],
      [545, -75.75],
      [550, -82.75],
      [560, -89],
      [568, -90]
    ]),
    outerSweep: rad * value([
      [487, 30],
      [492, 105],
      [497, 170],
      [500, 199],
      [505, 241.5],
      [510, 270.25],
      [515, 295.75],
      [520, 313.25],
      [527, 331],
      [530, 336.75],
      [535, 344.5],
      [540, 350.75],
      [545, 355],
      [550, 358],
      [560, 360],
      [568, 360]
    ]),
    whiteStart: rad * value([
      [487, -100],
      [492, -30],
      [497, 72],
      [500, 107],
      [505, 140],
      [510, 172],
      [515, 196],
      [520, 213.75],
      [527, 234.5],
      [530, 241],
      [535, 249.75],
      [540, 256.75],
      [545, 262.25],
      [550, 265.75],
      [560, 269.5],
      [568, 270]
    ]),
    whiteSweep: rad * value([
      [487, 30],
      [492, 105],
      [497, 170],
      [500, 200],
      [505, 241.25],
      [510, 272.75],
      [515, 295.5],
      [520, 313.75],
      [527, 330.5],
      [530, 335.5],
      [535, 343.5],
      [540, 349.5],
      [545, 354],
      [550, 357],
      [560, 360],
      [568, 360]
    ]),
    innerRadius: value([
      [487, 124.5],
      [492, 123],
      [495, 121],
      [500, 117.5],
      [505, 112],
      [510, 107.5],
      [515, 104],
      [520, 101.5],
      [530, 97],
      [540, 94],
      [550, 91.5],
      [560, 90],
      [568, 88.5]
    ]),
    innerStart: rad * value([
      [487, -20],
      [490, -4.25],
      [492, 14],
      [495, 48.75],
      [497, 69.25],
      [500, 93.25],
      [505, 121.25],
      [510, 141],
      [515, 155.5],
      [520, 166.5],
      [527, 178],
      [530, 181],
      [535, 187.75],
      [540, 192],
      [545, 195.25],
      [550, 197.75],
      [555, 199.75],
      [560, 200.75],
      [568, 201.5]
    ]),
    innerSweep: rad * value([
      [487, 0],
      [492, 22.5],
      [497, 55.75],
      [500, 70.25],
      [505, 87.25],
      [510, 99],
      [515, 108],
      [520, 115],
      [527, 123.5],
      [530, 127],
      [535, 128],
      [540, 131],
      [550, 134],
      [568, 136]
    ]),
    orbit: rad * value([
      [487, 70],
      [492, 33],
      [493, 13.2],
      [494, -5.8],
      [495, -24.2],
      [496, -41.4],
      [497, -57.2],
      [498, -71.4],
      [499, -84.2],
      [500, -96],
      [505, -140.8],
      [510, -172.3],
      [515, -195.5],
      [520, -213.5],
      [527, -234],
      [530, -238.8],
      [535, -247.7],
      [540, -254.7],
      [545, -260.1],
      [550, -264],
      [555, -266.9],
      [560, -268.8],
      [568, -270]
    ]),
    orbitRadius: value([
      [487, 270],
      [495, 236],
      [500, 224],
      [505, 214],
      [510, 206],
      [515, 199],
      [520, 193],
      [530, 185],
      [540, 179.5],
      [550, 174.5],
      [560, 171.5],
      [568, 169]
    ]),
    dotRadius: value([
      [487, 0],
      [492, 1],
      [500, 5.6],
      [510, 7],
      [520, 7.8],
      [530, 8],
      [568, 8]
    ]),
    blackCap: value([
      [487, 45],
      [504, 35],
      [510, 20],
      [515, 10],
      [520, 5],
      [525, 2.5],
      [535, 2],
      [550, 0],
      [568, 0]
    ]),
    whiteCap: value([
      [487, 40],
      [492, 30],
      [497, 22],
      [502, 16],
      [507, 10],
      [512, 5],
      [520, 1.5],
      [540, 0],
      [568, 0]
    ])
  };
}

// src/boot-orbit-tracks.ts
var sides = [
  [543, 827.5, 561, 1091, 518, 38.7, 260, 250, 0, 0],
  [544, 827.5, 561, 1091, 518, 38.7, 260, 250, 3, 3],
  [545, 827.5, 560.5, 1091, 518.5, 38.6, 265, 236, 17, 17],
  [546, 827.5, 559.5, 1091, 519.5, 38.5, 274, 190, 44, 44],
  [547, 827.6, 558.2, 1090.6, 520.8, 38.5, 296, 121, 79, 81],
  [548, 828.06, 556.76, 1090.35, 522.58, 38.38, 332.38, 50.99, 111.73, 113.42],
  [549, 827.84, 555.13, 1090.42, 524.39, 38.34, 367.27, -14.03, 141.56, 145.66],
  [550, 827.89, 553.28, 1090.2, 525.89, 38.44, 396.14, -68.97, 168.88, 172.89],
  [551, 828, 551.68, 1090.21, 527.44, 38.28, 421.62, -113.84, 191.68, 195.3],
  [552, 828.19, 550.24, 1089.94, 528.85, 38.25, 444.24, -152.25, 208.55, 215.7],
  [553, 828.42, 548.83, 1089.7, 530.24, 38.18, 462.76, -184.19, 225.92, 230.73],
  [554, 828.61, 547.64, 1089.53, 531.44, 38.12, 479.15, -212.29, 239.9, 244.63],
  [555, 828.8, 546.47, 1089.37, 532.55, 38.03, 494.23, -237.06, 251.56, 257.75],
  [
    556,
    829.06,
    545.41,
    1089.16,
    533.53,
    37.91,
    508.57,
    -259.19,
    261.49,
    267.63
  ],
  [557, 829.25, 544.49, 1088.93, 534.52, 37.87, 520.46, -279.2, 271.23, 278.42],
  [558, 829.47, 543.66, 1088.7, 535.37, 37.79, 530.39, -295.86, 280.34, 286.17],
  [559, 829.75, 542.88, 1088.46, 536.12, 37.72, 541.33, -312.5, 287.17, 295.12],
  [
    560,
    829.95,
    542.16,
    1088.26,
    536.88,
    37.65,
    549.47,
    -326.37,
    295.98,
    301.16
  ],
  [
    561,
    830.16,
    541.63,
    1088.02,
    537.41,
    37.57,
    557.82,
    -338.78,
    302.29,
    307.52
  ],
  [562, 830.33, 541.08, 1087.81, 537.94, 37.52, 565.1, -350.74, 307.99, 312.58],
  [
    563,
    830.59,
    540.65,
    1087.61,
    538.36,
    37.43,
    571.89,
    -362.08,
    313.78,
    319.28
  ],
  [
    564,
    830.83,
    540.28,
    1087.38,
    538.74,
    37.37,
    578.93,
    -371.93,
    316.97,
    323.53
  ],
  [
    565,
    831.04,
    539.97,
    1087.14,
    539.04,
    37.35,
    583.85,
    -380.51,
    322.27,
    327.78
  ],
  [
    566,
    831.25,
    539.74,
    1086.93,
    539.28,
    37.27,
    589.84,
    -388.93,
    325.61,
    331.62
  ],
  [567, 831.48, 539.56, 1086.73, 539.4, 37.19, 594, -396.5, 330.93, 335.92],
  [568, 831.61, 539.51, 1086.55, 539.48, 37.15, 599.44, -403.94, 332.25, 338.7]
];
var columns = Array.from(
  { length: 9 },
  (_, i) => sides.map((row) => [row[0], row[i + 1]])
);
var orbit = [
  [548, 926.33, 554.33],
  [551, 944.38, 572.15],
  [553, 954.92, 575.05],
  [555, 963, 575],
  [558, 971.63, 572.85],
  [561, 977.53, 569.78],
  [564, 981.5, 566.86],
  [568, 984.71, 563.5]
];
var orbitAngle = orbit.map(
  ([f, x, y]) => [f, Math.atan2(y - 539.5, x - 959.5)]
);
var orbitRadius = orbit.map(
  ([f, x, y]) => [f, Math.hypot(x - 959.5, y - 539.5)]
);
var dotGrowth = [
  [-1, 0],
  [0, 0.8],
  [1, 1.7],
  [2, 2.35],
  [3, 2.85],
  [4, 3.15],
  [5, 3.45],
  [7, 3.8],
  [10, 4.1],
  [15, 4.25],
  [20, 4.25]
];
function scanOrbitTrack(frame) {
  const values = columns.map((keys) => track(keys, frame));
  const [lx, ly, rx, ry, radius, leftStart, rightStart, leftSweep, rightSweep] = values;
  const angle = track(orbitAngle, frame), distance = track(orbitRadius, frame);
  const sourceFrame = Math.floor(frame + 1e-5);
  const flash = [546, 547, 549, 550].includes(sourceFrame);
  const coreRadius = flash ? track(
    [
      [546, 42.93],
      [550, 42.43]
    ],
    frame
  ) : track(
    [
      [545, 0],
      [548, 7.96],
      [551, 9.89],
      [553, 10.63],
      [555, 11.08],
      [559, 11.45],
      [564, 11.45],
      [568, 11.27]
    ],
    frame
  );
  return {
    sides: [
      {
        x: lx,
        y: ly,
        radius,
        start: leftStart * Math.PI / 180,
        sweep: leftSweep * Math.PI / 180
      },
      {
        x: rx,
        y: ry,
        radius,
        start: rightStart * Math.PI / 180,
        sweep: rightSweep * Math.PI / 180
      }
    ],
    sideVisible: frame >= 544,
    coreRadius,
    satellites: Array.from({ length: 6 }, (_, i) => {
      const phase = angle + i * Math.PI / 3;
      return {
        x: 959.5 + Math.cos(phase) * distance,
        y: 539.5 + Math.sin(phase) * distance,
        radius: track(dotGrowth, frame - 548 - i * 3)
      };
    })
  };
}

// src/boot-logo-tracks.ts
var draw = [
  [229, -0.02, -0.0198],
  [230, -0.0195, -0.011],
  [231, -0.016, 9e-3],
  [232, -95e-4, 0.047],
  [233, 25e-4, 0.1135],
  [234, 0.0215, 0.2205],
  [235, 0.048, 0.365],
  [236, 0.0735, 0.504],
  [237, 0.094, 0.6155],
  [238, 0.109, 0.7045],
  [239, 0.122, 0.7755],
  [240, 0.133, 0.835],
  [241, 0.1425, 0.884],
  [242, 0.1505, 0.927],
  [243, 0.1575, 0.964],
  [244, 0.164, 0.996],
  [245, 0.1695, 1.023],
  [246, 0.1745, 1.0475],
  [247, 0.1785, 1.0685],
  [248, 0.183, 1.087],
  [249, 0.187, 1.103],
  [250, 0.19, 1.1175],
  [251, 0.1935, 1.13],
  [252, 0.196, 1.1405],
  [253, 0.1985, 1.15],
  [254, 0.201, 1.1575],
  [255, 0.203, 1.1645],
  [256, 0.205, 1.17],
  [257, 0.2065, 1.1745],
  [258, 0.208, 1.1785],
  [259, 0.2095, 1.181],
  [260, 0.2105, 1.183],
  [261, 0.2115, 1.184],
  [264, 0.211, 1.1835]
];
var drawStart = draw.map(([f, start]) => [f, start]);
var drawEnd = draw.map(([f, , end]) => [f, end]);
var movingCut = [
  [420, 0.1835],
  [425, 0.184],
  [430, 0.1885],
  [435, 0.2],
  [440, 0.2205],
  [450, 0.2925],
  [455, 0.3485],
  [460, 0.421],
  [465, 0.5095],
  [470, 0.614],
  [475, 0.722],
  [480, 0.827],
  [485, 0.9195],
  [486, 0.9365]
];
var leftEdge = [
  [260, 814],
  [261, 813],
  [262, 812],
  [263, 808],
  [264, 803],
  [265, 794],
  [266, 781],
  [267, 763],
  [268, 740],
  [269, 716],
  [270, 691],
  [271, 668],
  [272, 648],
  [273, 630],
  [274, 615],
  [275, 601],
  [276, 590],
  [277, 580],
  [278, 571],
  [279, 563],
  [280, 556],
  [281, 550],
  [282, 545],
  [283, 540],
  [284, 536],
  [285, 533],
  [286, 530],
  [287, 527],
  [288, 525],
  [289, 523],
  [290, 521],
  [291, 520],
  [292, 519],
  [294, 518]
];
function bootLogoTrack(frame) {
  const cut = track(movingCut, frame);
  const start = frame < 420 ? track(drawStart, frame) : cut + 0.0275;
  const end = frame < 420 ? track(drawEnd, frame) : cut + 1;
  return {
    offsetX: track(leftEdge, frame) - 520,
    start,
    length: Math.max(0, Math.min(1, end - start)),
    strokeWidth: track(
      [
        [229, 0.25],
        [230, 2.8],
        [231, 8],
        [232, 16],
        [233, 23.5],
        [234, 26]
      ],
      frame
    ),
    symbolScale: track(
      [
        [246, 0],
        [247, 0.4],
        [248, 0.63],
        [249, 0.75],
        [250, 0.83],
        [251, 0.89],
        [252, 0.94],
        [254, 0.985],
        [256, 1]
      ],
      frame
    ),
    plusX: track(
      [
        [246, 155],
        [247, 123.3],
        [248, 104],
        [249, 93.53],
        [250, 86.52],
        [251, 81.78],
        [252, 78.48],
        [254, 74.26],
        [256, 72.96],
        [260, 72.6]
      ],
      frame
    ),
    minusX: track(
      [
        [246, 155],
        [247, 186.82],
        [248, 206.14],
        [249, 216.66],
        [250, 223.47],
        [251, 228.21],
        [252, 231.77],
        [254, 236.06],
        [256, 237.57],
        [260, 237.1]
      ],
      frame
    ),
    minusWidth: track(
      [
        [246, 15],
        [256, 15],
        [257, 23],
        [258, 31],
        [259, 35.5],
        [260, 38],
        [262, 42],
        [264, 44],
        [268, 46]
      ],
      frame
    ),
    plusAngle: track(
      [
        [255, 0],
        [256, -3],
        [257, -22],
        [258, -45],
        [259, -62],
        [260, -70],
        [262, -80],
        [265, -87],
        [270, -90]
      ],
      frame
    )
  };
}

// src/boot-motion.ts
var progress = (t, a, b) => Math.max(0, Math.min(1, (t - a) / (b - a)));
var smooth = (p) => p * p * (3 - 2 * p);
var typed = (text, f, start, end) => text.slice(
  0,
  f < start ? 0 : Math.min(
    text.length,
    1 + Math.floor((f - start) * (text.length - 1) / (end - start))
  )
);
var at = (f, frames) => frames.includes(f);
var accessCounts = [
  1,
  1,
  3,
  4,
  5,
  6,
  9,
  11,
  12,
  14,
  17,
  18,
  19,
  20,
  22,
  23,
  25,
  26
];
function bootMotion(appTime) {
  const t = appTime + 5;
  const f = Math.floor(t * 25 + 1e-5);
  const step = t < 9.12 ? "access" : t < 11.12 ? "logo" : t < 19.48 ? "auth" : t < 22.76 ? "scan" : "welcome";
  let auth = "";
  if (f < 363) {
    auth = typed("CORE READY", f, 282, 295);
    if (f >= 320) auth += " : " + typed("CIPHERCHAT", f, 321, 339);
  } else if (f < 421) auth = typed("REQUEST RECEIVED", f, 367, 389);
  else {
    auth = typed("START PROCESSING", f, 423, 440);
    if (f >= 449)
      auth += ".".repeat(Math.min(3, 1 + Math.floor((f - 449) / 4)));
    if (at(f, [479, 485, 486])) auth = "              SING...";
  }
  const frame = t * 25;
  const scan = scanTrack(frame);
  const scanOrbit = scanOrbitTrack(frame);
  const scanGlitch = at(f, [525, 526, 528, 529]);
  const welcomeIntro = [1, 0, 0.28, 0, 1, 0, 0];
  const flashIndex = f - 569;
  const exit = smooth(progress(t, 26.56, 26.92));
  return {
    t,
    f,
    step,
    auth,
    access: "ACCESS PERMISSION REQUIRED".slice(
      0,
      f < 170 ? 0 : accessCounts[Math.min(17, f - 170)]
    ),
    accessOpacity: f >= 170 && f < 227 ? f === 226 ? 0.25 : 1 : 0,
    logoOpacity: t >= 9.16 && t < 19.48 ? 1 : 0,
    logo: bootLogoTrack(frame),
    logoLetters: typed("RHINE\xB7LAB", f, 232, 255),
    authOpacity: f >= 281 && f < 487 ? 1 : 0,
    brand: [0, 1, 2].map((line) => brandTrack(frame, line)),
    poweredLetters: typed("POWERED BY RHINE LAB", f, 279, 295).length,
    scanVisible: t >= 19.48 && t < 22.76,
    scan,
    scanOrbit,
    scanRadius: scan.radius,
    ringScale: scanGlitch ? 1.94 : 1,
    ringOpacity: scanGlitch ? 0.32 : track(
      [
        [487, 0],
        [488, 0.18],
        [490, 0.6],
        [493, 1]
      ],
      frame
    ),
    ringBlur: scanGlitch ? 2.2 : 0,
    scanTracking: track(
      [
        [487, 40],
        [492, 28],
        [497, 18],
        [500, 14],
        [505, 8],
        [510, 4],
        [515, 1.7],
        [520, 0.5],
        [527, 0],
        [568, 0]
      ],
      frame
    ),
    scanFont: 26.5,
    permissionOpacity: t < 21.8 ? progress(t, 19.48, 19.88) : track(
      [
        [545, 1],
        [546, 0.4],
        [547, 0.3],
        [548, 0.25],
        [549, 0.1],
        [550, 0.04],
        [551, 0]
      ],
      frame
    ),
    ornament: t >= 21.84,
    coreRadius: scanOrbit.coreRadius,
    welcomeVisible: t >= 22.76 && t < 26.92,
    welcomePanel: flashIndex >= 0 && flashIndex < 7 ? welcomeIntro[flashIndex] : 0,
    welcomeInk: flashIndex >= 0 && flashIndex < 7 ? [0, 0, 0.2, 1, 0, 0, 0.25][flashIndex] : 1,
    companyVisible: f >= 588 && !at(f, [590, 591]),
    companyMask: at(f, [594, 595]),
    highlight: companyTrack(frame),
    databaseOpacity: f < 626 || at(f, [628, 629, 631, 634]) ? 0 : 1,
    welcomeLogo: f >= 588,
    welcomeScale: 1 - 0.46 * exit,
    welcomeOpacity: 1 - Math.pow(exit, 3),
    exitBlur: 8 * exit,
    exit,
    backgroundOpacity: t < 26.92 ? 1 : 0,
    white: smooth(progress(t, 26.16, 26.88))
  };
}

// src/decryption.ts
var DECRYPTION_START = 34.12;
var DECRYPTION_END = 39.56;
var clamp = (value) => Math.max(0, Math.min(1, value));
var smooth2 = (value) => {
  const x = clamp(value);
  return x * x * x * (10 + x * (-15 + x * 6));
};
function sampleCurve(knots, time) {
  if (time <= knots[0][0]) return knots[0][1];
  if (time >= knots[knots.length - 1][0]) return knots[knots.length - 1][1];
  const secant = (i2) => (knots[i2 + 1][1] - knots[i2][1]) / (knots[i2 + 1][0] - knots[i2][0]);
  const slope = (i2) => {
    if (i2 === 0 || i2 === knots.length - 1) return 0;
    const a = secant(i2 - 1), b = secant(i2);
    if (a * b <= 0) return 0;
    const left = knots[i2][0] - knots[i2 - 1][0], right = knots[i2 + 1][0] - knots[i2][0];
    const w1 = 2 * right + left, w2 = right + 2 * left;
    return (w1 + w2) / (w1 / a + w2 / b);
  };
  let i = 0;
  while (time > knots[i + 1][0]) i++;
  const span = knots[i + 1][0] - knots[i][0], t = (time - knots[i][0]) / span, t2 = t * t, t3 = t2 * t;
  return (2 * t3 - 3 * t2 + 1) * knots[i][1] + (t3 - 2 * t2 + t) * span * slope(i) + (-2 * t3 + 3 * t2) * knots[i + 1][1] + (t3 - t2) * span * slope(i + 1);
}
var GROW = [
  [34.24, 0],
  [34.4, 0.19],
  [34.64, 0.57],
  [34.96, 0.79],
  [35.28, 0.92],
  [35.6, 0.973],
  [36.04, 1]
];
var REVEAL = [
  [38.84, 0],
  [38.92, 0.28],
  [39, 0.51],
  [39.16, 0.74],
  [39.32, 0.94],
  [39.56, 1]
];
var RETRACT = [
  [37.72, 1],
  [37.88, 0.72],
  [38, 0.38],
  [38.12, 0.22],
  [38.24, 0.14],
  [38.4, 0.075],
  [38.64, 0.024],
  [38.84, 0]
];
var SCAN_FROM = [-1.6, 0.5];
var SCAN_TO = [1.98, 3.24];
var SCAN_CORNERS = [
  [-1.88, 3.2],
  [2.14, 3.36],
  [-1.77, 0.36],
  [2.17, 0.65]
];
function decryptionFrame(time) {
  const grow = sampleCurve(GROW, time);
  const remaining = sampleCurve(RETRACT, time);
  const intervals = [];
  if (time >= 34.24 && time < 36.04 && grow > 0)
    intervals.push([0, grow * 0.5], [1 - grow * 0.5, 1]);
  else if (time >= 36.04 && time < 38.84)
    intervals.push([0.5 - remaining * 0.5, 0.5 + remaining * 0.5]);
  const markers = smooth2((time - 34.2) / 0.12) * (1 - smooth2((time - 37.76) / 0.56));
  return {
    time,
    intervals,
    markers,
    point: smooth2((time - 38.58) / 0.2) * (1 - smooth2((time - 39.08) / 0.22)),
    label: smooth2((time - 34.32) / 0.36) * (1 - smooth2((time - 37.68) / 0.24)),
    labelValue: smooth2((time - 35.64) / 0.56),
    clarity: sampleCurve(REVEAL, time),
    phase: time < 34.24 ? "waiting" : time < 36.04 ? "joining" : time < 37.72 ? "connected" : time < 38.84 ? "retracting" : time < DECRYPTION_END ? "revealing" : "clear"
  };
}

// src/brand.ts
var paths = `<path d="M156 75C127 48 103 15 70 15C37 15 15 39 15 70S38 128 70 128C103 128 127 96 176 52M155 75C182 99 208 128 240 128C273 128 295 105 295 73S273 15 240 15C221 15 207 23 192 38" fill="none" stroke="currentColor" stroke-width="26"/><path d="M44 70h50M69 45v50M219 70h44" fill="none" stroke="currentColor" stroke-width="15"/>`;
var labelMarkSvg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 310 145" color="#171713">${paths}</svg>`;
var logo = `<svg viewBox="0 0 310 185" aria-label="Rhine Lab" role="img">${paths}<text x="165" y="174" text-anchor="middle" font-family="MiSans,sans-serif" font-size="16" font-weight="700" letter-spacing="22">RHINE\xB7LAB</text></svg>`;
var bootMarkContour = "M295 73C295 41 273 15 240 15C221 15 207 23 192 38C186 43 181 47 176 52C127 96 103 128 70 128C38 128 15 101 15 70C15 39 37 15 70 15C103 15 127 48 156 75C182 99 208 128 240 128C273 128 295 105 295 73Z";
var brandHeading = `<h1 style="font-size:40px">CIPHERCHAT</h1><div>PRIVATE COMMUNICATION</div><p><span class="brand-analysis brand-spread" role="img" aria-label="MESSAGES">${[..."MESSAGES"].map((letter) => `<span aria-hidden="true">${letter}</span>`).join("")}</span> <b>OS</b></p>`;
export {
  DECRYPTION_END,
  DECRYPTION_START,
  SCAN_CORNERS,
  SCAN_FROM,
  SCAN_TO,
  bootMarkContour,
  bootMotion,
  decryptionFrame
};
