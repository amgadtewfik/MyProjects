#!/usr/bin/env python3
"""Create app.ico for PLY Viewer - a cool 3D wireframe cube / point cloud icon in blue on transparent background."""
import math
import random
from PIL import Image, ImageDraw, ImageFilter

def project(point, angle_x, angle_y, angle_z, scale, cx, cy):
    x, y, z = point
    cy_, sy = math.cos(angle_y), math.sin(angle_y)
    cx_, sx = math.cos(angle_x), math.sin(angle_x)
    cz_, sz = math.cos(angle_z), math.sin(angle_z)
    x2 = x * cy_ + z * sy
    z2 = -x * sy + z * cy_
    y2 = y * cx_ - z2 * sx
    z3 = y * sx + z2 * cx_
    x3 = x2 * cz_ - y2 * sz
    y3 = x2 * sz + y2 * cz_
    return cx + x3 * scale, cy - y3 * scale, z3

def create_icon():
    sizes = [(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (16, 16)]
    images = []

    cube_size = 1.0
    s = cube_size
    cube_vertices = [
        (-s, -s, -s), ( s, -s, -s), ( s,  s, -s), (-s,  s, -s),
        (-s, -s,  s), ( s, -s,  s), ( s,  s,  s), (-s,  s,  s),
    ]
    cube_edges = [
        (0,1),(1,2),(2,3),(3,0),
        (4,5),(5,6),(6,7),(7,4),
        (0,4),(1,5),(2,6),(3,7),
    ]
    cube_faces = [
        (0,1,2,3), (5,4,7,6), (4,0,3,7),
        (1,5,6,2), (3,2,6,7), (4,5,1,0),
    ]

    random.seed(7)
    point_cloud = []
    for _ in range(110):
        u = random.random()
        v = random.random()
        w = random.random()
        r = 0.55 + 0.55 * random.random()
        jitter_x = (random.random() - 0.5) * 0.35
        jitter_y = (random.random() - 0.5) * 0.35
        jitter_z = (random.random() - 0.5) * 0.35
        x = (u - 0.5) * 2.0 * r + jitter_x
        y = (v - 0.5) * 2.0 * r + jitter_y
        z = (w - 0.5) * 2.0 * r + jitter_z
        if abs(x) < s and abs(y) < s and abs(z) < s:
            continue
        point_cloud.append((x, y, z))

    ax = math.radians(28)
    ay = math.radians(35)
    az = math.radians(0)

    for w, h in sizes:
        scale = min(w, h) * 0.30
        cx, cy = w / 2, h / 2

        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img, "RGBA")

        projected_faces = []
        for fi, face in enumerate(cube_faces):
            pts = []
            zs = []
            for vi in face:
                v = cube_vertices[vi]
                px, py, pz = project(v, ax, ay, az, scale, cx, cy)
                pts.append((int(round(px)), int(round(py))))
                zs.append(pz)
            avg_z = sum(zs) / 4.0
            projected_faces.append((avg_z, fi, pts))
        projected_faces.sort(key=lambda t: t[0])

        for avg_z, fi, pts in projected_faces:
            shade = 1.0 - min(1.0, max(0.0, (avg_z + 1.2) / 2.4))
            base_alpha = 55 + int(55 * shade)
            r = int(10 + 25 * shade)
            g = int(50 + 60 * shade)
            b = int(110 + 90 * shade)
            draw.polygon(pts, fill=(r, g, b, base_alpha))

        projected_edges = []
        for a, b in cube_edges:
            pa = cube_vertices[a]
            pb = cube_vertices[b]
            axp, ayp, azp = project(pa, ax, ay, az, scale, cx, cy)
            bxp, byp, bzp = project(pb, ax, ay, az, scale, cx, cy)
            avg_z = (azp + bzp) / 2.0
            projected_edges.append((avg_z, (int(round(axp)), int(round(ayp))), (int(round(bxp)), int(round(byp)))))
        projected_edges.sort(key=lambda t: t[0])

        edge_layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        edge_draw = ImageDraw.Draw(edge_layer, "RGBA")
        for avg_z, p1, p2 in projected_edges:
            shade = 1.0 - min(1.0, max(0.0, (avg_z + 1.2) / 2.4))
            edge_r = int(30 + 80 * shade)
            edge_g = int(140 + 80 * shade)
            edge_b = int(180 + 75 * shade)
            line_w = max(1, int(min(w, h) / 64))
            edge_draw.line([p1, p2], fill=(edge_r, edge_g, edge_b, 255), width=line_w)
        img = Image.alpha_composite(img, edge_layer)

        projected_points = []
        for p in point_cloud:
            px, py, pz = project(p, ax, ay, az, scale, cx, cy)
            projected_points.append((px, py, pz))
        projected_points.sort(key=lambda t: t[2])

        point_layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        point_draw = ImageDraw.Draw(point_layer, "RGBA")
        for px, py, pz in projected_points:
            shade = 1.0 - min(1.0, max(0.0, (pz + 1.5) / 3.0))
            r = int(40 + 70 * shade)
            g = int(150 + 70 * shade)
            b = int(170 + 85 * shade)
            radius = max(1, int(min(w, h) / 96))
            ipx, ipy = int(round(px)), int(round(py))
            point_draw.ellipse(
                (ipx - radius, ipy - radius, ipx + radius, ipy + radius),
                fill=(r, g, b, 220),
            )
        point_layer = point_layer.filter(ImageFilter.GaussianBlur(radius=max(0.4, min(w, h) / 180)))
        img = Image.alpha_composite(img, point_layer)

        bright_layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        bright_draw = ImageDraw.Draw(bright_layer, "RGBA")
        for px, py, pz in projected_points:
            shade = 1.0 - min(1.0, max(0.0, (pz + 1.5) / 3.0))
            r = int(110 + 60 * shade)
            g = int(200 + 40 * shade)
            b = int(220 + 35 * shade)
            radius = max(1, int(min(w, h) / 160))
            ipx, ipy = int(round(px)), int(round(py))
            bright_draw.ellipse(
                (ipx - radius, ipy - radius, ipx + radius, ipy + radius),
                fill=(r, g, b, 255),
            )
        img = Image.alpha_composite(img, bright_layer)

        origin_x, origin_y, _ = project((0, 0, 0), ax, ay, az, scale, cx, cy)
        x_end = project((0.7, 0, 0), ax, ay, az, scale, cx, cy)
        y_end = project((0, 0.7, 0), ax, ay, az, scale, cx, cy)
        z_end = project((0, 0, 0.7), ax, ay, az, scale, cx, cy)
        gizmo_layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        gizmo_draw = ImageDraw.Draw(gizmo_layer, "RGBA")
        line_w = max(1, int(min(w, h) / 80))
        ox, oy = int(round(origin_x)), int(round(origin_y))
        xe = (int(round(x_end[0])), int(round(x_end[1])))
        ye = (int(round(y_end[0])), int(round(y_end[1])))
        ze = (int(round(z_end[0])), int(round(z_end[1])))
        gizmo_draw.line([(ox, oy), xe], fill=(60, 160, 200, 230), width=line_w)
        gizmo_draw.line([(ox, oy), ye], fill=(90, 200, 220, 230), width=line_w)
        gizmo_draw.line([(ox, oy), ze], fill=(140, 220, 230, 230), width=line_w)
        img = Image.alpha_composite(img, gizmo_layer)

        img = img.filter(ImageFilter.GaussianBlur(radius=max(0.0, min(w, h) / 600.0)))
        images.append(img)

    images[0].save("app.ico", format="ICO", sizes=sizes, append_images=images[1:])
    print("Created app.ico")

if __name__ == "__main__":
    create_icon()
