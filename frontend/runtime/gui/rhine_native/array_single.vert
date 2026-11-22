VARYING float vTheme;

// 根据顶点位置及实例参数计算渲染位置，并把插值数据传给片元阶段。
void MAIN()
{
    vTheme = uTheme;
}
