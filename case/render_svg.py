"""Hidden-line SVG views of the case parts (accurate, unlike the matplotlib previews)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cadquery as cq
import buddy_case as bc

out = bc.OUT
shell, lid = bc.front_shell(), bc.back_cover()
both = cq.Workplane().add(bc.on_table(shell.val())).add(bc.on_table(lid.val()))
views = {
    "svg_front": (both, (0.25, -1, 0.35)),
    "svg_back": (both, (-0.4, 1, 0.3)),
    "svg_shell_front": (shell, (0, 0, -1)),
    "svg_shell_inside": (shell, (0.3, 0.3, 1)),
    "svg_lid_inside": (lid, (0.3, 0.3, -1)),
}
for name, (wp, d) in views.items():
    comp = cq.Compound.makeCompound([v for v in wp.vals()])
    cq.exporters.export(cq.Workplane().add(comp), os.path.join(out, name + ".svg"),
                        opt={"width": 700, "height": 500, "projectionDir": d,
                             "showHidden": False, "strokeWidth": 0.25, "marginLeft": 20, "marginTop": 20})
print("ok")
