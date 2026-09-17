"""Release dependency check; full security/geometry validation is -mapvalidate."""
import json
import pathlib
import sys
import zipfile


def fields(value):
    if isinstance(value, dict):
        return {key.lower(): fields(item) for key, item in value.items()}
    if isinstance(value, list):
        return [fields(item) for item in value]
    return value


def dependencies(project):
    imported = project.get("import")
    if imported:
        yield imported["source"]
        if imported.get("textures"):
            yield imported["textures"]
        elif not project.get("materials"):
            raise ValueError("import has neither baked nor borrowed materials")
    collision = project.get("collision")
    if collision and collision.get("source"):
        yield collision["source"]
    for asset in project.get("assets", []):
        yield asset["path"]


def check(path):
    if path.suffix == ".fpmap":
        with zipfile.ZipFile(path) as archive:
            names = archive.namelist()
            manifest = fields(json.loads(archive.read("manifest.json"))) if "manifest.json" in names else None
            recipes = [name for name in names if name.lower().endswith(".json")]
            project_name = manifest["project"] if manifest else recipes[0] if len(recipes) == 1 else None
            if project_name is None:
                raise ValueError("expected one legacy recipe")
            project = fields(json.loads(archive.read(project_name)))
            for reference in dependencies(project):
                if reference not in names:
                    raise ValueError(f"missing packaged dependency {reference}")
    else:
        project = fields(json.loads(path.read_text(encoding="utf-8-sig")))
        for reference in dependencies(project):
            if not (path.parent / reference).is_file():
                # Loose PK3 recipes can bake their derived .tex on first build.
                imported = project.get("import", {})
                if reference == imported.get("textures") and imported.get("source", "").lower().endswith(".pk3"):
                    continue
                raise ValueError(f"missing dependency {reference}")


root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "maps")
failed = False
for path in sorted(root.rglob("*")):
    if not path.is_file() or path.suffix not in (".json", ".fpmap"):
        continue
    if any(part.startswith(".") or part in ("textures", "audio", "preview") for part in path.relative_to(root).parts):
        continue
    if path.name in ("manifest.json", "map.build.json"):
        continue
    if path.suffix == ".json" and (root / (path.stem + ".fpmap")).is_file():
        continue
    try:
        check(path)
        print(f"ok: {path}")
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        print(f"MISSING: {path}: {error}")
        failed = True
sys.exit(1 if failed else 0)
