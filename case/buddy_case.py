"""Desk Buddy case for the Seeed XIAO 1.47" Touch Display (ESP32-S3 Plus).

Two printed parts:
  front_shell  tub with screen window, flexure buttons, mic hole and port cut-outs
  back_cover   press-fit lid with board hold-down posts, an FPC-antenna recess and
               a wedge foot: the case sits on the table with the screen leaning
               back 45 degrees

Geometry is modelled in BOARD coordinates taken from Seeed's STEP model:
  x  across the board (25 mm), x- edge has the power slide switch -> case top
  y  along the board (50 mm), y=0 end has buttons + mic, USB-C at y=51.4
  z  thickness, the display faces -z, components on the back at +z

Run:  .venv-cad/bin/python case/buddy_case.py [--check board.step]
Outputs STL/STEP files and preview PNGs in case/out/.
"""
import argparse
import math
import os

import cadquery as cq
from OCP.BRepBuilderAPI import BRepBuilderAPI_Transform
from OCP.gp import gp_Trsf

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

# ---------------- parameters (mm) ----------------
CLR = 0.25          # board-to-wall clearance
WALL = 2.0
FRONT_T = 1.6       # front plate thickness
BACK_T = 1.6        # back cover thickness
CORNER_R = 4.0
EDGE_R = 1.2
LID_FIT = 0.15      # lip-to-wall clearance (raise if the lid is too tight)
LIP_T = 1.2
LIP_H = 3.0
BACK_SPACE = 2.5    # room above the tallest back component (plugs, antenna cable)
SNAP = 0.3          # snap-ridge height on the lid lip (0 disables the snap)
TILT_DEG = 45       # screen lean-back when sitting on the table
FOOT_LEN = None     # length of the wedge's table face; None = full size (wedge covers the whole back)

# Board features (from the STEP model)
PCB_X, PCB_Y, PCB_T = 25.0, 50.0, 1.51
GLASS_FRONT_Z = -3.43
BACK_COMPONENT_Z = 7.10           # tallest part on the back (battery connector)
USB_X, USB_Z = 0.0, 4.39          # USB-C centre, face at y=51.39
USB_FACE_Y = 51.39
VA_W, VA_H, VA_CY = 17.75, 32.93, 29.0   # visible area of the cover glass
BUTTONS = [(5.5, 5.0), (-5.5, 5.0)]     # tact switches, plunger top at z=-1.59
BUTTON_TOP_Z = -1.59
MIC = (0.0, 5.0)

# Derived cavity / outer envelope
IX0, IX1 = -PCB_X / 2 - CLR, PCB_X / 2 + CLR
IY0, IY1 = -CLR, USB_FACE_Y + 0.3
IZ0 = GLASS_FRONT_Z
IZ1 = BACK_COMPONENT_Z + BACK_SPACE        # inner face of back cover
OX0, OX1 = IX0 - WALL, IX1 + WALL
OY0, OY1 = IY0 - WALL, IY1 + WALL
OZ0 = IZ0 - FRONT_T
OZ1 = IZ1 + BACK_T
if FOOT_LEN is None:
    # Table face length at which the wedge's back face reaches the top of the case
    _a = math.radians(TILT_DEG)
    FOOT_LEN = (OX1 - OX0) / (math.sin(_a) + math.cos(_a) / math.tan(_a))


def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


def rbox(x0, x1, y0, y1, z0, z1, r):
    return box(x0, x1, y0, y1, z0, z1).edges("|Z").fillet(r)


