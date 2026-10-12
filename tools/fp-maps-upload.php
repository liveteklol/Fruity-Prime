<?php
// Adds a Quake 3 map pack to FruityPrime's maps (maps/, beside index.json):
// a .pk3 -- a zip holding at least one maps/*.bsp -- up to 100 MB, under a
// name not taken yet. fp-maps-index then lists it (fp-maps-index.path).
header('Content-Type: application/json');
header('Cache-Control: no-store');

function answer($code, $message) {
    http_response_code($code);
    echo json_encode(['message' => $message]);
    exit;
}

const MAX_BYTES = 100 * 1048576;
const KEEP_FREE = 1024 * 1048576;
const MAPS = __DIR__ . '/maps';

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    answer(405, 'POST a .pk3 file.');
}
$file = $_FILES['pk3'] ?? null;
if ($file === null || !is_array($file) || is_array($file['name'])) {
    answer(400, 'No file was sent.');
}
if ($file['error'] === UPLOAD_ERR_INI_SIZE || $file['error'] === UPLOAD_ERR_FORM_SIZE) {
    answer(413, 'That file is over 100 MB.');
}
if ($file['error'] !== UPLOAD_ERR_OK || !is_uploaded_file($file['tmp_name'])) {
    answer(400, 'The upload did not arrive whole.');
}
if ($file['size'] <= 0 || $file['size'] > MAX_BYTES) {
    answer(413, 'That file is over 100 MB.');
}

// The name: the file's own, kept to letters, digits and ._+- .
$name = basename((string)$file['name']);
if (!preg_match('/\.pk3$/i', $name)) {
    answer(415, 'Only .pk3 files are accepted.');
}
$stem = preg_replace('/[^A-Za-z0-9._+-]/', '_', substr($name, 0, -4));
$stem = trim($stem, '.');
if ($stem === '' || strlen($stem) > 64) {
    answer(400, 'That file name cannot be used.');
}
$name = $stem . '.pk3';
$target = MAPS . '/' . $name;
foreach (scandir(MAPS) as $existing) {
    if (strcasecmp($existing, $name) === 0) {
        answer(409, $name . ' is already on the server.');
    }
}
if (disk_free_space(MAPS) - $file['size'] < KEEP_FREE) {
    answer(507, 'The server is out of room for maps.');
}

// A zip, holding a level, with no entry climbing out of its folder
// (fp-pk3-check, which also names the levels).
$output = [];
$status = 1;
exec('/usr/local/bin/fp-pk3-check ' . escapeshellarg($file['tmp_name']), $output, $status);
if ($status !== 0 || count($output) === 0) {
    answer(415, 'That file is not a .pk3 holding a level (maps/*.bsp).');
}
$levels = $output;

// Same contents under another name: one copy is enough.
$digest = hash_file('sha256', $file['tmp_name']);
$index = json_decode((string)@file_get_contents(MAPS . '/index.json'), true);
foreach (($index['maps'] ?? []) as $known) {
    if (($known['sha256'] ?? '') === $digest) {
        answer(409, 'This map is already on the server as ' . $known['file'] . '.');
    }
}

$temporary = MAPS . '/.upload-' . bin2hex(random_bytes(8));
if (!move_uploaded_file($file['tmp_name'], $temporary)) {
    answer(500, 'The map could not be stored.');
}
chmod($temporary, 0644);
if (file_exists($target) || !rename($temporary, $target)) {
    @unlink($temporary);
    answer(409, $name . ' is already on the server.');
}
error_log('fruityprime map upload: ' . $name . ' (' . $file['size'] . ' bytes, ' . implode(', ', $levels)
    . ') from ' . ($_SERVER['REMOTE_ADDR'] ?? '?'));
answer(200, 'Added ' . implode(', ', $levels) . '. It will be listed in a few seconds.');
