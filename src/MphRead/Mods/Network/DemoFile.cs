using System;
using System.IO;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// A recorded match: every packet a client received, verbatim, each
    /// timestamped with how many milliseconds after the recording started it
    /// arrived. Played back by feeding them to <see cref="NetSession"/> at
    /// the same pace they originally arrived at, through the exact code path
    /// that applied them live -- see <see cref="DemoPlayback"/>.
    ///
    /// One file, sequential, no index: this is the format a "press record,
    /// press stop" button needs. Seeking would need one; nothing here reads
    /// or writes one yet.
    /// </summary>
    internal static class DemoFile
    {
        // "FPDM" -- Fruity Prime DeMo.
        public static readonly byte[] Magic = { (byte)'F', (byte)'P', (byte)'D', (byte)'M' };
        public const byte FormatVersion = 1;
        public const string Extension = ".fpdemo";
    }

    /// <summary>Appends recorded packets to a file as they arrive. Not thread-safe -- called from the net-update thread only.</summary>
    internal sealed class DemoWriter : IDisposable
    {
        private readonly FileStream _stream;
        private readonly BinaryWriter _writer;

        public DemoWriter(string path)
        {
            string? dir = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(dir))
            {
                Directory.CreateDirectory(dir);
            }
            _stream = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.Read);
            _writer = new BinaryWriter(_stream);
            _writer.Write(DemoFile.Magic);
            _writer.Write(DemoFile.FormatVersion);
            // NetConfig.ProtocolVersion is a const int, not a byte -- writing
            // it directly would write 4 bytes here while DemoReader.Open
            // only ever reads 1 back, desyncing every record after it by 3
            // bytes for the rest of the file. Cast explicitly so there's
            // exactly one byte to disagree about, not a width to get wrong
            // again the same way.
            _writer.Write((byte)NetConfig.ProtocolVersion);
        }

        /// <param name="elapsedMs">Milliseconds since this writer was created.</param>
        public void WriteRecord(uint elapsedMs, ReadOnlySpan<byte> data)
        {
            _writer.Write(elapsedMs);
            _writer.Write((ushort)data.Length);
            _writer.Write(data);
            // Flushed per record rather than left to the OS: a demo that
            // crashes with the game must still be watchable up to that
            // point, not truncated to whatever the last buffer flush caught.
            _writer.Flush();
        }

        public void Dispose()
        {
            _writer.Dispose();
            _stream.Dispose();
        }
    }

    /// <summary>One recorded packet, read back from a demo file.</summary>
    internal readonly struct DemoRecord
    {
        public readonly uint ElapsedMs;
        public readonly byte[] Data;

        public DemoRecord(uint elapsedMs, byte[] data)
        {
            ElapsedMs = elapsedMs;
            Data = data;
        }
    }

    /// <summary>Reads a demo file's header, then its records in order.</summary>
    internal sealed class DemoReader : IDisposable
    {
        private readonly FileStream _stream;
        private readonly BinaryReader _reader;

        public byte ProtocolVersion { get; }

        /// <summary>Null if the file doesn't look like a demo at all (bad magic, truncated header).</summary>
        public static DemoReader? Open(string path)
        {
            FileStream? stream = null;
            try
            {
                stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
                var reader = new BinaryReader(stream);
                byte[] magic = reader.ReadBytes(DemoFile.Magic.Length);
                if (!magic.AsSpan().SequenceEqual(DemoFile.Magic))
                {
                    stream.Dispose();
                    return null;
                }
                byte formatVersion = reader.ReadByte();
                if (formatVersion != DemoFile.FormatVersion)
                {
                    stream.Dispose();
                    return null;
                }
                byte protocolVersion = reader.ReadByte();
                return new DemoReader(stream, reader, protocolVersion);
            }
            catch (IOException)
            {
                stream?.Dispose();
                return null;
            }
        }

        private DemoReader(FileStream stream, BinaryReader reader, byte protocolVersion)
        {
            _stream = stream;
            _reader = reader;
            ProtocolVersion = protocolVersion;
        }

        /// <summary>The next record, or null at end of file.</summary>
        public DemoRecord? ReadNext()
        {
            if (_stream.Position >= _stream.Length)
            {
                return null;
            }
            try
            {
                uint elapsedMs = _reader.ReadUInt32();
                ushort length = _reader.ReadUInt16();
                byte[] data = _reader.ReadBytes(length);
                if (data.Length != length)
                {
                    // Cut off mid-record -- the process that wrote it did not
                    // get to flush the rest. Treat what's left as the end.
                    return null;
                }
                return new DemoRecord(elapsedMs, data);
            }
            catch (EndOfStreamException)
            {
                return null;
            }
        }

        public void Dispose()
        {
            _reader.Dispose();
            _stream.Dispose();
        }
    }
}
