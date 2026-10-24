"""Offline export of Diana's actual default body/head/jacket bind-pose geometry.

Python 3.10 + numpy. Source game archives are not accessed by this script; first
extract the exact members with the separately audited pak_io.py research tool.
Only portable GLB, manifest and original skeleton evidence are written to output.
No Blender, native game code, format-reader package, textures or renderer runs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

import numpy as np

PARTS = [
    ("Body", "00/ch0100_00"),
    ("Head", "10/ch0100_10"),
    ("DefaultJacket", "40/ch0100_40_neo"),
]
PREFABS = {"Body":"ch05000_parts_body1.pfb.18", "Head":"ch05000_parts_head.pfb.18", "DefaultJacket":"ch05000_parts_cloth_neo.pfb.18"}


def span(data, offset, count, dtype):
    dtype = np.dtype(dtype)
    if offset < 0 or count < 0 or offset + count * dtype.itemsize > len(data):
        raise ValueError(f"Resource span outside file: {offset}+{count}*{dtype.itemsize}")
    return np.frombuffer(data, dtype=dtype, count=count, offset=offset)


def u32(data, offset):
    return int(span(data, offset, 1, "<u4")[0])


def u64(data, offset):
    return int(span(data, offset, 1, "<u8")[0])


def read_names(data):
    count = int(span(data, 20, 1, "<u2")[0])
    offsets = span(data, u64(data, 152), count, "<u8")
    names = []
    for offset in offsets:
        offset = int(offset)
        if not 0 <= offset < len(data):
            raise ValueError("Invalid mesh name pointer")
        end = data.index(0, offset)
        names.append(data[offset:end].decode("utf8"))
    return names


def read_elements(data, offset, count):
    if not 1 <= count <= 32:
        raise ValueError("Invalid vertex element count")
    span(data, offset, count*8, "u1")
    elements = {}
    for i in range(count):
        kind, stride, position = struct.unpack_from("<HHI", data, offset+i*8)
        if kind in elements:
            raise ValueError("Duplicate vertex element type")
        elements[kind] = {"stride": stride, "offset": position}
    return elements


def decode_buffer(source, base, vertex_size, end_size, elements):
    span(source, base, end_size, "u1")
    if 0 not in elements or elements[0]["stride"] != 12 or elements[0]["offset"] != 0:
        raise ValueError("Unsupported mesh position stream")
    following = sorted(e["offset"] for e in elements.values() if e["offset"] > 0)
    if not following or following[0] % 12:
        raise ValueError("Invalid position count")
    n = following[0] // 12
    if not 1 <= n <= 1000000:
        raise ValueError("Excessive mesh vertex count")
    attributes = {"POSITION": span(source, base, n*3, "<f4").reshape(-1,3).copy()}
    if not np.isfinite(attributes["POSITION"]).all() or np.max(np.abs(attributes["POSITION"])) > 100:
        raise ValueError("Invalid native position")
    if 1 in elements:
        e = elements[1]
        if e["stride"] != 8:
            raise ValueError("Unsupported tangent stream")
        raw = span(source, base+e["offset"], n*8, "i1").reshape(-1,8)
        normal = np.maximum(raw[:,:3].astype("<f4")/127.0, -1)
        tangent = np.maximum(raw[:,4:7].astype("<f4")/127.0, -1)
        normal /= np.maximum(np.linalg.norm(normal,axis=1,keepdims=True),1e-9)
        tangent -= normal * np.sum(normal*tangent,axis=1,keepdims=True)
        tangent /= np.maximum(np.linalg.norm(tangent,axis=1,keepdims=True),1e-9)
        attributes["NORMAL"] = normal.astype("<f4")
        attributes["TANGENT"] = np.column_stack((tangent, np.where(raw[:,7]<0,-1.0,1.0))).astype("<f4")
    else:
        raise ValueError("Actual normal/tangent stream required")
    for kind, name in ((2,"TEXCOORD_0"),(3,"TEXCOORD_1")):
        if kind in elements:
            e = elements[kind]
            if e["stride"] != 4:
                raise ValueError("Unsupported UV stream")
            uv = span(source,base+e["offset"],n*2,"<f2").reshape(-1,2).astype("<f4")
            if not np.isfinite(uv).all():
                raise ValueError("Nonfinite native UV")
            attributes[name] = uv
    if "TEXCOORD_0" not in attributes:
        raise ValueError("Missing actual primary UV")
    if 5 in elements:
        e = elements[5]
        if e["stride"] != 4:
            raise ValueError("Unsupported vertex color stream")
        attributes["COLOR_0"] = span(source,base+e["offset"],n*4,"u1").reshape(-1,4).astype("<f4")/255.0
    for e in elements.values():
        if e["offset"] + n*e["stride"] > vertex_size:
            raise ValueError("Vertex element exceeds its buffer")
    return {"attributes":attributes,"source":source,"faceBase":base+vertex_size,"faceEnd":base+end_size,"vertexCount":n,"elements":elements}


def read_mesh(path, streaming_path):
    data = path.read_bytes()
    if data[:4] != b"MESH" or u32(data,4) != 250707828 or u32(data,8) != len(data):
        raise ValueError(f"Unsupported retail Pragmata mesh: {path.name}")
    main, buffer = u64(data,48), u64(data,88)
    names = read_names(data)
    material_count = data[main+1]
    material_remap = span(data,u64(data,128),material_count,"<u2")
    if np.max(material_remap) >= len(names):
        raise ValueError("Invalid material name remap")
    materials = [names[int(i)] for i in material_remap]
    element_count = int(span(data,buffer+34,1,"<u2")[0])
    main_element_count = int(span(data,buffer+32,1,"<u2")[0])
    elements = read_elements(data,u64(data,buffer),element_count)
    vertex_size = u32(data,buffer+28)
    end_size = u32(data,buffer+52)
    buffers = [decode_buffer(data,u64(data,buffer+8),vertex_size,end_size,elements)]
    stream_info = u64(data,160)
    stream_count = u32(data,stream_info) if stream_info else 0
    if stream_count > 8:
        raise ValueError("Too many streaming chunks")
    streaming = None
    if stream_count:
        if not streaming_path.exists():
            raise ValueError(f"Missing required streaming companion {streaming_path}")
        streaming = streaming_path.read_bytes()
        info_entries = u64(data,stream_info+8)
        stream_elements = u64(data,buffer+80)
        padded_elements = ((main_element_count*8+15)//16)*16
        for i in range(stream_count):
            stream_start, stream_length = struct.unpack_from("<II",data,info_entries+i*8)
            stream_header = buffer+96+i*80
            stream_vertex_size = u32(data,stream_header+12)
            stream_end_size = u32(data,stream_header+36)
            if stream_end_size > stream_length:
                raise ValueError("Streaming descriptor exceeds its chunk")
            stream_table = read_elements(data,stream_elements+i*padded_elements,main_element_count)
            buffers.append(decode_buffer(streaming,stream_start,stream_vertex_size,stream_end_size,stream_table))
    # LOD0 is selected explicitly; the resident buffer can contain other LODs.
    lod = u64(data,main+64)
    if not 0 < data[lod] <= 128:
        raise ValueError("Invalid LOD0 mesh groups")
    index_type = "<u4" if data[main+6] else "<u2"
    submeshes = []
    for group_i in range(data[lod]):
        group = u64(data,lod+16+group_i*8)
        visibility_group = data[group]
        for sub_i in range(data[group+1]):
            sub = group+16+sub_i*32
            material, quad, vertex_buffer, _ = struct.unpack_from("<4B",data,sub)
            face_count, face_start, vertex_start = struct.unpack_from("<3I",data,sub+8)
            if quad or vertex_buffer >= len(buffers) or material >= len(materials) or face_count % 3:
                raise ValueError("Unsupported LOD0 primitive")
            buf = buffers[vertex_buffer]
            first_byte = buf["faceBase"] + face_start*np.dtype(index_type).itemsize
            if first_byte+face_count*np.dtype(index_type).itemsize > buf["faceEnd"]:
                raise ValueError("Mesh face range exceeds buffer")
            faces = span(buf["source"],first_byte,face_count,index_type).astype("<u4").reshape(-1,3)+vertex_start
            if faces.size == 0 or np.max(faces) >= buf["vertexCount"]:
                raise ValueError("Mesh face points outside vertex buffer")
            selected, inverse = np.unique(faces.ravel(),return_inverse=True)
            attributes = {k:v[selected].copy() for k,v in buf["attributes"].items()}
            submeshes.append({"attributes":attributes,"faces":inverse.astype("<u4").reshape(-1,3),"sourceVertexIndices":selected,"materialIndex":material,"materialName":materials[material],"visibilityGroup":visibility_group,"group":group_i,"submesh":sub_i,"vertexBuffer":vertex_buffer})
    # Retain complete original skeleton evidence without inventing influence
    # decoding for the retail auxiliary weight stream. GLB is static bind pose.
    skeleton_offset = u64(data,120)
    skeleton = None
    if skeleton_offset:
        count, remap_count = struct.unpack_from("<2I",data,skeleton_offset)
        if count > 4096 or remap_count > 4096:
            raise ValueError("Invalid skeleton counts")
        name_map = span(data,u64(data,136),count,"<u2")
        records_offset, local_offset, world_offset, inverse_offset = struct.unpack_from("<4Q",data,skeleton_offset+16)
        records = span(data,records_offset,count*8,"<i2").reshape(-1,8)
        local = span(data,local_offset,count*16,"<f4").reshape(-1,4,4)
        world = span(data,world_offset,count*16,"<f4").reshape(-1,4,4)
        inverse = span(data,inverse_offset,count*16,"<f4").reshape(-1,4,4)
        if np.max(name_map) >= len(names) or not np.isfinite(local).all() or not np.isfinite(world).all() or not np.isfinite(inverse).all():
            raise ValueError("Invalid original skeleton evidence")
        skeleton = {"boneCount":count,"weightRemapCount":remap_count,"matrixConvention":"source row-vector matrices, translation in fourth row, metres","bones":[{"name":names[int(name_map[i])],"index":int(records[i,0]),"parent":int(records[i,1]),"localMatrix":local[i].tolist(),"worldMatrix":world[i].tolist(),"inverseBindMatrix":inverse[i].tolist()} for i in range(count)]}
    return {"path":path,"data":data,"materials":materials,"submeshes":submeshes,"skeleton":skeleton,"streamingPath":streaming_path if streaming is not None else None,"streamingData":streaming}


class Glb:
    def __init__(self):
        self.payload = bytearray()
        self.document = {"asset":{"version":"2.0","generator":"OpenglLearn Diana original-geometry offline exporter"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"Diana_Default","children":[]}],"meshes":[],"materials":[],"bufferViews":[],"accessors":[],"buffers":[{}]}
    def array(self, values, kind, bounds=False):
        dtype = 5126 if values.dtype.kind == "f" else 5125
        values = values.astype("<f4" if dtype == 5126 else "<u4")
        while len(self.payload)%4:
            self.payload.append(0)
        view = {"buffer":0,"byteOffset":len(self.payload),"byteLength":values.nbytes}
        self.document["bufferViews"].append(view)
        self.payload.extend(values.tobytes())
        accessor = {"bufferView":len(self.document["bufferViews"])-1,"componentType":dtype,"count":len(values),"type":kind}
        if bounds:
            accessor.update(min=values.min(0).tolist(),max=values.max(0).tolist())
        self.document["accessors"].append(accessor)
        return len(self.document["accessors"])-1
    def save(self,path):
        self.document["buffers"][0]["byteLength"] = len(self.payload)
        js = json.dumps(self.document,ensure_ascii=False,separators=(",",":")).encode("utf8")
        js += b" "*(-len(js)%4)
        self.payload += b"\0"*(-len(self.payload)%4)
        result = struct.pack("<4sII",b"glTF",2,28+len(js)+len(self.payload))+struct.pack("<I4s",len(js),b"JSON")+js+struct.pack("<I4s",len(self.payload),b"BIN\0")+self.payload
        path.write_bytes(result)
        return hashlib.sha256(result).hexdigest()


def export(args):
    extracted = args.research_root/"extracted/natives/stm"
    out = args.output.resolve()
    out.mkdir(parents=True,exist_ok=True)
    writer = Glb()
    material_ids = {}
    manifest = {"asset":"diana.glb","unit":"metre","upAxis":"Y","pose":"actual source static bind pose","hair":"Strands supplied separately by ../HairSimulation/diana_lod0.hairbin; haircard20 deliberately excluded","uvConvention":"Original raw half-float UVs preserved without V inversion","parts":[],"materials":[],"limitations":["Original rest skeleton matrices preserved in skeleton-evidence.json; GLB mesh is static because full retail auxiliary influences were not reconstructed.","MDF shading parameters and original decoded textures are supplied separately by materials.json; no synthetic texture or lighting is embedded."]}
    skeletons = {"format":"Original Diana rest skeleton evidence","parts":[]}
    for label, resource in PARTS:
        uri = f"character/ch/ch01/ch0100/{resource}.mesh.251121828"
        mesh = read_mesh(extracted/uri,extracted/"streaming"/uri)
        primitives = []
        primitive_evidence = []
        for sub in mesh["submeshes"]:
            name = sub["materialName"]
            if name not in material_ids:
                material_ids[name] = len(writer.document["materials"])
                # Neutral factors are superseded by original MDF profile in Model.
                writer.document["materials"].append({"name":name,"pbrMetallicRoughness":{"baseColorFactor":[1,1,1,1],"metallicFactor":0,"roughnessFactor":1},"doubleSided":False,"extras":{"sourceMaterialName":name}})
                manifest["materials"].append({"index":material_ids[name],"name":name,"sourceMesh":uri})
            attributes = {key:writer.array(values,{2:"VEC2",3:"VEC3",4:"VEC4"}[values.shape[1]],key=="POSITION") for key,values in sub["attributes"].items()}
            faces = sub["faces"]
            extras = {k:sub[k] for k in ("materialIndex","materialName","visibilityGroup","group","submesh","vertexBuffer")}
            primitives.append({"attributes":attributes,"indices":writer.array(faces.ravel(),"SCALAR"),"material":material_ids[name],"mode":4,"extras":extras})
            position = sub["attributes"]["POSITION"]
            primitive_evidence.append(dict(extras,vertices=len(position),triangles=len(faces),boundsMin=position.min(0).tolist(),boundsMax=position.max(0).tolist(),attributes=list(attributes)))
        writer.document["nodes"][0]["children"].append(len(writer.document["nodes"]))
        writer.document["nodes"].append({"name":label,"mesh":len(writer.document["meshes"])})
        writer.document["meshes"].append({"name":label,"primitives":primitives,"extras":{"sourceResource":uri,"sourceMaterialSlots":mesh["materials"]}})
        prefab_uri = "character/prefab/ch05000/cospa0000/"+PREFABS[label]
        prefab_data = (extracted/prefab_uri).read_bytes()
        # Resource strings are independently recognizable even where an app
        # component later in an RSZ prefab has an unknown schema CRC.
        logical_mesh_uri = uri.removesuffix(".251121828")
        if logical_mesh_uri.encode("utf-16le") not in prefab_data.lower():
            raise ValueError("Selected default prefab does not name its mesh")
        part = {"name":label,"resource":"natives/stm/"+uri,"sourceSha256":hashlib.sha256(mesh["data"]).hexdigest(),"sourceBytes":len(mesh["data"]),"materialSlots":mesh["materials"],"primitives":primitive_evidence,"selectionPrefab":{"resource":"natives/stm/"+prefab_uri,"sha256":hashlib.sha256(prefab_data).hexdigest(),"meshReferencePresent":True},"materialResource":"natives/stm/character/ch/ch01/ch0100/"+resource+"_mat.mdf2.51"}
        if mesh["streamingData"] is not None:
            part["streaming"] = {"resource":"natives/stm/streaming/"+uri,"sha256":hashlib.sha256(mesh["streamingData"]).hexdigest(),"bytes":len(mesh["streamingData"])}
        manifest["parts"].append(part)
        skeletons["parts"].append({"name":label,"resource":part["resource"],"skeleton":mesh["skeleton"]})
    manifest["glbSha256"] = writer.save(out/"diana.glb")
    manifest["counts"] = {"parts":len(manifest["parts"]),"materials":len(material_ids),"primitives":sum(len(p["primitives"]) for p in manifest["parts"]),"vertices":sum(s["vertices"] for p in manifest["parts"] for s in p["primitives"]),"triangles":sum(s["triangles"] for p in manifest["parts"] for s in p["primitives"])}
    positions = np.concatenate([s["boundsMin"] for p in manifest["parts"] for s in p["primitives"]]).reshape(-1,3)
    upper = np.concatenate([s["boundsMax"] for p in manifest["parts"] for s in p["primitives"]]).reshape(-1,3)
    manifest["bounds"] = {"min":positions.min(0).tolist(),"max":upper.max(0).tolist()}
    (out/"geometry-manifest.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding="utf8")
    (out/"skeleton-evidence.json").write_text(json.dumps(skeletons,ensure_ascii=False,indent=2),encoding="utf8")
    evidence = args.research_root/"diana_full"
    evidence.mkdir(parents=True,exist_ok=True)
    (evidence/"geometry-export.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding="utf8")
    print(json.dumps({"output":str(out/"diana.glb"),"sha256":manifest["glbSha256"],"counts":manifest["counts"],"bounds":manifest["bounds"]},indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--research-root",type=Path,required=True)
    parser.add_argument("--output",type=Path,default=Path(__file__).resolve().parents[3]/"Asset/Diana")
    export(parser.parse_args())
