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
    "pylone":(78,138),
    "porte_garage":(184,147),
    "panneaux_solaires":(292,112),
    "ballon_ecs":(49,79),
    "vehicule_electrique":(136,97),
    "batterie_coque":(46,90),
}

# The user's approved panel SVG is an 800x450 transparent overlay already
# aligned with the approved house: render it at 1:1 and crop blank margins.
# This does NOT move, resize, or reinterpret any of its SVG vertices.
# Render bbox in the house coordinate space: x 240..532, y 68..180.
SOLAR_CROP=(240,68,532,180)
# The pylon is already present in the user-validated house SVG. Split it
# into its own native raster ONLY at conversion time, without changing that SVG.
# These bounds cover the whole tower and no portion of the house (800x450).
PYLON_CROP=(18,132,96,270)

def make_asset(svg_bytes:bytes,w:int,h:int,crop=None,erase_pylon=False)->bytes:
    if crop is None:
        rendered=cairosvg.svg2png(bytestring=svg_bytes,output_width=w,output_height=h)
        rgba=Image.open(io.BytesIO(rendered)).convert("RGBA")
    else:
        import xml.etree.ElementTree as ET
        root=ET.fromstring(svg_bytes)
        if crop==SOLAR_CROP:
            assert root.attrib.get("viewBox")=="0 0 800 450", "Approved panel must align with the house 800x450 canvas"
        else:
            assert crop==PYLON_CROP, "Only solar and original-house pylon crops are permitted"
        assert (crop[2]-crop[0],crop[3]-crop[1])==(w,h)
        rendered=cairosvg.svg2png(bytestring=svg_bytes,output_width=800,output_height=450)
        rgba=Image.open(io.BytesIO(rendered)).convert("RGBA").crop(crop)
    if erase_pylon:
        assert crop is None and (w,h)==(800,450)
        # The source SVG is unchanged; erase ONLY the pixels belonging to the
        # existing pylon, now drawn in a separate sprite at the chosen position.
        rgba.paste((0,0,0,0),PYLON_CROP)
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
        source="maison_fond" if name=="pylone" else name
        crop=SOLAR_CROP if name=="panneaux_solaires" else (PYLON_CROP if name=="pylone" else None)
        out=make_asset((SVG_DIR/(source+".svg")).read_bytes(),w,h,
                       crop,erase_pylon=name=="maison_fond")
        (OUT_DIR/(name+".i8r")).write_bytes(out)
        print(f"{name}: {w}x{h} -> {len(out)} bytes; verified")
