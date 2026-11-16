"""Join the three turnaround stills into one labelled sheet.

python make_sheet.py <out_dir> <tag> [out.png]
Reads <out_dir>/<tag>_front.png, _back.png, _right.png (from turnaround.py); writes <out_dir>/<tag>.png by default.
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

out_dir, tag = sys.argv[1], sys.argv[2]
out = sys.argv[3] if len(sys.argv) > 3 else os.path.join(out_dir, tag + ".png")
VIEWS = ["front", "back", "right"]
ims = [Image.open(os.path.join(out_dir, "%s_%s.png" % (tag, v))).convert("RGB") for v in VIEWS]
w, h = ims[0].size
GAP, BAR = 4, 40
sheet = Image.new("RGB", (3 * w + 2 * GAP, h + BAR), (24, 24, 24))
d = ImageDraw.Draw(sheet)
try:
    font = ImageFont.truetype("arial.ttf", 24)
except OSError:
    font = ImageFont.load_default()
for i, (v, im) in enumerate(zip(VIEWS, ims)):
    x = i * (w + GAP)
    sheet.paste(im.resize((w, h)), (x, BAR))
    d.text((x + 10, 6), ("%s  |  %s" % (tag, v)) if i == 0 else v, fill=(255, 220, 60), font=font)
sheet.save(out)
print(out, sheet.size)
