VARYING float vHeight;
VARYING float vTheme;

// glass-reveal.ts: FEATHER 0.12, frosted 0.42 → clear 0.025 roughness.
float glassRevealAtHeight(float progress, float height)
{
    float edge = 1.0 - 1.24 * clamp(progress, 0.0, 1.0);
    return smoothstep(edge, edge + 0.24, clamp(height, 0.0, 1.0));
}

void MAIN()
{
    float reveal = glassRevealAtHeight(uClarity, vHeight);
    // Array palette (low) → extracted palette (high).
    // scene.ts tints the array shell from vec3(0.40, 0.30, 0.20) at the base; it is
    // seen through transmission, so uTint scales its visible strength here.
    vec3 base = mix(vec3(1.0), vec3(0.40, 0.30, 0.20), uTint);
    vec3 low = vec3(1.0, 0.969, 0.929) * mix(base, vec3(1.0, 0.98, 0.94), smoothstep(0.1, 1.0, vHeight));
    vec3 high = vec3(1.0, 0.992, 0.980);
    vec3 color = mix(low, high, uQuality);
    vec3 dark = mix(uDark.rgb, vec3(0.92, 0.96, 0.97), reveal * (1.0 - uArray));
    BASE_COLOR = vec4(mix(color, dark, vTheme), 1.0);
    ROUGHNESS = mix(mix(0.28, 0.42, uQuality), 0.025, reveal);
    METALNESS = 0.0;
    SPECULAR_AMOUNT = 0.5;
    CLEARCOAT_AMOUNT = mix(0.3, 0.0, uQuality);
    CLEARCOAT_ROUGHNESS = 0.25;
    TRANSMISSION_FACTOR = mix(mix(uTransmission, 0.9, uQuality), 0.985, reveal);
    THICKNESS_FACTOR = mix(0.12, 0.018, reveal);
    ATTENUATION_COLOR = mix(vec3(0.933, 0.902, 0.875), vec3(1.0), reveal);
    ATTENUATION_DISTANCE = mix(2.0, 8.0, reveal);
}