# ---------------- front shell ----------------
def front_shell():
    shell = rbox(OX0, OX1, OY0, OY1, OZ0, IZ1, CORNER_R).faces("<Z").edges().fillet(EDGE_R)
    shell = shell.cut(rbox(IX0, IX1, IY0, IY1, IZ0, IZ1 + 1, CORNER_R - WALL))

    # Screen window with a 45 degree chamfer on the outside
    ww, wh = VA_W + 0.3, VA_H + 0.3
    shell = shell.cut(rbox(-ww / 2, ww / 2, VA_CY - wh / 2, VA_CY + wh / 2, OZ0 - 1, IZ0 + 1, 1.5))
    ch = 1.0
    chamfer = (cq.Workplane("XY").workplane(offset=OZ0 - 0.01)
               .center(0, VA_CY).rect(ww + 2 * ch, wh + 2 * ch)
               .extrude(ch + 0.01, taper=45))
    shell = shell.cut(chamfer)

    # Flexure buttons: a U-slot frees a tongue hinged towards the screen,
    # thinned from the inside, with a nub that presses the tact switch.
    tongue_t = 0.9
    for bx, by in BUTTONS:
        tx0, tx1, ty0, ty1 = bx - 2.4, bx + 2.4, by - 4.0, by + 4.0
        slot = box(tx0 - 0.5, tx1 + 0.5, ty0 - 0.5, ty1, OZ0 - 1, IZ0 + 0.01).cut(
            box(tx0, tx1, ty0, ty1 + 1, OZ0 - 2, IZ0 + 1))
        shell = shell.cut(slot)
        shell = shell.cut(box(tx0, tx1, ty0, ty1, OZ0 + tongue_t, IZ0 + 0.01))
        nub = (cq.Workplane("XY").workplane(offset=OZ0 + tongue_t).center(bx, by)
               .circle(1.2).extrude(BUTTON_TOP_Z - 0.25 - (OZ0 + tongue_t)))
        shell = shell.union(nub)
        # Small dimple so the buttons can be found by touch
        shell = shell.cut(cq.Workplane("XY").workplane(offset=OZ0 - 0.01).center(bx, by)
                          .circle(1.6).extrude(0.35))

    # Microphone hole
    shell = shell.cut(cq.Workplane("XY").workplane(offset=OZ0 - 1).center(*MIC).circle(0.6).extrude(5))

    # USB-C (y+ wall), open towards the back so the board drops in
    shell = shell.cut(rbox(USB_X - 6.25, USB_X + 6.25, IY1 - 1, OY1 + 1, USB_Z - 3.6, IZ1 + 1, 0.01)
                      .edges("|Y").fillet(1.5))
    # microSD slot (y- wall)
    shell = shell.cut(box(-6.6, 5.9, OY0 - 1, IY0 + 1, 1.6, 3.8))
    # Power slide switch (x- wall = top), open towards the back
    shell = shell.cut(box(OX0 - 1, IX0 + 0.01, 15.3, 20.7, 1.0, IZ1 + 1))
    # Battery plug / cable (x+ wall = bottom), open towards the back
    shell = shell.cut(box(IX1 - 0.01, OX1 + 1, 10.4, 21.4, 2.6, IZ1 + 1))
    # Antenna exit (x- wall = top), open towards the back so the FPC/cable can be laid in
    ay0, ay1, adepth = ANTENNA_SLOT
    shell = shell.cut(box(OX0 - 1, IX0 + 0.01, ay0, ay1, IZ1 - adepth, IZ1 + 1))
    # Grooves the lid's snap ridges click into
    for axis, pos, lo, hi in SNAP_RIDGES if SNAP > 0 else []:
        shell = shell.cut(snap_bar(axis, pos, lo, hi, IZ1 - LIP_H / 2, outward=SNAP + 0.1, r=0.6))
    return shell


