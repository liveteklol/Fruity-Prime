#!/usr/bin/env python3
"""Writes index.json beside the .pk3 maps FruityPrime clients download
(Mods::MapGen::MapCatalog) -- each pk3's name, size, SHA-256, the levels in
it and each level's picture -- and index.html, the same as a page with a
form to add a map (upload.php). Run on any change to the folder
(fp-maps-index.path).

A level's picture is the levelshot its pack carries (levelshots/<level>.jpg
or .tga), written beside the packs as <pack>__<level>.preview.jpg (a JPEG
as it is) or .png (a TGA, decoded here: no imaging library needed)."""
import hashlib, html, json, os, re, struct, sys, tempfile, time, zipfile, zlib

folder = sys.argv[1] if len(sys.argv) > 1 else "/var/www/net.livetek.fr/fruityprime/maps"


def tga_to_png(data):
    """An uncompressed or RLE true-colour TGA (what levelshots are) as PNG."""
    idlen, cmaptype, kind = data[0], data[1], data[2]
    width, height = struct.unpack_from("<HH", data, 12)
    bpp, desc = data[16], data[17]
    if cmaptype != 0 or kind not in (2, 10) or bpp not in (24, 32) or not width or not height:
        return None
    step = bpp // 8
    at = 18 + idlen
    count = width * height
    pixels = bytearray()
    if kind == 2:
        pixels = data[at:at + count * step]
    else:
        while len(pixels) < count * step and at < len(data):
            head = data[at]; at += 1
            n = (head & 0x7F) + 1
            if head & 0x80:
                pixels += data[at:at + step] * n; at += step
            else:
                pixels += data[at:at + n * step]; at += n * step
    if len(pixels) < count * step:
        return None
    rows = []
    for y in range(height):
        src = y if desc & 0x20 else height - 1 - y
        row = pixels[src * width * step:(src + 1) * width * step]
        out = bytearray(width * 3)
        out[0::3] = row[2::step]; out[1::3] = row[1::step]; out[2::3] = row[0::step]
        rows.append(b"\0" + bytes(out))

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


def write_bytes(name, data):
    fd, temporary = tempfile.mkstemp(dir=folder, prefix=".index-")
    with os.fdopen(fd, "wb") as f:
        f.write(data)
    os.chmod(temporary, 0o644)
    os.replace(temporary, os.path.join(folder, name))


def write(name, text):
    write_bytes(name, text.encode("utf-8"))


def safe(text):
    return re.sub(r"[^A-Za-z0-9._+-]", "_", text)


entries = []
previews_kept = set()
for name in sorted(os.listdir(folder), key=str.lower):
    path = os.path.join(folder, name)
    if not name.lower().endswith(".pk3") or not os.path.isfile(path):
        continue
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    previews = {}
    try:
        with zipfile.ZipFile(path) as z:
            names = z.namelist()
            levels = sorted({n.split("/")[-1][:-4] for n in names
                             if n.lower().startswith("maps/") and n.lower().endswith(".bsp")})
            lower = {n.lower(): n for n in names}
            for level in levels:
                for extension in (".jpg", ".jpeg", ".tga", ".png"):
                    member = lower.get("levelshots/" + level.lower() + extension)
                    if member is None:
                        continue
                    data = z.read(member)
                    if extension == ".tga":
                        data, extension = tga_to_png(data), ".png"
                    elif extension == ".jpeg":
                        extension = ".jpg"
                    if not data:
                        continue
                    picture = safe(name[:-4]) + "__" + safe(level) + ".preview" + extension
                    target = os.path.join(folder, picture)
                    if not os.path.exists(target) or os.path.getmtime(target) < os.path.getmtime(path):
                        write_bytes(picture, data)
                    previews[level] = picture
                    previews_kept.add(picture)
                    break
    except (zipfile.BadZipFile, OSError, struct.error):
        continue
    if not levels:
        continue
    entries.append({"file": name, "size": os.path.getsize(path),
                    "sha256": digest.hexdigest(), "levels": levels,
                    "previews": previews,
                    "modified": int(os.path.getmtime(path))})

# The pictures of packs that are gone.
for name in os.listdir(folder):
    if ".preview." in name and name not in previews_kept:
        try:
            os.remove(os.path.join(folder, name))
        except OSError:
            pass

write("index.json", json.dumps({"version": 2, "maps": entries}, indent=1))

