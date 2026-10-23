"""Print the named parts of the board's STEP model with their bounding boxes (mm)."""
import sys

from OCP.STEPCAFControl import STEPCAFControl_Reader
from OCP.TDocStd import TDocStd_Document
from OCP.TCollection import TCollection_ExtendedString
from OCP.XCAFDoc import XCAFDoc_DocumentTool
from OCP.TDF import TDF_LabelSequence
from OCP.TDataStd import TDataStd_Name
from OCP.Bnd import Bnd_Box
from OCP.BRepBndLib import BRepBndLib
from OCP.IFSelect import IFSelect_RetDone

doc = TDocStd_Document(TCollection_ExtendedString("doc"))
reader = STEPCAFControl_Reader()
reader.SetNameMode(True)
if reader.ReadFile(sys.argv[1]) != IFSelect_RetDone:
    sys.exit("read failed")
reader.Transfer(doc)
st = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())

def name_of(label):
    attr = TDataStd_Name()
    if label.FindAttribute(TDataStd_Name.GetID_s(), attr):
        return attr.Get().ToExtString()
    return "?"

rows = []
def walk(label, depth=0):
    shape = st.GetShape_s(label)
    nm = name_of(label)
    if st.IsReference_s(label):
        ref = label.__class__()
        st.GetReferredShape_s(label, ref)
        nm = nm + " -> " + name_of(ref)
    box = Bnd_Box()
    BRepBndLib.Add_s(shape, box, True)
    if not box.IsVoid():
        x0, y0, z0, x1, y1, z1 = box.Get()
        rows.append((depth, nm, x0, y0, z0, x1, y1, z1))
    comps = TDF_LabelSequence()
    st.GetComponents_s(label, comps)
    for i in range(1, comps.Length() + 1):
        walk(comps.Value(i), depth + 1)

free = TDF_LabelSequence()
st.GetFreeShapes(free)
for i in range(1, free.Length() + 1):
    walk(free.Value(i))

maxdepth = int(sys.argv[2]) if len(sys.argv) > 2 else 2
for d, nm, x0, y0, z0, x1, y1, z1 in rows:
    if d <= maxdepth:
        print(f"{'  '*d}{nm[:60]:60s} x[{x0:7.2f},{x1:7.2f}] y[{y0:7.2f},{y1:7.2f}] z[{z0:6.2f},{z1:6.2f}]  size {x1-x0:6.2f} x {y1-y0:6.2f} x {z1-z0:5.2f}")
