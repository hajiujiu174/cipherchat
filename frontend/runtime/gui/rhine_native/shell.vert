VARYING float vHeight;
VARYING float vTheme;

// 根据顶点位置及实例参数计算渲染位置，并把插值数据传给片元阶段。
void MAIN()
{
    // Cover height in the cassette's own frame (0 at the base, 1 at the top edge).
    vHeight = VERTEX.y / 3.7;
    vTheme = INSTANCE_DATA.x;
}
