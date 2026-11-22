VARYING float vTheme;

void MAIN()
{
    BASE_COLOR = vec4(mix(uLight.rgb, uDarkColor.rgb, vTheme), 1.0);
    ROUGHNESS = uRoughness;
    METALNESS = uMetalness;
    SPECULAR_AMOUNT = 0.5;
}