# ---------------- back cover ----------------
ANTENNA_SLOT = (24.0, 34.0, 3.0)    # y0, y1 and depth (from the rim) of the top-wall antenna exit
LID_NOTCHES = [
    box(IX0 - 1, IX0 + 3, 15.0, 21.0, IZ1 - LIP_H - 1, IZ1 + 0.01),     # slide switch
    box(IX0 - 1, IX0 + 3, ANTENNA_SLOT[0] - 0.3, ANTENNA_SLOT[1] + 0.3,
        IZ1 - LIP_H - 1, IZ1 + 0.01),                                     # antenna exit
    box(IX1 - 3, IX1 + 1, 10.1, 21.7, IZ1 - LIP_H - 1, IZ1 + 0.01),     # battery plug
    box(-7.5, 7.5, IY1 - 3, IY1 + 1, IZ1 - LIP_H - 1, IZ1 + 0.01),      # USB plug overmold
]
# The PCB's four 2.5 mm mounting holes (from the STEP model). Lid posts end in a
# pin that locates in the hole and a shoulder that holds the board down.
MOUNT_HOLES = [(sx * 10.0, y) for sx in (-1, 1) for y in (2.5, 25.0)]
PIN_D = 1.9
SHOULDER_D = 4.2
SHOULDER_GAP = 0.1     # shoulder stops this far above the PCB (absorbs tolerances)
PIN_TIP_Z = 0.15       # display module sits just below the PCB at y=25
# Snap ridges on the lid lip / grooves in the shell wall: (axis, wall position, from, to)
SNAP_RIDGES = [
    ("y", IY0, -8.0, 8.0),       # microSD end
    ("x", IX1, 26.0, 46.0),      # bottom (battery) side
    ("x", IX0, 37.0, 47.0),      # top side, clear of switch and antenna exit
]


def snap_bar(axis, pos, lo, hi, z, outward, r):
    """Horizontal rod along a wall that sticks `outward` mm out of the plane `pos`."""
    sign = -1 if pos in (IX0, IY0) else 1
    c = pos + sign * (outward - r)
    if axis == "x":   # rod runs along y on an x-wall
        return box(c - r, c + r, lo, hi, z - r, z + r).edges("|Y").fillet(r * 0.99)
    return box(lo, hi, c - r, c + r, z - r, z + r).edges("|X").fillet(r * 0.99)


def back_cover():
    lid = rbox(OX0, OX1, OY0, OY1, IZ1, OZ1, CORNER_R).faces(">Z").edges().fillet(EDGE_R)
    lx0, lx1, ly0, ly1 = IX0 + LID_FIT, IX1 - LID_FIT, IY0 + LID_FIT, IY1 - LID_FIT
    lip = rbox(lx0, lx1, ly0, ly1, IZ1 - LIP_H, IZ1 + 0.01, CORNER_R - WALL - LID_FIT).cut(
        rbox(lx0 + LIP_T, lx1 - LIP_T, ly0 + LIP_T, ly1 - LIP_T, IZ1 - LIP_H - 1, IZ1 + 1, 0.8))
    for n in LID_NOTCHES:
        lip = lip.cut(n)
    # Snap ridges (the lip sits LID_FIT inside the wall, so they protrude SNAP past the wall)
    for axis, pos, lo, hi in SNAP_RIDGES if SNAP > 0 else []:
        lip_face = pos + (LID_FIT if pos in (IX0, IY0) else -LID_FIT)
        ridge = snap_bar(axis, lip_face, lo, hi, IZ1 - LIP_H / 2, outward=SNAP + LID_FIT, r=0.5)
        lip = lip.union(ridge)
    lid = lid.union(lip)

    # Posts into the PCB's mounting holes: shoulder holds the board, pin locates it
    shoulder_z = PCB_T + SHOULDER_GAP
    for px, py in MOUNT_HOLES:
        post = (cq.Workplane("XY").workplane(offset=shoulder_z).center(px, py)
                .circle(SHOULDER_D / 2).extrude(IZ1 - shoulder_z + 0.01))
        pin = (cq.Workplane("XY").workplane(offset=PIN_TIP_Z).center(px, py)
               .circle(PIN_D / 2).extrude(shoulder_z - PIN_TIP_Z + 0.01)
               .faces("<Z").edges().chamfer(0.3))
        lid = lid.union(post).union(pin)

    lid = lid.union(wedge_foot())

    lid = lid.cut(wedge_text("hi, buddy"))
    return lid


