"""Read-only Blender -> glTF bridge. Run using Blender's bundled Python.

blender --background --factory-startup --disable-autoexec --python-exit-code 1
        --python blend_export.py -- --input character.blend --output-dir cache

The input is opened with scripts disabled and is never saved. The GLB is an
interchange copy; modifier / garment authoring evidence is retained separately.
"""

from __future__ import annotations

import argparse
from array import array
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys
import traceback

# Keep offline fitting from oversubscribing BLAS workers on the desktop.
# These settings affect only this Blender conversion process.
for _thread_setting in ("OPENBLAS_NUM_THREADS", "OMP_NUM_THREADS", "MKL_NUM_THREADS", "BLIS_NUM_THREADS"):
    os.environ[_thread_setting] = "2"


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--inspect-only", action="store_true")
    parser.add_argument("--static-pose", action="store_true",
                        help="Bake the saved source pose and default morphs for the static-model loader")
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])


def finite(value):
    number = float(value)
    return number if math.isfinite(number) else None


def values(owner, names):
    result = {}
    for name in names:
        if not hasattr(owner, name):
            continue
        value = getattr(owner, name)
        if isinstance(value, (bool, str, int)):
            result[name] = value
        elif isinstance(value, float):
            result[name] = finite(value)
    return result


def custom_tags(owner):
    # Only inert JSON scalars are retained. Drivers/scripts are not evaluated.
    result = {}
    for key in owner.keys():
        value = owner[key]
        if key != "_RNA_UI" and isinstance(value, (bool, str, int, float)):
            result[str(key)] = finite(value) if isinstance(value, float) else value
    return result


def mesh_metadata(obj):
    mesh = obj.data
    section_vertices = [set() for _ in obj.material_slots]
    polygon_counts = [0 for _ in obj.material_slots]
    for polygon in mesh.polygons:
        if polygon.material_index < len(section_vertices):
            section_vertices[polygon.material_index].update(polygon.vertices)
            polygon_counts[polygon.material_index] += 1
    group_statistics = [{"index": group.index, "name": group.name,
                         "weightedVertexCount": 0, "weightMinimum": None,
                         "weightMaximum": None} for group in obj.vertex_groups]
    section_groups = [{} for _ in section_vertices]
    vertex_sections = [[] for _ in mesh.vertices]
    for section, vertices in enumerate(section_vertices):
        for vertex in vertices:
            vertex_sections[vertex].append(section)
    for vertex in mesh.vertices:
        for weight in vertex.groups:
            if weight.group >= len(group_statistics) or weight.weight <= 0:
                continue
            item = group_statistics[weight.group]
            item["weightedVertexCount"] += 1
            item["weightMinimum"] = min(item["weightMinimum"] if item["weightMinimum"] is not None else weight.weight, weight.weight)
            item["weightMaximum"] = max(item["weightMaximum"] if item["weightMaximum"] is not None else weight.weight, weight.weight)
            for section in vertex_sections[vertex.index]:
                section_groups[section][weight.group] = section_groups[section].get(weight.group, 0) + 1
    sections = []
    for slot, material in enumerate(obj.material_slots):
        sections.append({"slot": slot, "name": material.material.name if material.material else "",
                         "polygonCount": polygon_counts[slot], "vertexCount": len(section_vertices[slot]),
                         "vertexGroups": [{"name": group_statistics[group]["name"], "weightedVertexCount": count}
                                          for group, count in sorted(section_groups[slot].items())]})
    keys = []
    if mesh.shape_keys:
        for key in mesh.shape_keys.key_blocks:
            keys.append({"name": key.name, "relativeKey": key.relative_key.name if key.relative_key else "",
                         "value": finite(key.value), "minimum": finite(key.slider_min), "maximum": finite(key.slider_max)})
    return {"vertices": len(mesh.vertices), "polygons": len(mesh.polygons),
            "vertexGroups": group_statistics, "materials": sections, "shapeKeys": keys,
            "uvLayers": [layer.name for layer in mesh.uv_layers]}


def modifier_metadata(modifier):
    result = {"name": modifier.name, "type": modifier.type,
              "showViewport": modifier.show_viewport, "showRender": modifier.show_render}
    if modifier.type == "ARMATURE":
        result["object"] = modifier.object.name if modifier.object else ""
        result.update(values(modifier, ["use_vertex_groups", "use_bone_envelopes", "use_deform_preserve_volume", "vertex_group"]))
    if modifier.type == "CLOTH":
        result["settings"] = values(modifier.settings, ["quality", "mass", "air_damping", "tension_stiffness", "compression_stiffness", "shear_stiffness", "bending_stiffness", "pin_stiffness", "vertex_group_mass", "vertex_group_structural_stiffness", "vertex_group_shear_stiffness", "vertex_group_bending", "use_sewing_springs", "use_dynamic_mesh"])
        result["collisionSettings"] = values(modifier.collision_settings, ["use_collision", "distance_min", "friction", "collision_quality", "use_self_collision", "self_distance_min", "self_friction", "vertex_group_self_collisions"])
        result["pointCache"] = values(modifier.point_cache, ["frame_start", "frame_end", "is_baked", "use_disk_cache"])
    return result


