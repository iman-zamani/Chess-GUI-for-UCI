from PIL import Image

def create_gradient_square(size, inner_color, outer_color):
    # Create a new blank image
    img = Image.new("RGB", (size, size))
    center_x, center_y = size / 2, size / 2
    max_distance = ((size / 2) ** 2 + (size / 2) ** 2) ** 0.5

    # Set pixels
    for x in range(size):
        for y in range(size):
            # Calculate distance from the center
            distance = ((x - center_x) ** 2 + (y - center_y) ** 2) ** 0.5
            # Normalize distance
            distance = distance / max_distance

            # Calculate new color
            r = int((outer_color[0] - inner_color[0]) * distance + inner_color[0])
            g = int((outer_color[1] - inner_color[1]) * distance + inner_color[1])
            b = int((outer_color[2] - inner_color[2]) * distance + inner_color[2])

            # Place the color in the image
            img.putpixel((x, y), (r, g, b))

    # Save the image
    img.save('./targetSquare.png')

# Define the size of the image
image_size = 240
# Define the colors: inner (bright red) and outer (darker red)
inner_color = (255, 0, 0)
outer_color = (128, 0, 0)

# Generate the image
create_gradient_square(image_size, inner_color, outer_color)
