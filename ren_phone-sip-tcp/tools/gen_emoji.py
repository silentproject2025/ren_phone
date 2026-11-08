#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# gen_emoji.py -- bikin emoji_data.h (sprite RGB565 10/16/24 px) + tabel kodepoint utk NyxOS.
# Semua sprite digambar sendiri (PIL, gaya flat), tanpa font emoji eksternal.
import math, sys
from PIL import Image, ImageDraw
S = 192            # kanvas kerja (supersample)
SIZES = [10, 16, 24]
Y  = (255, 204, 51); YD = (222, 150, 20); OUT = (120, 70, 10)
BR = (90, 50, 20); WH = (255, 255, 255); RD = (230, 50, 60); BL = (60, 130, 230)
def new(): return Image.new('RGBA', (S, S), (0, 0, 0, 0))
def circ(d, cx, cy, r, fill, outline=None, w=0):
    d.ellipse([cx-r, cy-r, cx+r, cy+r], fill=fill, outline=outline, width=w)
def base_face(col=Y, rim=YD):
    im = new(); d = ImageDraw.Draw(im)
    circ(d, 96, 96, 90, rim); circ(d, 96, 96, 82, col)
    return im, d
def eye_dot(d, cx, cy, r=10, c=BR): circ(d, cx, cy, r, c)
def smile(d, cy=116, w=44, h=30, width=9, c=BR):
    d.arc([96-w, cy-h, 96+w, cy+h], 15, 165, fill=c, width=width)
def frown(d, cy=140, w=36, h=26, width=9, c=BR):
    d.arc([96-w, cy-h, 96+w, cy+h], 195, 345, fill=c, width=width)