# ---------------- placement into world coordinates ----------------
# World: X to the viewer's right, Y away from the viewer, Z up.
# Board -> world: X = -(y - y_mid), Y = z, Z = -x  (switch on top, USB on the left).
Y_MID = (OY0 + OY1) / 2


def _trsf(rows):
    t = gp_Trsf()
    (a, b, c, d), (e, f, g, h), (i, j, k, l) = rows
    t.SetValues(a, b, c, d, e, f, g, h, i, j, k, l)
    return t


TO_WORLD = _trsf([[0, -1, 0, Y_MID], [0, 0, 1, 0], [-1, 0, 0, 0]])
TO_BOARD = _trsf([[0, 0, -1, 0], [-1, 0, 0, Y_MID], [0, 1, 0, 0]])


def _apply(shape, trsf):
    return cq.Shape.cast(BRepBuilderAPI_Transform(shape.wrapped, trsf, True).Shape())


def to_world(shape):
    return _apply(shape, TO_WORLD)


def tilt(shape, deg=TILT_DEG):
    return shape.rotate(cq.Vector(0, 0, 0), cq.Vector(1, 0, 0), -deg)


def on_table(shape):
    """World pose of a board-frame part: leaning back, table face at Z=0."""
    return tilt(to_world(shape)).translate(cq.Vector(0, 0, -TABLE_Z))


# ---------------- wedge foot ----------------
# In the upright world frame (before tilting) the body spans depth Y in
# [OZ0, OZ1] and height Z in [-OX1, -OX0]. Tilted back by TILT_DEG it rests on
# its back-bottom edge B; the foot extends the table face from B backwards past
# the centre of mass so the buddy can't tip over.
def wedge_points():
    a = math.radians(TILT_DEG)
    # Table direction in the upright frame (perpendicular to "up" after tilting)
    ty, tz = math.cos(a), math.sin(a)
    by, bz = OZ1, -OX1                                  # back-bottom edge
    fy, fz = by + FOOT_LEN * ty, bz + FOOT_LEN * tz     # tip of the foot
    gz = fz + (fy - by) * math.tan(math.pi / 2 - a)     # back of the foot on the cover
    return (by, bz), (fy, fz), (by, gz)


def wedge_foot():
    (by, bz), (fy, fz), (_, gz) = wedge_points()
    half = (OY1 - OY0) / 2 + 1
    inset = 1.0  # sink into the cover so the union is solid
    prof = [(by - inset, bz + inset), (by, bz), (fy, fz), (by, gz), (by - inset, gz)]
    foot = (cq.Workplane("YZ").workplane(offset=-half)
            .polyline(prof).close().extrude(2 * half))
    foot = cq.Workplane().add(_apply(foot.val(), TO_BOARD))
    # Trim to the case outline so the wedge follows its rounded corners
    return foot.intersect(rbox(OX0, OX1, OY0, OY1, OZ1 - inset - 1, OZ1 + 40, CORNER_R))


def wedge_text(msg, size=5, depth=0.5):
    """Text engraved into the wedge's back face (vertical when on the table)."""
    _, (fy, fz), (gy, gz) = wedge_points()
    a = math.radians(TILT_DEG)
    n = (0, math.cos(a), math.sin(a))          # outward normal of the face, upright frame
    plane = cq.Plane(origin=(0, (fy + gy) / 2, (fz + gz) / 2), xDir=(-1, 0, 0), normal=n)
    text = cq.Workplane(plane).text(msg, size, -depth, kind="bold", halign="center", valign="center")
    return cq.Workplane().add(_apply(text.val(), TO_BOARD))


def _table_z():
    (by, bz), _, _ = wedge_points()
    a = math.radians(TILT_DEG)
    return -by * math.sin(a) + bz * math.cos(a)


TABLE_Z = _table_z()


