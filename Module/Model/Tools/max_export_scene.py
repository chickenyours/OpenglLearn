"""Export the open 3ds Max scene without saving or modifying its geometry.

Run inside 3ds Max's Python runtime. Call export_scene(new_directory) from a
small Run Script entry point. Binary FBX retains the evaluated high mesh;
SHA-256-bound sidecar preserves source-object and modifier provenance.
"""
import hashlib
import json
from pathlib import Path
import shutil
import sys
from pymxs import runtime as rt


def export_scene(destination):
    # Preserve the state of the live scene, not just the saved .max file.
    # Temporary bitmap path assignments can mark a clean scene as dirty.
    original_dirty = bool(rt.getSaveRequired())
    output = Path(destination).resolve()
    if output.exists() and any(output.iterdir()):
        raise RuntimeError("Export directory must be new or empty: " + str(output))
    output.mkdir(parents=True, exist_ok=True)
    source = Path(str(rt.maxFilePath) + str(rt.maxFileName))
    if not source.is_file():
        raise RuntimeError("Save the source scene before exporting provenance")
    geom = [o for o in rt.objects if rt.superClassOf(o) == rt.GeometryClass]
    names = [str(o.name) for o in geom]
    if len(set(names)) != len(names):
        raise RuntimeError("Source geometry object names must be unique")
    original_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    records = []
    for o in geom:
        mat = o.material
        mats = []
        if mat:
            mats.append(str(mat.name))
            for i in range(1, int(rt.getNumSubMtls(mat)) + 1):
                child = rt.getSubMtl(mat, i)
                if child:
                    mats.append(str(child.name))
        records.append({"name": str(o.name), "layer": str(o.layer.name),
                        "parent": str(o.parent.name) if o.parent else "",
                        "class": str(rt.classOf(o)), "materials": mats,
                        "modifiers": [{"name": str(m.name), "type": str(rt.classOf(m)),
                                       "enabled": bool(m.enabled)} for m in o.modifiers],
                        "sourceFaces": int(rt.getPolygonCount(o)[0]),
                        "sourceVertices": int(rt.getPolygonCount(o)[1])})

    # Resolve legacy workstation paths only within this source resource folder.
    candidates = {}
    for p in source.parent.rglob('*'):
        if p.is_file() and p.suffix.lower() in {'.png', '.jpg', '.jpeg', '.tga', '.bmp', '.tif', '.tiff', '.dds', '.exr'}:
            candidates.setdefault(p.name.casefold(), []).append(p)
    texture_dir = output / 'Textures'
    restored = []
    textures = []
    pushed = False
    fbx = output / 'scene.fbx'
    try:
        for bitmap in list(rt.getClassInstances(rt.Bitmaptexture)):
            original = str(bitmap.filename)
            if not original:
                continue
            path = Path(original)
            if not path.is_file():
                found = candidates.get(path.name.casefold(), [])
                if len(found) != 1:
                    raise RuntimeError("Missing or ambiguous source texture: " + original)
                path = found[0]
            texture_dir.mkdir(exist_ok=True)
            copy = texture_dir / path.name
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if copy.exists():
                if hashlib.sha256(copy.read_bytes()).hexdigest() != digest:
                    raise RuntimeError("Texture basename collision: " + path.name)
            else:
                shutil.copy2(path, copy)
            restored.append((bitmap, original))
            bitmap.filename = str(copy)
            textures.append({"source": original, "resolved": str(path),
                             "exported": 'Textures/' + copy.name, "sha256": digest})

        rt.pluginManager.loadClass(rt.FBXEXP)
        rt.FBXExporterSetParam('PushSettings')
        pushed = True
        rt.FBXExporterSetParam('ResetExport')
        settings = {'ASCII': False, 'Animation': False, 'Cameras': False,
                    'Lights': False, 'EmbedTextures': True,
                    'Preserveinstances': False, 'SmoothingGroups': True,
                    'NormalsPerPoly': False, 'TangentSpaceExport': True,
                    'Triangulate': True, 'SmoothMeshExport': False,
                    'Skin': True, 'Shape': True, 'UpAxis': 'Y',
                    'ConvertUnit': 'm', 'GenerateLog': True, 'ShowWarnings': False}
        accepted = {}
        for key, value in settings.items():
            result = rt.FBXExporterSetParam(key, value)
            accepted[key] = {'requested': value, 'result': str(result),
                             'readback': str(rt.FBXExporterGetParam(key))}
            # pymxs in Max 2026 returns True for accepted setters; MAXScript
            # versions also expose the documented OK string.
            if result is not True and str(result).lower() != 'ok':
                raise RuntimeError('Unsupported FBX setting: ' + key + ' = ' + str(result))
        version = str(rt.FBXExporterGetParam('FileVersion'))
        result = rt.exportFile(str(fbx), rt.Name('noPrompt'), selectedOnly=False, using=rt.FBXEXP)
        if result is False or not fbx.is_file() or fbx.stat().st_size < 128:
            raise RuntimeError('FBX exporter did not produce a valid output file')
        metadata = {'assetType': 'MaxSceneMetadata', 'version': 1,
                    'fbxSha256': hashlib.sha256(fbx.read_bytes()).hexdigest(),
                    'source': str(source),
                    'objects': [{k: v for k, v in o.items() if k not in ('sourceFaces', 'sourceVertices')}
                                for o in records]}
        (output / 'scene.fbx.max.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding='utf-8')
        (output / 'export_report.json').write_text(json.dumps({
            'settings': accepted, 'fileVersion': version, 'textures': textures,
            'source': str(source), 'sourceSystemUnit': str(rt.units.SystemType),
            'sourceSystemScale': float(rt.units.SystemScale),
            'exportedUpAxis': 'Y', 'exportedUnit': 'm', 'objects': records,
            'geometryObjects': len(records),
            'sourceFaces': sum(o['sourceFaces'] for o in records),
            'sourceVertices': sum(o['sourceVertices'] for o in records),
            'fbxBytes': fbx.stat().st_size,
            'originalSourceHash': original_hash}, ensure_ascii=False, indent=2), encoding='utf-8')
    finally:
        active_error = sys.exc_info()[1]
        restoration_errors = []
        for bitmap, original in reversed(restored):
            try:
                bitmap.filename = original
            except Exception as error:
                restoration_errors.append("bitmap path " + repr(original) + ": " + repr(error))
        if pushed:
            try:
                result = rt.FBXExporterSetParam('PopSettings')
                if result is not True and str(result).lower() != 'ok':
                    raise RuntimeError("FBX PopSettings returned " + str(result))
            except Exception as error:
                restoration_errors.append("FBX exporter settings: " + repr(error))
        try:
            # An incomplete restoration must remain visibly dirty rather than
            # hiding an outstanding live-scene change behind the old clean flag.
            rt.setSaveRequired(original_dirty or bool(restoration_errors))
        except Exception as error:
            restoration_errors.append("scene save-required flag: " + repr(error))
        if restoration_errors:
            message = ("Model bridge: live-scene restoration was incomplete. "
                       "The scene was kept dirty where possible. " + "; ".join(restoration_errors))
            # Reporting must never replace an existing export exception, even
            # if the host's optional Python stderr is unavailable or cannot
            # encode a source texture name.
            diagnostic = message.encode('ascii', 'backslashreplace').decode('ascii')
            try:
                print(diagnostic, file=sys.stderr, flush=True)
            except Exception:
                try:
                    print(diagnostic, flush=True)
                except Exception:
                    pass
            if active_error is None:
                raise RuntimeError(message)
    if hashlib.sha256(source.read_bytes()).hexdigest() != original_hash:
        raise RuntimeError('Source file changed during export')
    print('Model bridge: full-density FBX exported to ' + str(fbx))
    return str(fbx)