cards = []
for e in entries:
    for level in e["levels"]:
        picture = e["previews"].get(level)
        image = ('<img src="{0}" alt="" loading="lazy">'.format(html.escape(picture)) if picture
                 else '<div class="none">Q3</div>')
        cards.append("""<li>{image}<div class="t"><b>{level}</b><span>{pack} &middot; {size:.1f} MB &middot; {date}</span>
<a href="{href}" download>Download</a></div></li>""".format(
            image=image, level=html.escape(level), pack=html.escape(e["file"]), size=e["size"] / 1048576,
            date=time.strftime("%Y-%m-%d", time.gmtime(e["modified"])), href=html.escape(e["file"])))

write("index.html", """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FruityPrime maps</title>
<style>
:root{{--bg:#f6f7f9;--card:#fff;--text:#111;--dim:#667;--line:#dde;--accent:#e8a33a}}
@media (prefers-color-scheme:dark){{:root{{--bg:#10141c;--card:#1a2030;--text:#e8ecf4;--dim:#93a0b8;--line:#2a3348}}}}
body{{font:15px/1.4 system-ui,sans-serif;margin:0;background:var(--bg);color:var(--text)}}
main{{max-width:1100px;margin:0 auto;padding:24px 16px}}
h1{{margin:0 0 4px}}p{{color:var(--dim);margin:0 0 20px}}
form{{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px;margin:0 0 24px;display:flex;flex-wrap:wrap;gap:12px;align-items:center}}
form b{{flex-basis:100%}}
button{{background:var(--accent);color:#111;border:0;border-radius:6px;padding:8px 16px;font-weight:600;cursor:pointer}}
button:disabled{{opacity:.5;cursor:default}}
progress{{flex:1;min-width:120px}}#status{{flex-basis:100%;color:var(--dim)}}
ul{{list-style:none;padding:0;margin:0;display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:14px}}
li{{background:var(--card);border:1px solid var(--line);border-radius:10px;overflow:hidden}}
li img,.none{{display:block;width:100%;aspect-ratio:16/9;object-fit:cover;background:#000}}
.none{{display:flex;align-items:center;justify-content:center;font:900 42px/1 system-ui;color:#7a1b12;background:#1b0b08}}
.t{{padding:10px 12px;display:flex;flex-direction:column;gap:4px}}.t span{{color:var(--dim);font-size:13px;overflow-wrap:anywhere}}
.t a{{color:var(--accent);font-weight:600;text-decoration:none}}
</style></head><body><main>
<h1>FruityPrime maps</h1>
<p>{count} Quake 3 levels. FruityPrime lists them under Offline &rsaquo; Download maps.</p>
<form id="up"><b>Add a map</b>
<input type="file" id="file" accept=".pk3" required>
<button id="send" type="submit">Upload</button>
<progress id="bar" max="1" value="0" hidden></progress>
<span id="status">A .pk3 holding at least one level (maps/*.bsp), up to 100 MB.</span>
</form>
<ul>
{cards}
</ul></main>
<script>
const form = document.getElementById("up"), file = document.getElementById("file"),
  send = document.getElementById("send"), bar = document.getElementById("bar"), status = document.getElementById("status");
form.addEventListener("submit", (event) => {{
  event.preventDefault();
  const chosen = file.files[0];
  if (!chosen || !/\\.pk3$/i.test(chosen.name)) {{ status.textContent = "Choose a .pk3 file."; return; }}
  if (chosen.size > 100 * 1048576) {{ status.textContent = "That file is over 100 MB."; return; }}
  const data = new FormData(); data.append("pk3", chosen);
  const request = new XMLHttpRequest();
  request.open("POST", "../upload.php");
  request.upload.onprogress = (e) => {{ if (e.lengthComputable) bar.value = e.loaded / e.total; }};
  request.onload = () => {{
    send.disabled = false; bar.hidden = true;
    let answer = {{}}; try {{ answer = JSON.parse(request.responseText); }} catch (e) {{}}
    status.textContent = answer.message || ("Upload failed (" + request.status + ").");
    if (request.status === 200) setTimeout(() => location.reload(), 4000);
  }};
  request.onerror = () => {{ send.disabled = false; bar.hidden = true; status.textContent = "Upload failed."; }};
  send.disabled = true; bar.hidden = false; bar.value = 0; status.textContent = "Uploading " + chosen.name + "...";
  request.send(data);
}});
</script>
</body></html>
""".format(count=sum(len(e["levels"]) for e in entries), cards="\n".join(cards)))
print(len(entries), "maps")