def open_mouth(d, cy=122, w=40, h=34, c=(110, 30, 30)):
    d.pieslice([96-w, cy-h, 96+w, cy+h], 0, 180, fill=c)
    d.pieslice([96-w+10, cy+h//2-4, 96+w-10, cy+h+8], 180, 360, fill=(240, 100, 110)) if False else None
def tear(d, x, y, r=9, c=(90, 190, 255)):
    d.ellipse([x-r, y-r, x+r, y+r+12], fill=c)
def heart_poly(d, cx, cy, sz, fill, outline=None):
    raw = []
    for i in range(0, 361, 4):
        t = math.radians(i)
        raw.append((16*math.sin(t)**3, -(13*math.cos(t)-5*math.cos(2*t)-2*math.cos(3*t)-math.cos(4*t))))
    x0 = min(p[0] for p in raw); x1 = max(p[0] for p in raw); y0 = min(p[1] for p in raw); y1 = max(p[1] for p in raw)
    k = sz / max(x1-x0, y1-y0)
    mx = (x0+x1)/2; my = (y0+y1)/2
    d.polygon([(cx+(x-mx)*k, cy+(y-my)*k) for x, y in raw], fill=fill, outline=outline)
def star_pts(cx, cy, ro, ri, n=5, rot=-90):
    pts = []
    for i in range(n*2):
        r = ro if i % 2 == 0 else ri
        a = math.radians(rot + i*180/n)
        pts.append((cx + r*math.cos(a), cy + r*math.sin(a)))
    return pts

E = {}   # nama -> Image
def add(name, fn): 
    im = fn(); E[name] = im

# ---------- wajah ----------
def f_grin():
    im, d = base_face()
    eye_dot(d, 66, 78); eye_dot(d, 126, 78)
    d.pieslice([56, 96, 136, 150], 0, 180, fill=(110, 30, 30))
    d.rectangle([66, 100, 126, 112], fill=WH)
    return im
add('grin', f_grin)
def f_beam():
    im, d = base_face()
    d.arc([48, 66, 84, 96], 190, 350, fill=BR, width=9); d.arc([108, 66, 144, 96], 190, 350, fill=BR, width=9)
    d.pieslice([54, 94, 138, 156], 0, 180, fill=(110, 30, 30)); d.rectangle([62, 98, 130, 112], fill=WH)
    return im
add('beam', f_beam)
def f_joy():
    im, d = base_face()
    d.arc([46, 66, 86, 100], 190, 350, fill=BR, width=9); d.arc([106, 66, 146, 100], 190, 350, fill=BR, width=9)
    d.pieslice([54, 92, 138, 156], 0, 180, fill=(110, 30, 30)); d.rectangle([62, 96, 130, 108], fill=WH)
    tear(d, 40, 100); tear(d, 152, 100)
    return im
add('joy', f_joy)
def f_smileye():
    im, d = base_face()
    d.arc([46, 66, 86, 100], 190, 350, fill=BR, width=9); d.arc([106, 66, 146, 100], 190, 350, fill=BR, width=9)
    smile(d, 112, 44, 34)
    circ(d, 52, 112, 10, (255, 140, 120)); circ(d, 140, 112, 10, (255, 140, 120))
    return im
add('smileye', f_smileye)
def f_slight():
    im, d = base_face(); eye_dot(d, 66, 78); eye_dot(d, 126, 78); smile(d, 112, 38, 26); return im
add('slight', f_slight)
def f_wink():
    im, d = base_face(); eye_dot(d, 126, 78); d.arc([46, 70, 86, 96], 190, 350, fill=BR, width=9); smile(d, 112, 40, 28); return im
add('wink', f_wink)
def f_hearteyes():
    im, d = base_face(); heart_poly(d, 62, 76, 40, RD); heart_poly(d, 130, 76, 40, RD); smile(d, 114, 40, 30); return im
add('hearteyes', f_hearteyes)
def f_kiss():
    im, d = base_face(); eye_dot(d, 66, 78); d.arc([108, 70, 148, 96], 190, 350, fill=BR, width=9)
    circ(d, 100, 124, 12, (230, 90, 100)); heart_poly(d, 150, 118, 22, RD); return im
add('kiss', f_kiss)
def f_yum():
    im, d = base_face(); eye_dot(d, 66, 78); eye_dot(d, 126, 78); smile(d, 108, 44, 30)
    d.rounded_rectangle([104, 120, 134, 152], 10, fill=(240, 90, 110)); return im
add('yum', f_yum)
def f_cool():
    im, d = base_face(); d.rounded_rectangle([34, 62, 90, 96], 10, fill=(25, 25, 25)); d.rounded_rectangle([102, 62, 158, 96], 10, fill=(25, 25, 25))
    d.rectangle([86, 68, 106, 76], fill=(25, 25, 25)); smile(d, 114, 40, 28); return im
add('cool', f_cool)
def f_think():
    im, d = base_face(); eye_dot(d, 66, 76); eye_dot(d, 126, 76)
    d.line([74, 132, 120, 128], fill=BR, width=9); d.line([110, 56, 146, 50], fill=BR, width=8)
    d.rounded_rectangle([100, 128, 132, 170], 12, fill=YD); return im
add('think', f_think)
def f_neutral():
    im, d = base_face(); eye_dot(d, 66, 78); eye_dot(d, 126, 78); d.line([66, 128, 126, 128], fill=BR, width=9); return im
add('neutral', f_neutral)
def f_sleep():
    im, d = base_face(); d.arc([46, 76, 86, 100], 10, 170, fill=BR, width=9); d.arc([106, 76, 146, 100], 10, 170, fill=BR, width=9)
    circ(d, 96, 130, 12, (110, 30, 30)); return im
add('sleep', f_sleep)
def f_cry():
    im, d = base_face(); eye_dot(d, 66, 78); eye_dot(d, 126, 78); frown(d, 144, 36, 26); tear(d, 62, 108, 10); return im
add('cry', f_cry)
def f_sob():
    im, d = base_face(); d.arc([46, 76, 86, 100], 10, 170, fill=BR, width=9); d.arc([106, 76, 146, 100], 10, 170, fill=BR, width=9)
    d.pieslice([66, 120, 126, 168], 180, 360, fill=(110, 30, 30))
    d.rectangle([56, 100, 70, 160], fill=(90, 190, 255)); d.rectangle([122, 100, 136, 160], fill=(90, 190, 255)); return im
add('sob', f_sob)
def f_angry():
    im, d = base_face(col=(235, 90, 70), rim=(160, 40, 30)); eye_dot(d, 66, 88); eye_dot(d, 126, 88)
    d.line([44, 60, 82, 76], fill=BR, width=10); d.line([148, 60, 110, 76], fill=BR, width=10); frown(d, 148, 34, 24); return im
add('angry', f_angry)
def f_scream():
    im, d = base_face(col=(255, 215, 90)); circ(d, 62, 80, 17, WH, BR, 4); circ(d, 130, 80, 17, WH, BR, 4); eye_dot(d, 62, 80, 7); eye_dot(d, 130, 80, 7)
    d.ellipse([74, 116, 118, 166], fill=(110, 30, 30)); d.arc([30, 120, 60, 150], 120, 240, fill=(90, 160, 255), width=8); return im
add('scream', f_scream)
def f_plead():
    im, d = base_face(); circ(d, 62, 80, 18, BR); circ(d, 130, 80, 18, BR); circ(d, 56, 72, 6, WH); circ(d, 124, 72, 6, WH); frown(d, 150, 28, 18); return im
add('plead', f_plead)
def f_halo():
    im, d = base_face(); d.ellipse([56, 2, 136, 26], outline=(255, 215, 0), width=9); d.arc([46, 70, 86, 100], 190, 350, fill=BR, width=9)
    d.arc([106, 70, 146, 100], 190, 350, fill=BR, width=9); smile(d, 112, 40, 28); return im
add('halo', f_halo)
def f_roll():
    im, d = base_face(); circ(d, 62, 78, 18, WH, BR, 4); circ(d, 130, 78, 18, WH, BR, 4); eye_dot(d, 66, 66, 7); eye_dot(d, 134, 66, 7)
    d.line([70, 132, 122, 132], fill=BR, width=9); return im
add('roll', f_roll)
def f_sweat():
    im, d = base_face(); eye_dot(d, 66, 78); eye_dot(d, 126, 78)
    d.pieslice([56, 96, 136, 150], 0, 180, fill=(110, 30, 30)); d.rectangle([66, 100, 126, 110], fill=WH); tear(d, 150, 50, 10); return im
add('sweat', f_sweat)
def f_smirk():
    im, d = base_face(); eye_dot(d, 66, 80); eye_dot(d, 126, 80)
    d.arc([60, 104, 150, 150], 20, 120, fill=BR, width=9); d.line([44, 62, 84, 70], fill=BR, width=8); return im
add('smirk', f_smirk)
def f_grimace():
    im, d = base_face(); eye_dot(d, 66, 78); eye_dot(d, 126, 78); d.rounded_rectangle([50, 108, 142, 146], 10, fill=WH, outline=BR, width=7)
    for x in (73, 96, 119): d.line([x, 110, x, 144], fill=BR, width=4)
    return im
add('grimace', f_grimace)
def f_starstruck():
    im, d = base_face(); d.polygon(star_pts(62, 78, 26, 11), fill=(255, 190, 0), outline=BR); d.polygon(star_pts(130, 78, 26, 11), fill=(255, 190, 0), outline=BR)
    d.pieslice([56, 98, 136, 156], 0, 180, fill=(110, 30, 30)); d.rectangle([66, 102, 126, 112], fill=WH); return im
add('starstruck', f_starstruck)
def f_squint():
    im, d = base_face(); d.line([44, 64, 80, 78], fill=BR, width=10); d.line([44, 92, 80, 78], fill=BR, width=10)
    d.line([148, 64, 112, 78], fill=BR, width=10); d.line([148, 92, 112, 78], fill=BR, width=10)
    d.pieslice([56, 98, 136, 156], 0, 180, fill=(110, 30, 30)); d.rectangle([66, 102, 126, 112], fill=WH); return im
add('squint', f_squint)
def f_tongue():
    im, d = base_face(); eye_dot(d, 66, 78); d.arc([108, 70, 148, 96], 190, 350, fill=BR, width=9); smile(d, 108, 44, 30)
    d.rounded_rectangle([104, 120, 134, 154], 10, fill=(240, 90, 110)); return im
add('tongue', f_tongue)
# ---------- hati ----------
def mkheart(col, outl):
    def f():
        im = new(); d = ImageDraw.Draw(im); heart_poly(d, 96, 98, 176, outl); heart_poly(d, 96, 98, 156, col)
        d.ellipse([46, 46, 70, 66], fill=tuple(min(255, c+70) for c in col)); return im
    return f
add('heart', mkheart((230, 40, 60), (150, 15, 30))); add('orange', mkheart((250, 140, 30), (170, 80, 10)))
add('yellow', mkheart((250, 210, 40), (180, 140, 10))); add('green', mkheart((60, 190, 90), (20, 120, 50)))
add('blue', mkheart((60, 130, 240), (20, 70, 160))); add('purple', mkheart((150, 90, 220), (90, 40, 150)))
add('black', mkheart((50, 50, 55), (10, 10, 10))); add('white', mkheart((245, 245, 245), (150, 150, 150)))
def f_broken():
    im = mkheart((230, 40, 60), (150, 15, 30))(); d = ImageDraw.Draw(im)
    d.line([100, 36, 84, 80, 112, 104, 92, 160], fill=(0, 0, 0, 0), width=12); return im
add('broken', f_broken)
# ---------- objek ----------
def f_up(flip=False):
    def f():
        im = new(); d = ImageDraw.Draw(im)
        d.rounded_rectangle([18, 92, 56, 168], 8, fill=BL, outline=(30, 80, 160), width=6)
        d.polygon([(64, 100), (96, 44), (118, 40), (118, 86), (162, 86), (172, 100), (166, 120), (172, 136), (160, 152), (162, 166), (146, 176), (70, 172), (64, 162)], fill=Y, outline=YD)
        if flip: im = im.transpose(Image.FLIP_TOP_BOTTOM)
        return im
    return f
add('thumbup', f_up()); add('thumbdown', f_up(True))
def f_fire():
    im = new(); d = ImageDraw.Draw(im)
    d.polygon([(96, 8), (130, 50), (150, 40), (168, 100), (150, 160), (96, 184), (42, 160), (24, 100), (46, 56), (66, 70)], fill=(240, 80, 20))
    d.polygon([(96, 60), (124, 100), (134, 140), (96, 172), (58, 140), (68, 100)], fill=(255, 170, 30))
    d.polygon([(96, 112), (114, 140), (96, 166), (78, 140)], fill=(255, 235, 120)); return im
add('fire', f_fire)
def f_star():
    im = new(); d = ImageDraw.Draw(im); d.polygon(star_pts(96, 102, 90, 38), fill=(255, 200, 30), outline=(200, 130, 0)); return im
add('star', f_star)
def f_sparkle():
    im = new(); d = ImageDraw.Draw(im)
    def sp(cx, cy, r, c=(255, 215, 60)): d.polygon([(cx, cy-r), (cx+r*0.28, cy-r*0.28), (cx+r, cy), (cx+r*0.28, cy+r*0.28), (cx, cy+r), (cx-r*0.28, cy+r*0.28), (cx-r, cy), (cx-r*0.28, cy-r*0.28)], fill=c)
    sp(80, 110, 68); sp(150, 50, 32); sp(150, 150, 26); return im
add('sparkle', f_sparkle)
def f_moon():
    im = new(); d = ImageDraw.Draw(im); circ(d, 96, 96, 84, (255, 214, 80)); circ(d, 130, 78, 74, (0, 0, 0, 0)); return im
add('moon', f_moon)
def f_sun():
    im = new(); d = ImageDraw.Draw(im)
    for i in range(12):
        a = math.radians(i*30); d.line([96+66*math.cos(a), 96+66*math.sin(a), 96+90*math.cos(a), 96+90*math.sin(a)], fill=(255, 170, 20), width=12)
    circ(d, 96, 96, 54, (255, 205, 40), (240, 150, 10), 6); return im
add('sun', f_sun)
def f_check():
    im = new(); d = ImageDraw.Draw(im); d.rounded_rectangle([10, 10, 182, 182], 30, fill=(70, 190, 100)); d.line([46, 100, 82, 138, 148, 58], fill=WH, width=22, joint='curve'); return im
add('check', f_check)
def f_cross():
    im = new(); d = ImageDraw.Draw(im); d.line([34, 34, 158, 158], fill=RD, width=32); d.line([158, 34, 34, 158], fill=RD, width=32); return im
add('cross', f_cross)
def f_bolt():
    im = new(); d = ImageDraw.Draw(im); d.polygon([(112, 6), (46, 106), (88, 106), (70, 186), (150, 76), (104, 76)], fill=(255, 210, 30), outline=(200, 140, 0)); return im
add('bolt', f_bolt)
def f_skull():
    im = new(); d = ImageDraw.Draw(im); circ(d, 96, 80, 66, (240, 240, 240), (150, 150, 150), 5)
    d.rounded_rectangle([60, 120, 132, 176], 10, fill=(240, 240, 240), outline=(150, 150, 150), width=5)
    circ(d, 70, 84, 18, (40, 40, 40)); circ(d, 122, 84, 18, (40, 40, 40)); d.polygon([(96, 100), (88, 118), (104, 118)], fill=(40, 40, 40))
    for x in (78, 96, 114): d.line([x, 138, x, 172], fill=(150, 150, 150), width=4)
    return im
add('skull', f_skull)
def f_eyes():
    im = new(); d = ImageDraw.Draw(im)
    for cx in (58, 134): d.ellipse([cx-40, 40, cx+40, 160], fill=WH, outline=(120, 120, 120), width=6); circ(d, cx+6, 108, 22, (60, 60, 60)); circ(d, cx+12, 100, 7, WH)
    return im
add('eyes', f_eyes)
def f_party():
    im = new(); d = ImageDraw.Draw(im)
    d.polygon([(20, 176), (60, 60), (136, 136)], fill=(250, 190, 40), outline=(200, 130, 10))
    d.polygon([(40, 150), (50, 120), (76, 146)], fill=(240, 90, 100))
    for (x, y, c) in [(110, 40, RD), (150, 70, BL), (130, 110, (80, 200, 100)), (166, 30, (200, 90, 220)), (90, 20, (250, 220, 60))]: circ(d, x, y, 9, c)
    d.line([128, 64, 150, 44], fill=BL, width=7); return im
add('party', f_party)
def f_coffee():
    im = new(); d = ImageDraw.Draw(im); d.rounded_rectangle([24, 70, 136, 160], 16, fill=WH, outline=(120, 80, 50), width=7)
    d.arc([112, 84, 176, 144], 270, 90, fill=(120, 80, 50), width=12); d.rectangle([34, 80, 126, 100], fill=(110, 70, 40))
    for x in (60, 84, 108): d.arc([x-10, 20, x+10, 60], 270, 90, fill=(150, 150, 150), width=6)
    d.rounded_rectangle([10, 164, 150, 180], 6, fill=(160, 120, 90)); return im
add('coffee', f_coffee)
def f_cake():
    im = new(); d = ImageDraw.Draw(im); d.rounded_rectangle([20, 96, 172, 172], 14, fill=(250, 200, 220), outline=(190, 110, 140), width=6)
    d.rounded_rectangle([20, 96, 172, 122], 12, fill=WH); 
    for x in (56, 96, 136): d.rectangle([x-5, 54, x+5, 94], fill=(80, 170, 230)); d.ellipse([x-8, 34, x+8, 56], fill=(255, 190, 40))
    return im
add('cake', f_cake)
def f_hundred():
    im = new(); d = ImageDraw.Draw(im); d.rounded_rectangle([6, 40, 186, 152], 18, fill=(30, 30, 40))
    d.line([40, 62, 40, 130], fill=RD, width=16); d.ellipse([66, 62, 104, 130], outline=RD, width=14); d.ellipse([116, 62, 154, 130], outline=RD, width=14)
    d.line([30, 150, 160, 150], fill=RD, width=8); return im
add('hundred', f_hundred)
def f_robot():
    im = new(); d = ImageDraw.Draw(im); d.rounded_rectangle([24, 50, 168, 172], 22, fill=(180, 190, 205), outline=(90, 100, 120), width=7)
    d.line([96, 50, 96, 22], fill=(90, 100, 120), width=8); circ(d, 96, 20, 12, RD)
    circ(d, 64, 98, 20, (60, 200, 240)); circ(d, 128, 98, 20, (60, 200, 240)); d.rounded_rectangle([60, 136, 132, 156], 8, fill=(60, 70, 90)); return im
add('robot', f_robot)
def f_gamepad():
    im = new(); d = ImageDraw.Draw(im); d.rounded_rectangle([8, 56, 184, 150], 40, fill=(80, 90, 120), outline=(40, 45, 70), width=7)
    d.rectangle([34, 90, 76, 104], fill=WH); d.rectangle([48, 76, 62, 118], fill=WH); circ(d, 134, 84, 10, RD); circ(d, 156, 106, 10, (80, 200, 100)); return im
add('gamepad', f_gamepad)
def f_phone():
    im = new(); d = ImageDraw.Draw(im); d.rounded_rectangle([52, 6, 140, 186], 18, fill=(40, 45, 60), outline=(150, 160, 180), width=7)
    d.rounded_rectangle([62, 28, 130, 150], 6, fill=(90, 190, 240)); circ(d, 96, 168, 8, (150, 160, 180)); return im
add('phone', f_phone)
def f_unknown():
    im = new(); d = ImageDraw.Draw(im); d.rounded_rectangle([14, 14, 178, 178], 28, fill=(110, 115, 130), outline=(70, 75, 90), width=7)
    d.arc([58, 34, 134, 108], 200, 70, fill=WH, width=16); d.line([102, 100, 96, 124], fill=WH, width=16); circ(d, 96, 152, 10, WH); return im
add('unknown', f_unknown)

# ---------- peta kodepoint ----------
# (nama sprite, kodepoint kanonik, [alias kodepoint])
LIST = [
 ('grin', 0x1F600, [0x1F603, 0x1F604]), ('beam', 0x1F601, []), ('joy', 0x1F602, [0x1F923]), ('smileye', 0x1F60A, [0x1F970, 0x263A]),
 ('slight', 0x1F642, [0x1F643]), ('wink', 0x1F609, []), ('hearteyes', 0x1F60D, []), ('kiss', 0x1F618, [0x1F617, 0x1F619, 0x1F61A]),
 ('yum', 0x1F60B, []), ('cool', 0x1F60E, []), ('think', 0x1F914, []), ('neutral', 0x1F610, [0x1F611, 0x1F636]),
 ('sleep', 0x1F634, [0x1F62A]), ('cry', 0x1F622, [0x1F625, 0x1F61E, 0x1F614, 0x1F629]), ('sob', 0x1F62D, []), ('angry', 0x1F621, [0x1F620, 0x1F92C]),
 ('scream', 0x1F631, [0x1F628, 0x1F630]), ('plead', 0x1F97A, []), ('halo', 0x1F607, []), ('roll', 0x1F644, []),
 ('sweat', 0x1F605, [0x1F613]), ('smirk', 0x1F60F, []), ('grimace', 0x1F62C, []), ('starstruck', 0x1F929, []),
 ('squint', 0x1F606, []), ('tongue', 0x1F61C, [0x1F61B, 0x1F61D]),
 ('heart', 0x2764, [0x1F496, 0x1F495, 0x1F497, 0x1F493, 0x1F498, 0x1F49E, 0x1F49D]), ('orange', 0x1F9E1, []), ('yellow', 0x1F49B, []),
 ('green', 0x1F49A, []), ('blue', 0x1F499, []), ('purple', 0x1F49C, []), ('black', 0x1F5A4, []), ('white', 0x1F90D, []),
 ('broken', 0x1F494, []), ('thumbup', 0x1F44D, [0x1F44C]), ('thumbdown', 0x1F44E, []), ('fire', 0x1F525, []),
 ('star', 0x2B50, [0x1F31F]), ('sparkle', 0x2728, []), ('moon', 0x1F319, [0x1F31A, 0x1F31B, 0x1F31C]), ('sun', 0x2600, [0x1F31E]),
 ('check', 0x2705, [0x2714]), ('cross', 0x274C, [0x2716]), ('bolt', 0x26A1, []), ('skull', 0x1F480, [0x2620]),
 ('eyes', 0x1F440, []), ('party', 0x1F389, [0x1F38A]), ('coffee', 0x2615, []), ('cake', 0x1F382, []),
 ('hundred', 0x1F4AF, []), ('robot', 0x1F916, []), ('gamepad', 0x1F3AE, []), ('phone', 0x1F4F1, []),
]
# halaman keyboard: urutan sprite (30 per halaman)
def to565(r, g, b): return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
def sprite(im, n):
    small = im.convert('RGBa').resize((n, n), Image.LANCZOS).convert('RGBA')
    out = []
    for y in range(n):
        for x in range(n):
            r, g, b, a = small.getpixel((x, y))
            if a < 110: out.append(0)
            else:
                v = to565(r, g, b)
                out.append(v if v else 0x0821)
    return out
def main(outdir):
    names = [x[0] for x in LIST] + ['unknown']
    N = len(names)
    lines = ['// emoji_data.h -- DIBUAT OTOMATIS oleh gen_emoji.py, jangan diedit manual.',
             '// %d sprite (urutan = LIST di gen_emoji.py), ukuran %s px, RGB565, 0x0000 = transparan.' % (N, '/'.join(map(str, SIZES))),
             '#pragma once', '#include <stdint.h>', '#define EMJ_COUNT %d' % N, '#define EMJ_UNKNOWN %d' % (N-1)]
    lines.append('static const uint8_t emjSizes[3] = {%s};' % ','.join(map(str, SIZES)))
    for n in SIZES:
        lines.append('static const uint16_t emjPx%d[EMJ_COUNT][%d] PROGMEM = {' % (n, n*n))
        for nm in names:
            px = sprite(E[nm], n)
            lines.append('{' + ','.join('0x%04X' % p for p in px) + '}, // ' + nm)
        lines.append('};')
    # kanonik
    canon = [x[1] for x in LIST] + [0x2753]
    lines.append('static const uint32_t emjCanon[EMJ_COUNT] = {' + ','.join('0x%X' % c for c in canon) + '};')
    pairs = []
    for i, (nm, cp, al) in enumerate(LIST):
        pairs.append((cp, i))
        for a in al: pairs.append((a, i))
    pairs.sort()
    assert len(set(p[0] for p in pairs)) == len(pairs), 'kodepoint dobel'
    lines.append('struct EmjPair { uint32_t cp; uint8_t idx; };')
    lines.append('#define EMJ_PAIRS %d' % len(pairs))
    lines.append('static const EmjPair emjPairs[EMJ_PAIRS] = {' + ','.join('{0x%X,%d}' % p for p in pairs) + '};')
    # halaman keyboard
    order = list(range(len(LIST)))
    lines.append('#define EMJ_KB_COUNT %d' % len(order))
    lines.append('static const uint8_t emjKbOrder[EMJ_KB_COUNT] = {' + ','.join(map(str, order)) + '};')
    def lab(i):
        if i is None: return '""'
        return '"' + ''.join('\\x%02X' % b for b in chr(0xE000 + i).encode('utf-8')) + '"'
    def page(start):
        rows = []
        for r in range(3):
            cells = []
            for c in range(10):
                k = start + r * 10 + c
                cells.append(lab(order[k] if k < len(order) else None))
            rows.append('{' + ','.join(cells) + '}')
        return '{' + ','.join(rows) + '}'
    lines.append('// halaman emoji keyboard virtual (label = emoji dlm bentuk PUA U+E000+idx, UTF-8 3 byte)')
    lines.append('static const char* kbE1[3][10] = ' + page(0) + ';')
    lines.append('static const char* kbE2[3][10] = ' + page(30) + ';')
    open(outdir + '/emoji_data.h', 'w').write('\n'.join(lines) + '\n')
    # pratinjau PNG
    sheet = Image.new('RGB', (len(names) // 2 * 56 + 8, 2 * 56 + 8), (24, 26, 34)); 
    for i, nm in enumerate(names):
        t = E[nm].resize((48, 48), Image.LANCZOS); sheet.paste(t, (8 + (i % (len(names)//2)) * 56 % (sheet.width-8), 8 + (i // (len(names)//2)) * 56), t)
    sheet.save(outdir + '/emoji_preview.png')
    print('OK', N, 'sprite;', len(pairs), 'kodepoint')
if __name__ == '__main__': main(sys.argv[1] if len(sys.argv) > 1 else '.')