def inspect_source(bpy, source):
    objects = []
    for index, obj in enumerate(sorted(bpy.data.objects, key=lambda value: value.name)):
        item = {"id": "object-" + str(index), "name": obj.name,
                "type": obj.type, "dataName": obj.data.name if obj.data else "",
                "parent": obj.parent.name if obj.parent else "",
                "collections": sorted(collection.name for collection in obj.users_collection),
                "hideRender": obj.hide_render, "hideViewport": obj.hide_viewport,
                "inActiveScene": obj.name in bpy.context.scene.objects,
                "visibleInViewLayer": obj.visible_get() if obj.name in bpy.context.view_layer.objects else False,
                "matrixWorld": [[finite(value) for value in row] for row in obj.matrix_world],
                "customProperties": custom_tags(obj),
                "modifiers": [modifier_metadata(modifier) for modifier in obj.modifiers],
                "export": {"nodes": []}}
        if obj.type == "MESH":
            item.update(mesh_metadata(obj))
        elif obj.type == "ARMATURE":
            item["bones"] = [{"name": bone.name, "parent": bone.parent.name if bone.parent else "",
                               "deform": bone.use_deform} for bone in obj.data.bones]
        objects.append(item)
    images = []
    for image in bpy.data.images:
        if image.type in {"RENDER_RESULT", "COMPOSITING"}:
            continue
        path = bpy.path.abspath(image.filepath, library=image.library) if image.filepath else ""
        images.append({"name": image.name, "source": image.source, "path": path,
                       "packed": bool(image.packed_file), "width": image.size[0], "height": image.size[1],
                       "exists": bool(path and Path(path).is_file()), "colorspace": image.colorspace_settings.name})
    # Capture evaluated joints before any mesh copy, modifier switch or export
    # operator can change dependency-graph state. This is an authored preview
    # pose, kept independently of the skeleton's original rest/bind matrices.
    from mathutils import Matrix
    yup = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))
    depsgraph = bpy.context.evaluated_depsgraph_get()
    pose_bones = []
    for obj in sorted(bpy.context.scene.objects, key=lambda value: value.name):
        if obj.type != "ARMATURE":
            continue
        evaluated = obj.evaluated_get(depsgraph)
        for bone in evaluated.pose.bones:
            world = yup @ evaluated.matrix_world @ bone.matrix
            pose_bones.append({"name": bone.name, "armature": obj.name,
                               "matrix": [[finite(value) for value in row] for row in world]})
    morph_defaults = [{"sourceObject": item["name"], "name": key["name"], "value": key["value"]}
                      for item in objects if item["type"] == "MESH"
                      for key in item.get("shapeKeys", [])[1:]]
    return {"assetType": "BlenderSourceMetadata", "version": 1,
            "sourcePath": str(source), "sourceSha256": hashlib.sha256(source.read_bytes()).hexdigest(),
            "blenderVersion": bpy.app.version_string,
            "scene": {"name": bpy.context.scene.name, "frameStart": bpy.context.scene.frame_start,
                      "frameEnd": bpy.context.scene.frame_end, "framesPerSecond": finite(bpy.context.scene.render.fps / bpy.context.scene.render.fps_base),
                      "unitScale": finite(bpy.context.scene.unit_settings.scale_length)},
            "objects": objects,
            "materials": [{"name": material.name, "usesNodes": material.use_nodes,
                           "diffuseColor": [finite(value) for value in material.diffuse_color],
                           "customProperties": custom_tags(material)} for material in bpy.data.materials],
            "sourcePose": {"name": "SourcePose", "frame": bpy.context.scene.frame_current,
                           "framesPerSecond": finite(bpy.context.scene.render.fps / bpy.context.scene.render.fps_base),
                           "coordinateSystem": "glTF right-handed Y-up (x,z,-y)",
                           "boneGlobals": pose_bones, "morphDefaults": morph_defaults},
            "images": images, "warnings": []}


def mesh_topology(mesh):
    connectivity = array("i", [0]) * len(mesh.loops)
    mesh.loops.foreach_get("vertex_index", connectivity)
    return len(mesh.vertices), len(mesh.polygons), connectivity.tobytes()


def mesh_coordinates(mesh):
    coordinates = array("f", [0.0]) * (len(mesh.vertices) * 3)
    mesh.vertices.foreach_get("co", coordinates)
    return coordinates


def surface_reference_domain(mesh):
    """Select the source surface that a triangle renderer can display.

    Mesh datablocks may also contain loose authoring points. glTF triangle
    primitives do not include them, so they are recorded as excluded source
    data rather than becoming nonexistent surface samples in validation.
    The full coordinates are still used by the fitting calculation.
    """
    import numpy as np

    referenced = set()
    for polygon in mesh.polygons:
        referenced.update(polygon.vertices)
    evaluated_count = len(mesh.vertices)
    excluded_count = evaluated_count - len(referenced)
    excluded_sample = []
    for index in range(evaluated_count):
        if index not in referenced:
            excluded_sample.append(index)
            if len(excluded_sample) >= 4096:
                break
    return np.fromiter(sorted(referenced), dtype=np.intp, count=len(referenced)), {
        "vertexDomain": "Vertices referenced by source evaluated polygons",
        "evaluatedVertexCount": evaluated_count,
        "excludedLooseVertexCount": excluded_count,
        "excludedLooseVertexIndices": excluded_sample,
        "excludedLooseVertexIndicesTruncated": excluded_count > len(excluded_sample)}