# ---------------- outputs ----------------
def tri_mesh(shape, tol=0.1):
    verts, tris = shape.tessellate(tol, 0.3)
    return [[(v.x, v.y, v.z) for v in (verts[a], verts[b], verts[c])] for a, b, c in tris]


def render(parts, path, elev, azim, title):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    import numpy as np

    fig = plt.figure(figsize=(7, 5.5), dpi=120)
    ax = fig.add_subplot(111, projection="3d")
    light = np.array([0.4, -0.6, 0.8])
    light /= np.linalg.norm(light)
    allpts = []
    for shape, color in parts:
        tris = np.array(tri_mesh(shape))
        n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
        n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-9
        shade = 0.35 + 0.65 * np.clip(n @ light, 0, 1)
        base = np.array(matplotlib.colors.to_rgb(color))
        ax.add_collection3d(Poly3DCollection(tris, facecolors=shade[:, None] * base, linewidths=0))
        allpts.append(tris.reshape(-1, 3))
    pts = np.vstack(allpts)
    c, r = (pts.max(0) + pts.min(0)) / 2, (pts.max(0) - pts.min(0)).max() / 2
    ax.set_xlim(c[0] - r, c[0] + r); ax.set_ylim(c[1] - r, c[1] + r); ax.set_zlim(c[2] - r, c[2] + r)
    ax.set_box_aspect((1, 1, 1))
    ax.view_init(elev=elev, azim=azim)
    ax.set_axis_off()
    ax.set_title(title)
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def check(board_step, shell, lid):
    board = cq.importers.importStep(board_step).val()
    for name, part in (("front_shell", shell), ("back_cover", lid)):
        inter = part.val().intersect(board)
        vol = inter.Volume()
        print(f"  board vs {name}: overlap {vol:.3f} mm^3", "OK" if vol < 0.5 else "<-- COLLISION")
        if vol >= 0.5:
            bb = inter.BoundingBox()
            print(f"     at x[{bb.xmin:.2f},{bb.xmax:.2f}] y[{bb.ymin:.2f},{bb.ymax:.2f}] z[{bb.zmin:.2f},{bb.zmax:.2f}]")
    inter = shell.val().intersect(lid.val()).Volume()
    print(f"  front_shell vs back_cover: overlap {inter:.3f} mm^3", "OK" if inter < 0.05 else "<-- COLLISION")
    return board


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", help="board STEP file for collision check + preview")
    ap.add_argument("--no-render", action="store_true")
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)

    shell, lid = front_shell(), back_cover()
    print(f"body outer: {OX1-OX0:.1f} x {OY1-OY0:.1f} x {OZ1-OZ0:.1f} mm")

    # Print orientations: shell face-down; back cover standing on its table face.
    cq.exporters.export(shell, os.path.join(OUT, "front_shell.stl"), tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(cq.Workplane().add(on_table(lid.val())), os.path.join(OUT, "back_cover.stl"),
                        tolerance=0.02, angularTolerance=0.1)
    asm = cq.Assembly()
    asm.add(shell, name="front_shell").add(lid, name="back_cover")
    asm.save(os.path.join(OUT, "buddy_case.step"))

    board = check(args.check, shell, lid) if args.check else None

    if not args.no_render:
        parts = [(on_table(shell.val()), "#e8e4da"), (on_table(lid.val()), "#ff8a5c")]
        if board is not None:
            parts.append((on_table(board), "#2a6b3a"))
        render(parts, os.path.join(OUT, "preview_front.png"), 18, -70, "front")
        render(parts, os.path.join(OUT, "preview_back.png"), 22, 120, "back")
        render([(shell.val(), "#e8e4da")], os.path.join(OUT, "preview_shell_inside.png"), 60, 30, "front shell, inside")
        render(parts, os.path.join(OUT, "preview_side.png"), 0, 0, "side (table at Z=0)")
        render([(lid.val(), "#c9c3b5")], os.path.join(OUT, "preview_lid_inside.png"), -60, 30, "back cover, inside")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
