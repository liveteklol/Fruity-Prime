#!/usr/bin/env python3
"""Writes index.json beside the .pk3 maps FruityPrime clients download
(Mods::MapGen::MapCatalog) -- each pk3's name, size, SHA-256 and the levels
in it -- and index.html, the same as a page. Run on any change to the
folder (fp-maps-index.path)."""
import hashlib, html, json, os, sys, tempfile, time, zipfile

folder = sys.argv[1] if len(sys.argv) > 1 else "/var/www/net.livetek.fr/fruityprime/maps"
entries = []
for name in sorted(os.listdir(folder), key=str.lower):
    path = os.path.join(folder, name)
    if not name.lower().endswith(".pk3") or not os.path.isfile(path):
        continue
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    try:
        with zipfile.ZipFile(path) as z:
            levels = sorted({n.split("/")[-1][:-4] for n in z.namelist()
                             if n.lower().startswith("maps/") and n.lower().endswith(".bsp")})
    except zipfile.BadZipFile:
        continue
    if not levels:
        continue
    entries.append({"file": name, "size": os.path.getsize(path),
                    "sha256": digest.hexdigest(), "levels": levels,
                    "modified": int(os.path.getmtime(path))})


def write(name, text):
    fd, temporary = tempfile.mkstemp(dir=folder, prefix=".index-")
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(text)
    os.chmod(temporary, 0o644)
    os.replace(temporary, os.path.join(folder, name))


write("index.json", json.dumps({"version": 1, "maps": entries}, indent=1))
rows = "\n".join(
    "<tr><td><a href=\"{0}\">{0}</a></td><td>{1}</td><td class=n>{2:.1f} MB</td><td>{3}</td></tr>".format(
        html.escape(e["file"]), html.escape(", ".join(e["levels"])), e["size"] / 1048576,
        time.strftime("%Y-%m-%d", time.gmtime(e["modified"])))
    for e in entries)
write("index.html", """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FruityPrime maps</title>
<style>
body{{font:15px/1.4 system-ui,sans-serif;margin:24px auto;max-width:900px;padding:0 16px;background:#fff;color:#111}}
table{{border-collapse:collapse;width:100%}}td,th{{padding:6px 8px;border-bottom:1px solid #ddd;text-align:left}}
td.n{{text-align:right;white-space:nowrap}}a{{color:#0645ad}}
@media (prefers-color-scheme:dark){{body{{background:#111;color:#eee}}td,th{{border-color:#333}}a{{color:#8ab4f8}}}}
</style></head><body>
<h1>FruityPrime maps</h1>
<p>{count} Quake 3 map packs. FruityPrime downloads the ones it lacks by itself.</p>
<table><tr><th>Pack</th><th>Levels</th><th>Size</th><th>Added</th></tr>
{rows}
</table></body></html>
""".format(count=len(entries), rows=rows))
print(len(entries), "maps")