def evaluated_mesh(bpy, obj):
    bpy.context.view_layer.update()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    return bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph),
                                         preserve_all_data_layers=True, depsgraph=depsgraph)


def skin_fit_matrices(bpy, obj, mesh, armature):
    """Full normalized Blender deform skin, in original Blender world space.

    The glTF exporter ignores weights <= 0.0001. Match that rule here and
    remove non-deform groups before export; exporting the entire joint tree
    otherwise makes glTF accidentally use those authoring-only groups.
    NumPy batches the matrix arithmetic rather than doing millions of Python
    matrix products for subdivided garments.
    """
    import numpy as np

    count = len(mesh.vertices)
    matrices = np.zeros((count, 4, 4), dtype=np.float64)
    totals = np.zeros(count, dtype=np.float64)
    eligible = {group.index: armature.data.bones.get(group.name)
                for group in obj.vertex_groups}
    eligible = {index: bone for index, bone in eligible.items() if bone and bone.use_deform}
    weights_by_group = {index: ([], []) for index in eligible}
    maximum = excess = 0
    four_discarded = 0.0
    for vertex in mesh.vertices:
        weights = []
        for item in vertex.groups:
            if item.group not in eligible or item.weight <= 0.0001:
                continue
            indices, values = weights_by_group[item.group]
            indices.append(vertex.index)
            values.append(item.weight)
            weights.append(item.weight)
        maximum = max(maximum, len(weights))
        if len(weights) > 4:
            excess += 1
            weights.sort(reverse=True)
            four_discarded = max(four_discarded, sum(weights[4:]) / sum(weights))
    evaluated_armature = armature.evaluated_get(bpy.context.evaluated_depsgraph_get())
    arm_world = np.asarray(evaluated_armature.matrix_world, dtype=np.float64)
    arm_inverse = np.linalg.inv(arm_world)
    for group_index in list(weights_by_group):
        indices, weights = weights_by_group.pop(group_index)
        if not indices:
            continue
        bone = eligible[group_index]
        deform = (arm_world @ np.asarray(evaluated_armature.pose.bones[bone.name].matrix, dtype=np.float64)
                  @ np.linalg.inv(np.asarray(bone.matrix_local, dtype=np.float64)) @ arm_inverse)
        indices = np.asarray(indices, dtype=np.intp)
        weights = np.asarray(weights, dtype=np.float64)
        matrices[indices] += weights[:, None, None] * deform
        totals[indices] += weights
    weighted = totals > 0
    matrices[weighted] /= totals[weighted, None, None]
    matrices[~weighted] = np.eye(4, dtype=np.float64)
    inverse = np.empty_like(matrices)
    # Limit transient LAPACK arrays even when a source has a very large mesh.
    for start in range(0, count, 32768):
        stop = min(count, start + 32768)
        determinants = np.linalg.det(matrices[start:stop, :3, :3])
        if not np.all(np.isfinite(determinants)) or np.any(np.abs(determinants) < 1e-10):
            raise ValueError("Source pose has a singular blended skin matrix; cannot preserve garment fit: " + obj.name)
        inverse[start:stop] = np.linalg.inv(matrices[start:stop])
    return matrices, inverse, {"maximumSourceInfluences": maximum,
        "verticesAboveFourInfluences": excess,
        "fourInfluenceDiscardedWeightFraction": four_discarded,
        "maximumDiscardedWeightFraction": 0.0,
        "exportedInfluenceLimit": maximum, "exportedAllInfluences": True,
        "ignoredWeightThreshold": 0.0001}


def fit_source_pose(obj, rest_mesh, posed_mesh, matrices, inverse):
    """Pull an original post-modifier surface into the unchanged bind frame."""
    import numpy as np

    local_reference = np.frombuffer(mesh_coordinates(posed_mesh), dtype=np.float32).reshape(-1, 3)
    rest_local = np.frombuffer(mesh_coordinates(rest_mesh), dtype=np.float32).reshape(-1, 3)
    world = np.asarray(obj.matrix_world, dtype=np.float64)
    world_inverse = np.linalg.inv(world)
    reference = local_reference @ world[:3, :3].T + world[:3, 3]
    if inverse is None:
        corrected_world = reference
    else:
        corrected_world = np.einsum("nij,nj->ni", inverse[:, :3, :3], reference) + inverse[:, :3, 3]
    corrected = (corrected_world @ world_inverse[:3, :3].T + world_inverse[:3, 3]).astype(np.float32)
    # Evaluate the residual after float32 storage, which is the actual glTF
    # vertex precision, rather than reporting an artificial double-only zero.
    cooked_world = corrected @ world[:3, :3].T + world[:3, 3]
    reconstructed = cooked_world if matrices is None else (
        np.einsum("nij,nj->ni", matrices[:, :3, :3], cooked_world) + matrices[:, :3, 3])
    residual = np.linalg.norm(reconstructed - reference, axis=1)
    rest_world = rest_local @ world[:3, :3].T + world[:3, 3]
    correction = np.linalg.norm(cooked_world - rest_world, axis=1)
    extent = float(np.linalg.norm(np.ptp(reference, axis=0))) if len(reference) else 0.0
    if not np.all(np.isfinite(corrected)) or (len(residual) and float(np.max(residual)) > max(1e-5, extent * 1e-5)):
        raise ValueError("Source-pose fitting exceeds the position tolerance: " + obj.name)
    rest_mesh.vertices.foreach_set("co", corrected.reshape(-1))
    rest_mesh.update()
    # Blender Z-up world -> glTF Y-up world: x,z,-y.
    engine_reference = reference[:, [0, 2, 1]].copy()
    engine_reference[:, 2] *= -1
    return engine_reference.astype("<f4"), {
        "maximumResidual": float(np.max(residual)) if len(residual) else 0.0,
        "meanResidual": float(np.mean(residual)) if len(residual) else 0.0,
        "maximumBindCorrection": float(np.max(correction)) if len(correction) else 0.0,
        "meanBindCorrection": float(np.mean(correction)) if len(correction) else 0.0}


