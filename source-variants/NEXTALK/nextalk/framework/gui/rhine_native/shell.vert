VARYING float vHeight;
VARYING float vTheme;

void MAIN()
{
    // Cover height in the cassette's own frame (0 at the base, 1 at the top edge).
    vHeight = VERTEX.y / 3.7;
    vTheme = INSTANCE_DATA.x;
}
