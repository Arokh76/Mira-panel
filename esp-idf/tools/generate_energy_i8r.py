#!/usr/bin/env python3
"""Convert approved SVG source files to compact LVGL I8/RLE assets OFFLINE.

No ThorVG or SVG parser is linked into the firmware. Output format:
1024 bytes palette (256 entries BGRA), then pairs of [run_length, index].
Each run length is 1..255, and the decoded pixel plane is exactly w*h bytes.
"""
from pathlib import Path
import io
import cairosvg
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
SVG_DIR=ROOT/"esp-idf"/"main"/"energy_svg"
OUT_DIR=ROOT/"esp-idf"/"main"/"energy_raster"
SIZES={
    "maison_fond":(800,450),
    "porte_garage":(184,147),
    "panneaux_solaires":(242,88),
    "ballon_ecs":(49,79),
    "vehicule_electrique":(136,97),
    "batterie_coque":(46,90),
}

def make_asset(svg_bytes:bytes,w:int,h:int)->bytes:
    rendered=cairosvg.svg2png(bytestring=svg_bytes,output_width=w,output_height=h)
    rgba=Image.open(io.BytesIO(rendered)).convert("RGBA")
    indexed=rgba.quantize(colors=256,method=Image.Quantize.FASTOCTREE,
                          dither=Image.Dither.NONE)
    palette=indexed.getpalette("RGBA")
    assert len(palette)==1024
    bgra=bytearray(1024)
    for i in range(256):
        r,g,b,a=palette[4*i:4*i+4]
        bgra[4*i:4*i+4]=bytes((b,g,r,a))
    raw=indexed.tobytes()
    assert len(raw)==w*h
    encoded=bytearray(bgra)
    pos=0
    while pos<len(raw):
        value=raw[pos]
        end=min(pos+255,len(raw))
        i=pos+1
        while i<end and raw[i]==value:
            i+=1
        encoded.extend((i-pos,value))
        pos=i
    verified=bytearray()
    for i in range(1024,len(encoded),2):
        verified.extend(bytes([encoded[i+1]])*encoded[i])
    assert verified==raw, "Corrupt RLE output"
    return bytes(encoded)

if __name__=="__main__":
    OUT_DIR.mkdir(parents=True,exist_ok=True)
    for name,(w,h) in SIZES.items():
        out=make_asset((SVG_DIR/(name+".svg")).read_bytes(),w,h)
        (OUT_DIR/(name+".i8r")).write_bytes(out)
        print(f"{name}: {w}x{h} -> {len(out)} bytes; verified")