def prepare_export_geometry(bpy, metadata, destination):
    """Bake supported rest topology identically for Basis and every shape key.

    Blender's ordinary apply-modifiers path drops shape keys. Re-evaluating
    the same topology for every key preserves the complete mirror geometry
    and authored morph deltas. Inverse skinning the *original ordered posed
    surface* also preserves garment fitting when subdivision/solidify follows
    the armature. Original rest bones and their bind matrices remain intact.
    """
    selected = []
    allowed = {"MIRROR", "SUBSURF", "SOLIDIFY", "TRIANGULATE", "EDGE_SPLIT", "WEIGHTED_NORMAL", "NORMAL_EDIT"}
    reference_path = destination / "source_pose_reference.bin"
    reference_stream = reference_path.open("xb")
    metadata["sourcePoseReference"] = {"file": reference_path.name, "componentType": "float32",
        "byteOrder": "little", "coordinateSystem": "glTF right-handed Y-up (x,z,-y)",
        "vertexDomain": "Vertices referenced by source evaluated polygons"}
    for obj in bpy.context.view_layer.objects:
        obj.select_set(False)
    for index, item in enumerate(metadata["objects"]):
        if item["type"] != "MESH":
            continue
        obj = bpy.data.objects[item["name"]]
        if not item["inActiveScene"] or not item["visibleInViewLayer"] or item["hideRender"]:
            item["export"]["skippedReason"] = "Mesh is hidden or belongs to another source scene/view layer"
            continue
        original = obj.data
        obj.data = original.copy()
        active_armatures = [modifier for modifier in obj.modifiers
                            if modifier.type == "ARMATURE" and modifier.show_render and modifier.object]
        if len(active_armatures) > 1:
            raise ValueError("Multiple armature modifiers cannot be represented by one engine skin: " + obj.name)
        live_armature = active_armatures[0] if active_armatures else None
        armature = live_armature.object if live_armature else None
        if live_armature and (live_armature.use_deform_preserve_volume or live_armature.use_bone_envelopes
                              or live_armature.vertex_group or not live_armature.use_vertex_groups):
            raise ValueError("This armature uses dual-quaternion, envelope or masked skinning; convert it to unmasked vertex-group LBS before import: " + obj.name)
        ignored_groups = []
        if armature:
            # Keeping all bone nodes is necessary for retargeting and source
            # constraints. Non-deform groups must nevertheless never become
            # glTF influences merely because their bone names are exported.
            for group in list(obj.vertex_groups):
                bone = armature.data.bones.get(group.name)
                if bone and not bone.use_deform:
                    ignored_groups.append(group.name)
                    obj.vertex_groups.remove(group)
        item["export"]["ignoredNonDeformGroups"] = ignored_groups
        snapshot = []
        relative = True
        action = None
        if obj.data.shape_keys:
            keys = obj.data.shape_keys
            relative = keys.use_relative
            if not relative:
                raise ValueError("Absolute-time shape keys require author-side conversion to relative keys: " + obj.name)
            if keys.animation_data:
                action = keys.animation_data.action
                if keys.animation_data.drivers:
                    metadata["warnings"].append("Shape-key drivers are not executed or transferred; named morph targets remain available: " + obj.name)
            for key in keys.key_blocks:
                coordinates = array("f", [0.0]) * (len(key.data) * 3)
                key.data.foreach_get("co", coordinates)
                snapshot.append({"name": key.name, "coordinates": coordinates, "value": key.value,
                                 "minimum": key.slider_min, "maximum": key.slider_max,
                                 "relative": key.relative_key.name if key.relative_key else ""})
            obj.shape_key_clear()
        topology = []
        for modifier in obj.modifiers:
            enabled = modifier.show_render
            outline = modifier.type == "SOLIDIFY" and "outline" in modifier.name.lower()
            modifier.show_viewport = enabled and (modifier.type in allowed or modifier == live_armature) and not outline
            modifier.show_render = modifier.show_viewport
            if modifier.show_viewport and modifier.type != "ARMATURE":
                topology.append({"name": modifier.name, "type": modifier.type})
            elif enabled and modifier.type != "ARMATURE" and not outline:
                metadata["warnings"].append("Unsupported rest modifier is retained only as authoring metadata: " + obj.name + " / " + modifier.name + " / " + modifier.type)
        item["export"]["bakedModifiers"] = topology
        item["export"]["omittedOutline"] = any(mod.type == "SOLIDIFY" and "outline" in mod.name.lower() for mod in obj.modifiers)
        base_mesh = None
        evaluated_shapes = []
        base_topology = None
        matrices = inverse = None
        skinning = None
        fit_reports = []
        inputs = snapshot or [{"name": "Basis", "coordinates": None}]
        for shape in inputs:
            if shape["coordinates"] is not None:
                obj.data.vertices.foreach_set("co", shape["coordinates"])
                obj.data.update()
            if live_armature:
                live_armature.show_viewport = live_armature.show_render = True
            posed = evaluated_mesh(bpy, obj)
            if live_armature:
                live_armature.show_viewport = live_armature.show_render = False
            evaluated = evaluated_mesh(bpy, obj)
            topology_signature = mesh_topology(evaluated)
            if topology_signature != mesh_topology(posed):
                bpy.data.meshes.remove(posed)
                bpy.data.meshes.remove(evaluated)
                raise ValueError("The original posed modifier stack changes rest topology; cannot preserve a skeletal garment safely: " + obj.name)
            if base_topology is not None and topology_signature != base_topology:
                bpy.data.meshes.remove(posed)
                bpy.data.meshes.remove(evaluated)
                raise ValueError("A rest modifier changes topology across shape keys; cannot safely preserve morphs: " + obj.name + " / " + shape["name"])
            if base_mesh is None and armature:
                matrices, inverse, skinning = skin_fit_matrices(bpy, obj, evaluated, armature)
            reference, fit = fit_source_pose(obj, evaluated, posed, matrices, inverse)
            if base_mesh is None:
                reference_indices, reference_domain = surface_reference_domain(posed)
            bpy.data.meshes.remove(posed)
            fit_reports.append({"shape": shape["name"], **fit})
            if base_mesh is None:
                base_mesh = evaluated
                base_topology = topology_signature
                surface_reference = reference[reference_indices]
                item["export"]["sourceReference"] = {"file": reference_path.name,
                    "byteOffset": reference_stream.tell(), "vertexCount": len(surface_reference),
                    "componentType": "float32", "byteOrder": "little",
                    "coordinateSystem": "glTF right-handed Y-up (x,z,-y)", "shape": "Basis",
                    **reference_domain}
                reference_stream.write(surface_reference.tobytes(order="C"))
            else:
                evaluated_shapes.append(mesh_coordinates(evaluated))
                bpy.data.meshes.remove(evaluated)
        item["export"]["sourceFit"] = {"method": "Inverse full deform skin of the original ordered source-pose modifier surface",
            "maximumResidual": max((fit["maximumResidual"] for fit in fit_reports), default=0.0),
            "maximumBindCorrection": max((fit["maximumBindCorrection"] for fit in fit_reports), default=0.0),
            "shapes": fit_reports}
        obj.data = base_mesh
        base_mesh.name = "codexMesh_" + format(index, "06d")
        item["export"]["meshToken"] = base_mesh.name
        # Remove evaluated modifiers, retain only live skinning with its exact
        # object reference and original vertex-group names/weights.
        for modifier in list(obj.modifiers):
            if modifier.type == "ARMATURE":
                source_modifier = next((value for value in item["modifiers"] if value["name"] == modifier.name), None)
                modifier.show_viewport = source_modifier["showViewport"] if source_modifier else True
                modifier.show_render = source_modifier["showRender"] if source_modifier else True
            else:
                obj.modifiers.remove(modifier)
        if snapshot:
            for shape_index, shape in enumerate(snapshot):
                key = obj.shape_key_add(name=shape["name"], from_mix=False)
                if shape_index:
                    key.data.foreach_set("co", evaluated_shapes[shape_index - 1])
                key.slider_min = shape["minimum"]
                key.slider_max = shape["maximum"]
                key.value = shape["value"] if shape_index else 0
            for shape in snapshot:
                key = obj.data.shape_keys.key_blocks[shape["name"]]
                if shape["relative"] in obj.data.shape_keys.key_blocks:
                    key.relative_key = obj.data.shape_keys.key_blocks[shape["relative"]]
            if action:
                obj.data.shape_keys.animation_data_create()
                obj.data.shape_keys.animation_data.action = action
        item["export"]["bakedVertices"] = len(obj.data.vertices)
        item["export"]["bakedPolygons"] = len(obj.data.polygons)
        if skinning:
            item["export"]["skinning"] = skinning
        obj.select_set(True)
        selected.append(obj)
        print("BLEND_MESH_READY " + obj.name + " " + str(len(obj.data.vertices)) + " vertices", flush=True)
    reference_stream.close()
    if not selected:
        raise ValueError("The active source scene contains no visible renderable mesh objects")
    for obj in selected:
        armature = obj.find_armature()
        if armature:
            armature.hide_set(False)
            armature.select_set(True)
    # Assimp's skeleton channel lookup is name based. Blender permits an
    # object named "body" and a bone named "body", whereas glTF carries both
    # as scene nodes. Namespace exported mesh nodes so an influence can never
    # accidentally resolve to a mesh object rather than its intended joint.
    bone_names = {bone.name for armature in bpy.data.armatures for bone in armature.bones}
    selected_pointers = {obj.as_pointer() for obj in selected}
    for index, item in enumerate(metadata["objects"]):
        if item["type"] != "MESH":
            continue
        obj = bpy.data.objects.get(item["name"])
        if obj is None or obj.as_pointer() not in selected_pointers:
            continue
        token = "codexObject_" + format(index, "06d")
        while token in bone_names or (token in bpy.data.objects and bpy.data.objects[token] != obj):
            token += "_mesh"
        obj.name = token
        item["export"]["exportNodeName"] = obj.name
    metadata["exportSelection"] = "Visible, renderable meshes in the active source scene, with their armatures"


