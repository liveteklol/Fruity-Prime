using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using OpenTK.Mathematics;

namespace MphRead.Mods.Replay
{
    internal readonly record struct ReplayCameraKeyframe(uint Frame, Vector3 Position,
        Quaternion Rotation, float Fov, sbyte LookAtSlot = -1);

    /// <summary>A bounded, presentation-only track. It never contains engine or network state.</summary>
    internal sealed class ReplayCameraTrack
    {
        internal const int MaxKeys = 64;
        private const uint Magic = 0x4D435046; // FPCM
        private const int HeaderSize = 23;
        private const int KeySize = 37;
        private readonly List<ReplayCameraKeyframe> _keys = new();
        public IReadOnlyList<ReplayCameraKeyframe> Keys => _keys;
        public string? LastError { get; private set; }
        public void Clear() { _keys.Clear(); LastError = null; }

        public bool Put(ReplayCameraKeyframe key)
        {
            if (!Valid(key)) { LastError = "Camera keyframe contains an invalid position, rotation, FOV or target."; return false; }
            int index = _keys.FindIndex(k => k.Frame >= key.Frame);
            if (index >= 0 && _keys[index].Frame == key.Frame) _keys[index] = key;
            else
            {
                if (_keys.Count == MaxKeys) { LastError = "A camera track can contain up to 64 keyframes."; return false; }
                _keys.Insert(index < 0 ? _keys.Count : index, key);
            }
            LastError = null;
            return true;
        }
        public bool Remove(uint frame)
        {
            int index = _keys.FindIndex(k => k.Frame == frame);
            if (index < 0) return false;
            _keys.RemoveAt(index);
            return true;
        }
        public bool Sample(uint frame, out ReplayCameraKeyframe sample)
        {
            sample = default;
            if (_keys.Count == 0) return false;
            if (frame <= _keys[0].Frame) { sample = _keys[0]; return true; }
            for (int i = 1; i < _keys.Count; i++)
            {
                ReplayCameraKeyframe right = _keys[i];
                if (frame > right.Frame) continue;
                ReplayCameraKeyframe left = _keys[i - 1];
                float t = (frame - left.Frame) / (float)(right.Frame - left.Frame);
                sample = new ReplayCameraKeyframe(frame, Vector3.Lerp(left.Position, right.Position, t),
                    Quaternion.Slerp(left.Rotation, right.Rotation, t).Normalized(),
                    left.Fov + (right.Fov - left.Fov) * t,
                    frame == right.Frame ? right.LookAtSlot : left.LookAtSlot);
                return true;
            }
            sample = _keys[^1];
            return true;
        }
        public static Quaternion FacingRotation(Vector3 facing)
        {
            if (!Finite(facing.X) || !Finite(facing.Y) || !Finite(facing.Z) || facing.LengthSquared < 0.000001f)
                return Quaternion.Identity;
            facing.Normalize();
            float yaw = MathF.Atan2(-facing.X, -facing.Z);
            float pitch = MathF.Asin(Math.Clamp(facing.Y, -1, 1));
            return (Quaternion.FromAxisAngle(Vector3.UnitY, yaw) * Quaternion.FromAxisAngle(Vector3.UnitX, pitch)).Normalized();
        }
        private static bool Finite(float value) => float.IsFinite(value);
        private static bool Valid(ReplayCameraKeyframe key)
        {
            return Finite(key.Position.X) && Finite(key.Position.Y) && Finite(key.Position.Z)
                && Math.Abs(key.Position.X) <= 1000000 && Math.Abs(key.Position.Y) <= 1000000 && Math.Abs(key.Position.Z) <= 1000000
                && Finite(key.Rotation.X) && Finite(key.Rotation.Y) && Finite(key.Rotation.Z) && Finite(key.Rotation.W)
                && Math.Abs(key.Rotation.LengthSquared - 1) < 0.001f
                && Finite(key.Fov) && key.Fov >= MathHelper.DegreesToRadians(1) && key.Fov <= MathHelper.DegreesToRadians(175)
                && key.LookAtSlot is >= -1 and < 8;
        }
        public bool Load(string replay)
        {
            Clear();
            string sidecar = replay + ".camera";
            try
            {
                if (!File.Exists(sidecar)) return true;
                using var input = new FileStream(sidecar, FileMode.Open, FileAccess.Read, FileShare.Read);
                if (input.Length < HeaderSize + 32 || input.Length > HeaderSize + MaxKeys * KeySize + 32)
                    throw new InvalidDataException("Camera track has an invalid size.");
                byte[] bytes = new byte[(int)input.Length];
                input.ReadExactly(bytes);
                if (input.Length != bytes.Length) throw new InvalidDataException("Camera track changed while reading.");
                ReadOnlySpan<byte> body = bytes.AsSpan(0, bytes.Length - 32);
                if (!CryptographicOperations.FixedTimeEquals(SHA256.HashData(body), bytes.AsSpan(bytes.Length - 32)))
                    throw new InvalidDataException("Camera track checksum failed.");
                using var stream = new MemoryStream(bytes, writable: false);
                using var reader = new BinaryReader(stream);
                if (reader.ReadUInt32() != Magic || reader.ReadByte() != 1) throw new InvalidDataException("Unsupported camera track.");
                var source = new FileInfo(replay);
                if (!source.Exists || reader.ReadInt64() != source.Length || reader.ReadInt64() != source.LastWriteTimeUtc.Ticks)
                    throw new InvalidDataException("Camera track belongs to a different version of this replay.");
                int count = reader.ReadUInt16();
                if (count > MaxKeys || bytes.Length != HeaderSize + count * KeySize + 32) throw new InvalidDataException("Invalid camera keyframe count.");
                var parsed = new List<ReplayCameraKeyframe>(count);
                for (int i = 0; i < count; i++)
                {
                    var key = new ReplayCameraKeyframe(reader.ReadUInt32(),
                        new Vector3(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle()),
                        new Quaternion(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle()),
                        reader.ReadSingle(), reader.ReadSByte());
                    if (!Valid(key) || (i > 0 && key.Frame <= parsed[i - 1].Frame)) throw new InvalidDataException("Invalid camera keyframe.");
                    parsed.Add(key);
                }
                _keys.AddRange(parsed);
                return true;
            }
            catch (Exception ex) when (ex is IOException or InvalidDataException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
            { LastError = ex.Message; return false; }
        }
        public bool Save(string replay)
        {
            string? temporary = null;
            try
            {
                var source = new FileInfo(replay);
                if (!source.Exists) throw new FileNotFoundException("Replay no longer exists.");
                using var stream = new MemoryStream(HeaderSize + MaxKeys * KeySize + 32);
                using var writer = new BinaryWriter(stream);
                writer.Write(Magic); writer.Write((byte)1); writer.Write(source.Length); writer.Write(source.LastWriteTimeUtc.Ticks);
                writer.Write((ushort)_keys.Count);
                foreach (ReplayCameraKeyframe key in _keys)
                {
                    writer.Write(key.Frame);
                    writer.Write(key.Position.X); writer.Write(key.Position.Y); writer.Write(key.Position.Z);
                    writer.Write(key.Rotation.X); writer.Write(key.Rotation.Y); writer.Write(key.Rotation.Z); writer.Write(key.Rotation.W);
                    writer.Write(key.Fov); writer.Write(key.LookAtSlot);
                }
                writer.Flush();
                byte[] body = stream.ToArray();
                temporary = replay + $".camera.{Guid.NewGuid():N}.tmp";
                using (var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    output.Write(body); output.Write(SHA256.HashData(body)); output.Flush(flushToDisk: true);
                }
                File.Move(temporary, replay + ".camera", overwrite: true);
                LastError = null;
                return true;
            }
            catch (Exception ex) when (ex is IOException or InvalidDataException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
            { LastError = ex.Message; return false; }
            finally
            {
                if (temporary != null)
                    try { File.Delete(temporary); } catch (IOException) { } catch (UnauthorizedAccessException) { }
            }
        }
    }
}
