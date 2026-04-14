from PIL import Image, ImageDraw

SIZE = 24

def save(name, draw_fn):
    img = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw_fn(draw)
    img.save(f'{name}.png')

def play(draw):
    draw.polygon([(8, 6), (8, 18), (19, 12)], fill=(255, 255, 255, 255))

def pause(draw):
    draw.rectangle([7, 6, 11, 18], fill=(255, 255, 255, 255))
    draw.rectangle([14, 6, 18, 18], fill=(255, 255, 255, 255))

def stop(draw):
    draw.rectangle([6, 6, 18, 18], fill=(255, 255, 255, 255))

def prev(draw):
    draw.rectangle([5, 6, 8, 18], fill=(255, 255, 255, 255))
    draw.polygon([(9, 12), (19, 6), (19, 18)], fill=(255, 255, 255, 255))

def next(draw):
    draw.polygon([(5, 6), (5, 18), (15, 12)], fill=(255, 255, 255, 255))
    draw.rectangle([16, 6, 19, 18], fill=(255, 255, 255, 255))

def volume(draw):
    # speaker body
    draw.polygon([(4, 9), (10, 9), (16, 5), (16, 19), (10, 15), (4, 15)], fill=(255, 255, 255, 255))
    # sound wave
    draw.arc([16, 6, 22, 18], start=-60, end=60, fill=(255, 255, 255, 255), width=2)

def mute(draw):
    draw.polygon([(4, 9), (10, 9), (16, 5), (16, 19), (10, 15), (4, 15)], fill=(200, 200, 200, 255))
    # cross
    draw.line([(17, 7), (23, 17)], fill=(255, 100, 100, 255), width=2)
    draw.line([(17, 17), (23, 7)], fill=(255, 100, 100, 255), width=2)

for n, f in [('play', play), ('pause', pause), ('stop', stop), ('prev', prev), ('next', next), ('volume', volume), ('mute', mute)]:
    save(n, f)

print('Icons generated.')