def bake_static_source_pose(bpy, metadata):
    """Freeze the visible source-pose surface, including unskinned morphs.

    Exporter's apply-modifiers mode can use raw mesh data when an object has
    no modifiers. Explicit evaluation also preserves default shape keys on
    those objects when static export disables glTF morph targets.
    """
    for item in metadata["objects"]:
        token = item.get("export", {}).get("exportNodeName")
        if item["type"] != "MESH" or not token:
            continue
        obj = bpy.data.objects[token]
        # The fitted carrier uses the glTF weight threshold. Match it when
        # Blender evaluates the final static skin as well; otherwise tiny
        # source tails would be reintroduced after the fitting correction.
        tiny_weights = {}
        for vertex in obj.data.vertices:
            for group in vertex.groups:
                if 0 < group.weight <= 0.0001:
                    tiny_weights.setdefault(group.group, []).append(vertex.index)
        for group_index, vertices in tiny_weights.items():
            obj.vertex_groups[group_index].remove(vertices)
        for modifier in obj.modifiers:
            modifier.show_viewport = modifier.show_render
        evaluated = evaluated_mesh(bpy, obj)
        original = obj.data
        mesh_name = original.name
        obj.data = evaluated
        for modifier in list(obj.modifiers):
            obj.modifiers.remove(modifier)
        if original.users == 0:
            bpy.data.meshes.remove(original)
        evaluated.name = mesh_name
        item["export"]["staticPoseBaked"] = True
    metadata["staticPoseBaked"] = True
    metadata["warnings"].append("Static-model geometry includes the saved source pose and default morph values; skeleton animation remains available through the separate skeletal import path.")


