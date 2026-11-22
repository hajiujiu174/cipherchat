"""Draw the existing green padlock branding as a multi-resolution Windows icon."""
from pathlib import Path
import sys
from PIL import Image, ImageDraw

image = Image.new("RGBA", (256, 256))
draw = ImageDraw.Draw(image)
draw.rounded_rectangle((8, 8, 248, 248), radius=64, fill="#1f7a6d")
draw.arc((83, 48, 173, 149), 180, 360, fill="white", width=17)
draw.rounded_rectangle((69, 110, 187, 202), radius=19, fill="white")
draw.ellipse((115, 133, 141, 159), fill="#1f7a6d")
draw.rounded_rectangle((122, 145, 134, 178), radius=5, fill="#1f7a6d")
image.save(Path(sys.argv[1]), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
