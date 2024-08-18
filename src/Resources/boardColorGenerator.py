#pip install pillow 
from PIL import Image, ImageDraw


img = Image.new('RGBA', (480, 240), (255, 255, 255, 255))
draw = ImageDraw.Draw(img)


color1 = (239, 210, 185, 255)  # first rectangle color
color2 = (158, 110, 89, 255)   # second rectangle color

# draw rectangles
draw.rectangle([0, 0, 240, 240], fill=color1)
draw.rectangle([240, 0, 480, 240], fill=color2)


img.save('boardColor.png')