def approximate_materials(bpy, metadata):
    """Project unsupported toon graphs to an unlit albedo + PBR carrier.

    Lighting ramps are intentionally left to the engine's toon renderer; image
    albedo and explicit RGB constants are traced from the active output.
    """
    ignored_images = set()

    def rgba(socket):
        value = getattr(socket, "default_value", None)
        if value is not None and hasattr(value, "__len__") and len(value) == 4:
            return tuple(float(component) for component in value)
        return (1.0, 1.0, 1.0, 1.0)

    def desc(socket, seen=None):
        seen = set() if seen is None else set(seen)
        if not socket.is_linked:
            return {"color": rgba(socket), "image": None, "score": 0}
        node = socket.links[0].from_node
        if node.as_pointer() in seen:
            return {"color": (1, 1, 1, 1), "image": None, "score": -10}
        seen.add(node.as_pointer())
        if node.type == "TEX_IMAGE" and node.image and (node.image.packed_file or node.image.has_data):
            if any(word in node.image.name.lower() for word in ("toon", "shadow", "normal", "rough", "metal", "mask", "occlusion")):
                # These inputs describe shading/control, not surface albedo.
                # An explicit RGB or even neutral white is preferable to
                # displaying a black/white light mask as a garment texture.
                ignored_images.add(node.image.name)
                return {"color": (1, 1, 1, 1), "image": None, "score": -3}
            return {"color": (1, 1, 1, 1), "image": node.image, "imageNode": node, "score": 10}
        if node.type == "RGB":
            return {"color": rgba(node.outputs[0]), "image": None, "score": 2}
        if node.type == "REROUTE":
            return desc(node.inputs[0], seen)
        if node.type in {"BSDF_PRINCIPLED", "BSDF_DIFFUSE", "DIFFUSE_BSDF", "EMISSION"}:
            color = node.inputs.get("Base Color") or node.inputs.get("Color")
            return desc(color, seen) if color else {"color": (1, 1, 1, 1), "image": None, "score": 0}
        if node.type == "SHADERTORGB":
            # A toon diffuse shader can itself carry the albedo texture. Drop
            # its lighting response while retaining that shader's Color input.
            return desc(node.inputs[0], seen)
        if node.type == "BSDF_TRANSPARENT":
            return {"color": (1, 1, 1, 1), "image": None, "score": -4}
        if node.type == "VALTORGB":
            return {"color": tuple(node.color_ramp.elements[-1].color), "image": None, "score": -2}
        inputs = [value for value in node.inputs if value.enabled and (value.type in {"RGBA", "SHADER"} or value.is_linked)]
        candidates = [desc(value, seen) for value in inputs]
        if not candidates:
            return {"color": (1, 1, 1, 1), "image": None, "score": -5}
        best = max(candidates, key=lambda value: value["score"])
        blend = getattr(node, "blend_type", "")
        # Multiplication by an authored RGB constant can be represented exactly
        # by a glTF base-color factor, including black cloth tints.
        if blend == "MULTIPLY" and best["image"]:
            constants = [value for value in candidates if value["image"] is None and value["score"] >= 0]
            if constants:
                tint = max(constants, key=lambda value: value["score"])["color"]
                best = dict(best)
                best["color"] = tuple(a * b for a, b in zip(best["color"], tint))
        return best

    for material in bpy.data.materials:
        if not material.use_nodes or not material.node_tree:
            continue
        nodes = material.node_tree.nodes
        output = next((node for node in nodes if node.type == "OUTPUT_MATERIAL" and node.is_active_output), None)
        if output is None or not output.inputs["Surface"].is_linked:
            continue
        source_node = output.inputs["Surface"].links[0].from_node
        # Ordinary supported PBR graphs already have an authoritative glTF path.
        if source_node.type == "BSDF_PRINCIPLED":
            continue
        ignored_images.clear()
        result = desc(output.inputs["Surface"])
        color = tuple(max(0.0, min(1.0, component)) for component in result["color"])
        image = result["image"]
        image_node = result.get("imageNode")
        image_uv = None
        mapping_values = None
        if image_node and image_node.inputs["Vector"].is_linked:
            vector_node = image_node.inputs["Vector"].links[0].from_node
            if vector_node.type == "MAPPING":
                mapping_values = {name: tuple(vector_node.inputs[name].default_value) for name in ("Location", "Rotation", "Scale")}
                if vector_node.inputs["Vector"].is_linked:
                    vector_node = vector_node.inputs["Vector"].links[0].from_node
            if vector_node.type == "UVMAP":
                image_uv = vector_node.uv_map
        transparent = any(node.type == "BSDF_TRANSPARENT" for node in nodes)
        for item in metadata["materials"]:
            if item["name"] == material.name:
                item["interchangeApproximation"] = {"method": "Connected albedo image / explicit RGB to Principled carrier; engine handles toon lighting",
                    "image": image.name if image else "", "baseColor": list(color), "alphaFromImage": transparent and image is not None,
                    "ignoredShadingImages": sorted(ignored_images)}
                break
        nodes.clear()
        output = nodes.new("ShaderNodeOutputMaterial")
        principled = nodes.new("ShaderNodeBsdfPrincipled")
        principled.inputs["Base Color"].default_value = color
        principled.inputs["Metallic"].default_value = 0
        principled.inputs["Roughness"].default_value = 0.8
        material.node_tree.links.new(principled.outputs["BSDF"], output.inputs["Surface"])
        if image:
            texture = nodes.new("ShaderNodeTexImage")
            texture.image = image
            if image_uv:
                uv = nodes.new("ShaderNodeUVMap")
                uv.uv_map = image_uv
            else:
                uv = nodes.new("ShaderNodeTexCoord")
            vector = uv.outputs["UV"]
            if mapping_values:
                mapping = nodes.new("ShaderNodeMapping")
                for name, value in mapping_values.items():
                    mapping.inputs[name].default_value = value
                material.node_tree.links.new(vector, mapping.inputs["Vector"])
                vector = mapping.outputs["Vector"]
            material.node_tree.links.new(vector, texture.inputs["Vector"])
            if any(abs(component - 1) > 1e-6 for component in color[:3]):
                multiply = nodes.new("ShaderNodeMixRGB")
                multiply.blend_type = "MULTIPLY"
                multiply.inputs[0].default_value = 1
                multiply.inputs[2].default_value = color
                material.node_tree.links.new(texture.outputs["Color"], multiply.inputs[1])
                material.node_tree.links.new(multiply.outputs[0], principled.inputs["Base Color"])
            else:
                material.node_tree.links.new(texture.outputs["Color"], principled.inputs["Base Color"])
            if transparent:
                material.node_tree.links.new(texture.outputs["Alpha"], principled.inputs["Alpha"])
                if hasattr(material, "surface_render_method"):
                    material.surface_render_method = "DITHERED"
                elif hasattr(material, "blend_method"):
                    material.blend_method = "BLEND"
        metadata["warnings"].append("Approximated Blender toon shader with engine albedo/PBR carrier: " + material.name)


