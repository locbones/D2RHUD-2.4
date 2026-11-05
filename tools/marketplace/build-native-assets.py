"""Compile the generated marketplace button into native SpA1 UI atlases.

ImageMagick performs mechanical resolution/state conversion; no artwork is
drawn by the HUD. Only the compiled sprites are installed for players.
"""
import argparse
from pathlib import Path
import struct
import subprocess

ASSETS = Path(__file__).with_name('assets')

def compile_button(source, width, height):
    frames=[]
    # Native ButtonWidget selects normal, pressed, and hover frames itself.
    for light in ('100', '78', '120'):
        result=subprocess.run(['magick','-background','none',str(source),'-resize',f'{width}x{height}!',
            '-modulate',f'{light},100,100','-depth','8','RGBA:-'],check=True,stdout=subprocess.PIPE)
        if len(result.stdout)!=width*height*4:raise ValueError('Unexpected native button pixel count')
        frames.append(result.stdout)
    atlas=bytearray()
    for y in range(height):
        for frame in frames:atlas.extend(frame[y*width*4:(y+1)*width*4])
    header=bytearray(40);header[:4]=b'SpA1'
    struct.pack_into('<HHII',header,4,31,width,width*3,height)
    struct.pack_into('<I',header,20,3)
    return header+atlas

def build(source):
    for name,width,height in [('MarketplaceButton.sprite',117,116),('MarketplaceButton.lowend.sprite',58,58)]:
        target=ASSETS/name;target.write_bytes(compile_button(source,width,height));print(target)
    for name,size in [('MarketplaceGift.sprite',64),('MarketplaceGift.lowend.sprite',32)]:
        target=ASSETS/name;target.write_bytes(compile_button(ASSETS/'MarketplaceGift.svg',size,size));print(target)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,default=ASSETS/'marketplace-button-source.png')
    build(p.parse_args().source)
