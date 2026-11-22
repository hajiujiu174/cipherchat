VARYING float vTheme;

// 根据插值数据、材质及当前动效计算片元颜色。
void MAIN()
{
    BASE_COLOR = vec4(mix(uLight.rgb, uDarkColor.rgb, vTheme), 1.0);
    ROUGHNESS = uRoughness;
    METALNESS = uMetalness;
    SPECULAR_AMOUNT = 0.5;
}