def read_glb_json(path):
    with path.open("rb") as stream:
        magic, version, length = struct.unpack("<III", stream.read(12))
        if magic != 0x46546C67 or version != 2 or length != path.stat().st_size:
            raise ValueError("Invalid generated GLB header")
        chunk_length, chunk_type = struct.unpack("<II", stream.read(8))
        if chunk_type != 0x4E4F534A or chunk_length > 64 * 1024 * 1024:
            raise ValueError("Invalid generated GLB JSON chunk")
        return json.loads(stream.read(chunk_length))


def record_export_mapping(metadata, glb):
    document = read_glb_json(glb)
    objects = {obj["export"].get("exportNodeName", obj["name"]): obj for obj in metadata["objects"]}
    meshes = document.get("meshes", [])
    materials = document.get("materials", [])
    accessors = document.get("accessors", [])
    for index, node in enumerate(document.get("nodes", [])):
        obj = objects.get(node.get("name", ""))
        if obj is None or "mesh" not in node:
            continue
        mesh_index = node["mesh"]
        mesh = meshes[mesh_index]
        mapping = {"nodeIndex": index, "nodeName": node.get("name", ""),
                   "meshIndex": mesh_index, "meshName": mesh.get("name", ""), "primitives": []}
        for primitive_index, primitive in enumerate(mesh.get("primitives", [])):
            material_index = primitive.get("material", -1)
            material_name = materials[material_index].get("name", "") if 0 <= material_index < len(materials) else ""
            position = primitive.get("attributes", {}).get("POSITION", -1)
            indices = primitive.get("indices", -1)
            count = accessors[indices]["count"] if indices >= 0 else accessors[position]["count"] if position >= 0 else 0
            mapping["primitives"].append({"primitiveIndex": primitive_index,
                "materialIndex": material_index, "materialName": material_name,
                "sourceMaterialSlots": [material["slot"] for material in obj.get("materials", []) if material["name"] == material_name],
                "vertices": accessors[position]["count"] if position >= 0 else 0,
                "triangles": count // 3 if primitive.get("mode", 4) == 4 else 0,
                "morphTargets": len(primitive.get("targets", []))})
        obj["export"]["nodes"].append(mapping)
    metadata["export"] = {"format": "GLB", "file": glb.name,
                          "nodes": len(document.get("nodes", [])), "meshes": len(meshes),
                          "materials": len(materials), "skins": len(document.get("skins", [])),
                          "animations": len(document.get("animations", [])),
                          "sha256": hashlib.sha256(glb.read_bytes()).hexdigest()}
    for obj in metadata["objects"]:
        if obj["type"] == "MESH" and not obj["export"]["nodes"]:
            metadata["warnings"].append("Source mesh was not exported: " + obj["name"])


def main():
    import bpy

    args = arguments()
    source = Path(args.input).expanduser().resolve(strict=True)
    destination = Path(args.output_dir).expanduser().resolve()
    if source.suffix.lower() != ".blend":
        raise ValueError("Expected a .blend source file")
    if destination == source.parent or source in destination.parents:
        raise ValueError("Use a separate generated cache directory")
    destination.mkdir(parents=True, exist_ok=True)
    metadata_file = destination / "source.blend.json"
    glb = destination / "scene.glb"
    if metadata_file.exists() or (glb.exists() and not args.inspect_only):
        raise FileExistsError("The generated destination must be a new revision")
    bpy.context.preferences.filepaths.use_scripts_auto_execute = False
    if hasattr(bpy.context.preferences.system, "use_online_access"):
        bpy.context.preferences.system.use_online_access = False
    bpy.ops.wm.open_mainfile(filepath=str(source), load_ui=False, use_scripts=False)
    metadata = inspect_source(bpy, source)
    metadata["warnings"].extend([
        "Source was opened read-only with embedded Python auto-execution disabled; it was not saved.",
        "Cloth, collision, constraints and driver authoring metadata are evidence only; Blender physics caches are not engine cloth assets.",
        "glTF preserves supported PBR node inputs, skinning and shape keys; arbitrary Blender shader graphs and procedural textures require author-side baking.",
        "Supported modifiers are evaluated in their original source-pose order for Basis and every shape key, then inverse-skinned with all deform weights into the unchanged rest skeleton. SourcePose restores the authored fitting; outlines are supplied by the engine. Cloth simulation is not baked into the rest mesh.",
    ])
    for image in metadata["images"]:
        if image["source"] == "FILE" and not image["packed"] and not image["exists"]:
            metadata["warnings"].append("Missing external image: " + image["name"] + " | " + image["path"])
    if not args.inspect_only:
        prepare_export_geometry(bpy, metadata, destination)
        approximate_materials(bpy, metadata)
        if args.static_pose:
            bake_static_source_pose(bpy, metadata)
        # Only inert custom properties are in the sidecar; do not export custom
        # properties which may include untrusted script/driver authoring data.
        options = {"filepath": str(glb), "export_format": "GLB", "check_existing": False,
                   "use_selection": True, "use_visible": False, "use_renderable": False,
                   "export_yup": True, "export_apply": False, "export_skins": True,
                   "export_morph": True, "export_morph_normal": True,
                   "export_animations": True, "export_extras": False,
                   "export_def_bones": False, "export_rest_position_armature": True,
                   "export_current_frame": True,
                   "export_all_influences": True,
                   "export_materials": "EXPORT", "export_image_format": "AUTO"}
        if args.static_pose:
            options.update(export_skins=False, export_morph=False,
                           export_morph_normal=False, export_animations=False,
                           export_rest_position_armature=False)
        supported = bpy.ops.export_scene.gltf.get_rna_type().properties.keys()
        options = {key: value for key, value in options.items() if key in supported}
        result = bpy.ops.export_scene.gltf(**options)
        if "FINISHED" not in result or not glb.is_file() or glb.stat().st_size < 20:
            raise RuntimeError("Blender glTF exporter did not produce a valid GLB")
        record_export_mapping(metadata, glb)
    # A complete sidecar is the commit marker. Failed conversions have no valid
    # metadata and will never be reused by the native bridge.
    temporary = destination / "source.blend.json.tmp"
    temporary.write_text(json.dumps(metadata, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")
    temporary.replace(metadata_file)
    print("BLEND_IMPORT_OK " + str(metadata_file), flush=True)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        traceback.print_exc()
        raise
