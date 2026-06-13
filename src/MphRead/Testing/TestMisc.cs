using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using MphRead.Entities;
using MphRead.Formats;
using MphRead.Formats.Collision;
using MphRead.Formats.Sound;
using MphRead.Utility;

namespace MphRead.Testing
{
    public static class TestMisc
    {
        public static void TestAllFV()
        {
            foreach (string path in Directory.EnumerateFiles(@"C:\Users\auser\Home\MPH\_FS\amfe\data\movies"))
            {
                TestFV(path);
            }
            Nop();
        }

        private static readonly ImmutableArray<int> _dword206B720 =
        [
            -0x7F99, -0x7ECC, -0x7E00, -0x7D33, -0x7C66, -0x7B99, -0x7ACC, -0x7A00, -0x77FF, -0x74CD,
            -0x7199, -0x6E65, -0x6B33, -0x67FF, -0x64CD, -0x6199, -0x5E65, -0x5B33, -0x57FF, -0x5334,
            -0x4CCC, -0x4668, -0x4000, -0x3998, -0x3334, -0x2CCC, -0x2668, -0x2000, -0x1998, -0x1334,
            -0xCCC, -0x668, 0, 0x668, 0xCCC, 0x1334, 0x1998, 0x2000, 0x2668, 0x2CCC, 0x3334, 0x3998,
            0x4000, 0x4668, 0x4CCC, 0x5334, 0x57FF, 0x5B33, 0x5E67, 0x6199, 0x64CD, 0x67FF, 0x6B33,
            0x6E65, 0x7199, 0x74CD, 0x77FF, 0x7A00, 0x7ACC, 0x7B99, 0x7C66, 0x7D33, 0x7E00, 0x7ECC
        ];

        private static readonly ImmutableArray<int> _dword206B820 =
        [
            -0x6B33, -0x67FF, -0x64CD, -0x6199, -0x5E65, -0x5B33, -0x57FF, -0x5334, -0x4CCC, -0x4668,
            -0x4000, -0x3998, -0x3334, -0x2CCC, -0x2668, -0x2000, -0x1998, -0x1334, -0xCCC, -0x668,
            0, 0x668, 0xCCC, 0x1334, 0x1998, 0x2000, 0x2668, 0x2CCC, 0x3334, 0x3998, 0x4000, 0x4668
        ];

        private static readonly ImmutableArray<int> _dword206B8A0 =
        [
            -0x4668, -0x4000, -0x3998, -0x3334, -0x2CCC, -0x2668, -0x2000, -0x1998, -0x1334, -0xCCC,
            -0x668, 0, 0x668, 0xCCC, 0x1334, 0x1998, 0x2000, 0x2668, 0x2CCC, 0x3334, 0x3998, 0x4000,
            0x4668, 0x4CCC, 0x5334, 0x57FF, 0x5B33, 0x5E67, 0x6199, 0x64CD, 0x67FF, 0x6B33
        ];

        private static readonly ImmutableArray<int> _dword206B920 =
        [
            -0x4CD0, -0x436C, -0x3A0C, -0x30A8, -0x2744, -0x1DE0, -0x1480, -0xB1C, -0x1B8, 0x7A8,
            0x110C, 0x1A70, 0x23D4, 0x2D34, 0x3698, 0x3FFC
        ];

        private static readonly ImmutableArray<int> _dword206B960 =
        [
            -0x3334, -0x23D8, -0x147C, -0x520, 0xA3C, 0x1998, 0x28F4, 0x384C
        ];

        private static readonly ImmutableArray<int> _dword206B980 =
        [
            -0x199C, -0xB1C, 0x368, 0x11E8, 0x2068, 0x2EEC, 0x3D6C, 0x4BEC
        ];

        private static readonly ImmutableArray<int> _dword206BEBC =
        [
            -0x2668, -0x1DDC, -0x1554, -0xCCC, -0x444, 0x444, 0xCCC, 0x1554, 0x1DDC, 0x2668, 0x2EF0,
            0x3778, 0x4000, 0x4888, 0x5110, 0x57FF
        ];

        private static readonly ImmutableArray<short> _dword206C268 =
        [
            -0x1C, -0x14, -0x0C, -0x04, 0x04, 0x0C, 0x14, 0x1C, -0x38, -0x28, -0x18,
            -0x08, 0x08, 0x18, 0x28, 0x38, -0x54, -0x3C, -0x24, -0x0C, 0x0C, 0x24, 0x3C, 0x54, -0x70,
            -0x50, -0x30, -0x10, 0x10, 0x30, 0x50, 0x70, -0x8C, -0x64, -0x3C, -0x14,
            0x14, 0x3C, 0x64, 0x8C, -0xA8, -0x78, -0x48, -0x18, 0x18, 0x48, 0x78, 0xA8, -0xC4, -0x8C,
            -0x54, -0x1C, 0x1C, 0x54, 0x8C, 0xC4, -0xE0, -0xA0, -0x60, -0x20, 0x20, 0x60, 0xA0,
            0xE0, -0xFC, -0xB4, -0x6C, -0x24, 0x24, 0x6C, 0xB4, 0xFC, -0x118, -0xC8, -0x78,
            -0x28, 0x28, 0x78, 0xC8, 0x118, -0x134, -0xDC, -0x84, -0x2C, 0x2C, 0x84, 0xDC, 0x134, -0x150,
            -0xF0, -0x90, -0x30, 0x30, 0x90, 0xF0, 0x150, -0x16C, -0x104, -0x9C, -0x34,
            0x34, 0x9C, 0x104, 0x16C, -0x188, -0x118, -0xA8, -0x38, 0x38, 0xA8, 0x118, 0x188, -0x1A4, -0x12C,
            -0xB4, -0x3C, 0x3C, 0xB4, 0x12C, 0x1A4, -0x1C0, -0x140, -0xC0, -0x40, 0x40, 0xC0, 0x140,
            0x1C0, -0x1F8, -0x168, -0xD8, -0x48, 0x48, 0xD8, 0x168, 0x1F8, -0x230, -0x190, -0xF0,
            -0x50, 0x50, 0xF0, 0x190, 0x230, -0x268, -0x1B8, -0x108, -0x58, 0x58, 0x108, 0x1B8, 0x268,
            -0x2A0, -0x1E0, -0x120, -0x60, 0x60, 0x120, 0x1E0, 0x2A0, -0x2D8, -0x208, -0x138,
            -0x68, 0x68, 0x138, 0x208, 0x2D8, -0x310, -0x230, -0x150, -0x70, 0x70, 0x150, 0x230,
            0x310, -0x348, -0x258, -0x168, -0x78, 0x78, 0x168, 0x258, 0x348, -0x380, -0x280,
            -0x180, -0x80, 0x80, 0x180, 0x280, 0x380, -0x3F0, -0x2D0, -0x1B0, -0x90, 0x90,
            0x1B0, 0x2D0, 0x3F0, -0x460, -0x320, -0x1E0, -0xA0, 0xA0, 0x1E0, 0x320, 0x460, -0x4D0,
            -0x370, -0x210, -0xB0, 0xB0, 0x210, 0x370, 0x4D0, -0x540, -0x3C0, -0x240, -0xC0,
            0xC0, 0x240, 0x3C0, 0x540, -0x5B0, -0x410, -0x270, -0xD0, 0xD0, 0x270, 0x410, 0x5B0,
            -0x620, -0x460, -0x2A0, -0xE0, 0xE0, 0x2A0, 0x460, 0x620, -0x690, -0x4B0, -0x2D0,
            -0xF0, 0xF0, 0x2D0, 0x4B0, 0x690, -0x700, -0x500, -0x300, -0x100, 0x100, 0x300, 0x500,
            0x700, -0x7E0, -0x5A0, -0x360, -0x120, 0x120, 0x360, 0x5A0, 0x7E0, -0x8C0, -0x640,
            -0x3C0, -0x140, 0x140, 0x3C0, 0x640, 0x8C0, -0x9A0, -0x6E0, -0x420, -0x160, 0x160,
            0x420, 0x6E0, 0x9A0, -0xA80, -0x780, -0x480, -0x180, 0x180, 0x480, 0x780, 0xA80, -0xB60,
            -0x820, -0x4E0, -0x1A0, 0x1A0, 0x4E0, 0x820, 0xB60, -0xC40, -0x8C0, -0x540, -0x1C0,
            0x1C0, 0x540, 0x8C0, 0xC40, -0xD20, -0x960, -0x5A0, -0x1E0, 0x1E0, 0x5A0, 0x960, 0xD20,
            -0xE00, -0xA00, -0x600, -0x200, 0x200, 0x600, 0xA00, 0xE00, -0xFC0, -0xB40, -0x6C0,
            -0x240, 0x240, 0x6C0, 0xB40, 0xFC0, -0x1180, -0xC80, -0x780, -0x280, 0x280, 0x780, 0xC80,
            0x1180, -0x1340, -0xDC0, -0x840, -0x2C0, 0x2C0, 0x840, 0xDC0, 0x1340, -0x1500, -0xF00,
            -0x900, -0x300, 0x300, 0x900, 0xF00, 0x1500, -0x16C0, -0x1040, -0x9C0, -0x340, 0x340,
            0x9C0, 0x1040, 0x16C0, -0x1880, -0x1180, -0xA80, -0x380, 0x380, 0xA80, 0x1180, 0x1880, -0x1A40,
            -0x12C0, -0xB40, -0x3C0, 0x3C0, 0xB40, 0x12C0, 0x1A40, -0x1C00, -0x1400, -0xC00, -0x400,
            0x400, 0xC00, 0x1400, 0x1C00, -0x1F7F, -0x167F, -0xD80, -0x480, 0x480, 0xD80, 0x1680, 0x1F80,
            -0x22FF, -0x18FF, -0xF00, -0x500, 0x500, 0xF00, 0x1900, 0x2300, -0x267F, -0x1B7F, -0x1080,
            -0x580, 0x580, 0x1080, 0x1B80, 0x2680, -0x29FF, -0x1DFF, -0x1200, -0x600, 0x600, 0x1200,
            0x1E00, 0x2A00, -0x2D7F, -0x207F, -0x1380, -0x680, 0x680, 0x1380, 0x2080, 0x2D80, -0x30FF,
            -0x22FF, -0x1500, -0x700, 0x700, 0x1500, 0x2300, 0x3100, -0x347F, -0x257F, -0x1680,
            -0x780, 0x780, 0x1680, 0x2580, 0x3480, -0x37FF, -0x27FF, -0x1800, -0x800, 0x800, 0x1800,
            0x2800, 0x3800, -0x3EFF, -0x2CFF, -0x1B00, -0x900, 0x900, 0x1B00, 0x2CFF, 0x3EFF, -0x45FF,
            -0x31FF, -0x1E00, -0xA00, 0xA00, 0x1E00, 0x31FF, 0x45FF, -0x4CFF, -0x36FF, -0x2100,
            -0xB00, 0xB00, 0x2100, 0x36FF, 0x4CFF, -0x53FF, -0x3BFF, -0x2400, -0xC00, 0xC00, 0x2400,
            0x3BFF, 0x53FF, -0x5AFF, -0x40FF, -0x2700, -0xD00, 0xD00, 0x2700, 0x40FF, 0x5AFF, -0x61FF,
            -0x45FF, -0x2A00, -0xE00, 0xE00, 0x2A00,0x45FF, 0x61FF, -0x68FF, -0x4AFF, -0x2D00,
            -0xF00, 0xF00, 0x2D00, 0x4AFF, 0x68FF, -0x6FFF, -0x4FFF, -0x3000, -0x1000, 0x1000, 0x3000, 0x4FFF, 0x6FFF
        ];

        public static void TestFV(string? path = null)
        {
            if (path == null)
            {
                ///path = @"C:\Users\auser\Home\MPH\_FS\amfe\data\movies\spawn_yellow-15fps-up-left.avi.fv";
                path = @"C:\Users\auser\Home\MPH\_FS\amfe\data\movies\teaser-15fps-up-left.avi.fv";
            }
            ReadOnlySpan<byte> fileBytes = File.ReadAllBytes(path);
            using FileStream fileStream = File.OpenRead(path);
            using var reader = new BinaryReader(fileStream);
            char[] magic = new char[4];
            magic[0] = reader.ReadChar(); // F (70)
            magic[1] = reader.ReadChar(); // V (86)
            magic[2] = reader.ReadChar(); // D (68)
            magic[3] = reader.ReadChar(); // S (83)
            int frameCount = reader.ReadInt32(); // 46
            int frameWidth = reader.ReadInt32(); // 256
            int frameHeight = reader.ReadInt32(); // 192
            decimal frameRate = reader.ReadInt32() / 65536m; // 983035
            int audioSampleRate = reader.ReadInt32(); // 32768
            int totalDataSize = reader.ReadInt32(); // 157236
            int maxDataSize = reader.ReadInt32(); // 7060
            int field24 = reader.ReadInt32(); // 4608
            Debug.Assert(magic[0] == 'F' && magic[1] == 'V' && magic[2] == 'D' && magic[3] == 'S');
            Debug.Assert(frameCount > 0);
            Debug.Assert(frameWidth == 256);
            Debug.Assert(frameHeight == 192);
            Debug.Assert(frameRate == 14.9999237060546875m);
            Debug.Assert(audioSampleRate == 32768);
            Debug.Assert(totalDataSize == fileBytes.Length - 36 - 16);
            Debug.Assert(field24 == 4608);
            var framePositions = new List<long>();
            var skipAheadPositions = new List<long>();
            var dataBuffer = new Span<byte>(new byte[maxDataSize - 8]);
            byte[] outputBuffer1 = new byte[2 * 256 * 192];
            byte[] outputBuffer2 = new byte[2 * 256 * 192];
            bool foundMaxSize = false;

            var decodeBuf = new Span<int>(new int[113]);

            int audioFrameTotal = 0;
            var samples = new List<short>(); // todo: get rid of this once we're streaming/writing
            Span<short> sampleSpan = stackalloc short[256];
            ReadOnlySpan<short> table206C268 = _dword206C268.AsSpan();

            for (int i = 0; i < frameCount; i++)
            {
                dataBuffer.Clear();
                framePositions.Add(reader.BaseStream.Position);
                skipAheadPositions.RemoveAll(r => r == reader.BaseStream.Position);
                int dataSize = reader.ReadInt32();
                int seekBackOffset = reader.ReadInt32();
                int seekAheadOffset = reader.ReadInt32();
                int frameIndex = reader.ReadInt32();
                int videoSize = reader.ReadInt32();
                fileStream.Read(dataBuffer.Slice(0, videoSize));
                int audioSize = reader.ReadInt32();
                fileStream.Read(dataBuffer.Slice(videoSize, audioSize));
                Debug.Assert(frameIndex == i);
                if (i == 0)
                {
                    Debug.Assert(seekBackOffset == 0);
                }
                if (i == frameCount - 1)
                {
                    // always 0 except at the end of opening-15fps-up-left.avi.fv
                    // in that one, frame n-2 has 0, frame n-1 has -1256, and frame n has -2284
                    // those are the only times skipAheadOffset is negative
                    Debug.Assert(seekAheadOffset <= 0);
                }
                if (seekBackOffset < 0)
                {
                    Debug.Assert(framePositions.Contains(framePositions[^1] + seekBackOffset));
                }
                if (seekAheadOffset > 0)
                {
                    skipAheadPositions.Add(framePositions[^1] + seekAheadOffset);
                }
                Debug.Assert(seekBackOffset <= 0);
                //Debug.Assert(skipAheadOffset >= 0);
                // dataSize includes the two size values, does not include the header (4 ints)
                // likewise, a buffer sized using maxDataSize holds 8 unnecessary bytes, due to the frame's video and audio size values being included
                Debug.Assert(dataSize == videoSize + audioSize + 8);
                Debug.Assert(audioSize == 320 || audioSize == 360);
                if (dataSize == maxDataSize)
                {
                    foundMaxSize = true;
                }
                // video
                int read1 = BinaryPrimitives.ReadInt32LittleEndian(dataBuffer);
                int read2 = BinaryPrimitives.ReadInt32LittleEndian(dataBuffer.Slice(4));
                int offset = read1 + read2 + 8;
                // audio
                int audioFrameCount = audioSize / 40;
                audioFrameTotal += audioFrameCount;
                Span<uint> audioData = MemoryMarshal.Cast<byte, uint>(dataBuffer.Slice(videoSize, audioSize));
                for (int j = 0; j < audioFrameCount; j++)
                {
                    Span<uint> audioFrameData = audioData.Slice(j * 10, 10);

                    static void Sub206B9A0(Span<uint> audioFrameData, Span<int> decodeBuf)
                    {
                        int destIndex = 0;
                        uint value = audioFrameData[0];
                        decodeBuf[destIndex++] = _dword206B720[(int)(value >> 26)];
                        decodeBuf[destIndex++] = _dword206B720[(int)((value >> 20) & 0x3F)];
                        decodeBuf[destIndex++] = _dword206B820[(int)((value >> 15) & 0x1F)];
                        decodeBuf[destIndex++] = _dword206B8A0[(int)((value >> 10) & 0x1F)];
                        decodeBuf[destIndex++] = _dword206B920[(int)((value >> 6) & 0xF)];
                        destIndex++; // decodeBuf[5] is written at the end
                        decodeBuf[destIndex++] = _dword206B960[(int)((value >> 3) & 7)];
                        decodeBuf[destIndex++] = _dword206B980[(int)(value & 7)];
                        value = audioFrameData[1];
                        decodeBuf[destIndex++] = (int)(value & 3);
                        decodeBuf[destIndex++] = (int)((value >> 2) & 3);
                        decodeBuf[destIndex++] = (int)((value >> 4) & 3);
                        decodeBuf[destIndex++] = (byte)value >> 6;
                        decodeBuf[destIndex++] = (int)((value >> 8) & 0x3F);
                        decodeBuf[destIndex++] = (int)((value >> 14) & 0x3F);
                        decodeBuf[destIndex++] = (int)((value >> 20) & 0x3F);
                        decodeBuf[destIndex++] = (int)(value >> 26);
                        int index5 = 0;
                        uint prevValue;
                        for (int i = 0; i < 8; i++)
                        {
                            prevValue = value;
                            value = audioFrameData[i + 2];
                            decodeBuf[destIndex++] = (int)(value >> 29);
                            decodeBuf[destIndex++] = (int)((value >> 26) & 7);
                            decodeBuf[destIndex++] = (int)((value >> 23) & 7);
                            decodeBuf[destIndex++] = (int)((value >> 20) & 7);
                            decodeBuf[destIndex++] = (int)((value >> 17) & 7);
                            decodeBuf[destIndex++] = (int)((value >> 14) & 7);
                            decodeBuf[destIndex++] = (int)((value >> 11) & 7);
                            decodeBuf[destIndex++] = (int)((value >> 8) & 7);
                            decodeBuf[destIndex++] = (byte)value >> 5;
                            decodeBuf[destIndex++] = (int)((value >> 2) & 7);
                            if (i % 2 == 1)
                            {
                                decodeBuf[destIndex++] = ((int)(value & 2) >> 1) | (2 * (int)(prevValue & 3));
                                index5 |= (int)(value & 1) << ((8 - i) / 2);
                            }
                        }
                        decodeBuf[5] = _dword206BEBC[index5];
                    }

                    static void Sub206BEFC(Span<short> sampleSpan, Span<int> decodeSlice, ReadOnlySpan<short> table, int clearCount)
                    {
                        sampleSpan.Clear();
                        for (int i = 0; i < 21; i++)
                        {
                            sampleSpan[i * 3 + clearCount] = table[decodeSlice[i]];
                        }
                    }

                    Sub206B9A0(audioFrameData, decodeBuf);
                    for (int k = 0; k < 4; k++)
                    {
                        Span<short> sampleSlice = sampleSpan.Slice(64 * k, 64);
                        Span<int> decodeSlice = decodeBuf.Slice(16 + 21 * k, 21);
                        ReadOnlySpan<short> tableSlice = table206C268.Slice(decodeBuf[k + 12] * 8);
                        Sub206BEFC(sampleSlice, decodeSlice, tableSlice, decodeBuf[k + 8]);
                    }
                    int halfSample = decodeBuf[109];
                    for (int k = 0; k < 256; k++)
                    {
                        int runningValue = sampleSpan[k];
                        for (int l = 107; l >= 100; l--)
                        {
                            int currentValue = decodeBuf[l];
                            int oppositeValue = decodeBuf[l - 100];
                            runningValue = runningValue - ((oppositeValue * currentValue + 0x4000) >> 15);
                            decodeBuf[l + 1] = currentValue + ((oppositeValue * runningValue + 0x4000) >> 15);
                        }
                        decodeBuf[100] = runningValue;
                        halfSample = runningValue + ((0x6E14 * halfSample + 0x4000) >> 15);
                        short sample = (short)Math.Clamp(halfSample * 2, Int16.MinValue, Int16.MaxValue);
                        sampleSpan[k] = sample;
                        samples.Add(sample);
                    }
                    decodeBuf[109] = halfSample;
                }
                Nop();
                Nop();
            }
            Debug.Assert(foundMaxSize);
            Debug.Assert(skipAheadPositions.Count == 0);
            int lastFrameValue = reader.ReadInt32();
            Debug.Assert(lastFrameValue == 0);
            int lastFrameValue1 = reader.ReadInt32();
            Debug.Assert(lastFrameValue1 == 0);
            int lastFrameValue2 = reader.ReadInt32();
            Debug.Assert(lastFrameValue2 == 0);
            int lastFrameValue3 = reader.ReadInt32();
            Debug.Assert(lastFrameValue3 == 0);
            Debug.Assert(reader.BaseStream.Position == fileBytes.Length);
            //using var output = File.OpenWrite(@"C:\Users\auser\Temp\out.wav");
            using var output = File.OpenWrite($@"C:\Users\auser\Temp\out_{Path.GetFileNameWithoutExtension(path)}.wav");
            using var writer = new BinaryWriter(output);
            SoundRead.WriteWavHeader(writer, (uint)audioFrameTotal * 256, (ushort)audioSampleRate, WaveFormat.PCM16);
            foreach (short sample in samples)
            {
                ushort data = (ushort)sample;
                writer.Write((byte)(data & 0xFF));
                writer.Write((byte)((data >> 8) & 0xFF));
            }
            Nop();
            Nop();
        }

        //public static int GetSfxIndex(string query)
        //{
        //    IReadOnlyList<SoundSample> samples = SoundRead.ReadSoundSamples();
        //    string[] split = query.Split(", ");
        //    var num = split.Select(s => s.StartsWith("0x") ? UInt32.Parse(s.Replace("0x", ""), NumberStyles.HexNumber) : UInt32.Parse(s)).ToList();
        //    var results = samples.Where(s => s.Header.Field0 == num[0] && s.Header.Field4 == num[1]
        //        && s.Header.Field6 == num[2] && s.Header.Field8 == num[3] && s.Header.FieldA == num[4]).ToList();
        //    if (results.Count != 1)
        //    {
        //        Debugger.Break();
        //    }
        //    return samples.IndexOf(s => s == results[0]);
        //}

        public static void TestCameraSequences()
        {
            var ids = new HashSet<int>();
            foreach (KeyValuePair<string, RoomMetadata> meta in Metadata.RoomMetadata)
            {
                if (meta.Value.EntityPath != null)
                {
                    IReadOnlyList<Entity> entities = Read.GetEntities(meta.Value.EntityPath, -1, meta.Value.FirstHunt);
                    foreach (Entity entity in entities)
                    {
                        if (entity.Type == EntityType.CameraSequence)
                        {
                            CameraSequenceEntityData data = ((Entity<CameraSequenceEntityData>)entity).Data;
                            var entityClass = new CamSeqEntity(data, scene: null!);
                            if (ids.Contains(data.SequenceId))
                            {
                                continue;
                            }
                            ids.Add(data.SequenceId);
                            foreach (CameraSequenceKeyframe frame in entityClass.Sequence.Keyframes)
                            {
                            }
                            Nop();
                        }
                    }
                }
            }
            Nop();
        }

        public static void TestCameraSequenceFiles()
        {
            foreach (string filePath in Directory.EnumerateFiles(Paths.Combine(Paths.FileSystem, "cameraEditor")))
            {
                string name = Path.GetFileName(filePath);
                if (name != "cameraEditBG.bin")
                {
                    var seq = CameraSequence.Load(name, scene: null!);
                    Nop();
                }
            }
            Nop();
        }

        public static void TestAllCollision()
        {
            var allCollision = new List<(bool, CollisionInstance)>();
            foreach (KeyValuePair<string, RoomMetadata> meta in Metadata.RoomMetadata)
            {
                if (!meta.Value.Hybrid)
                {
                    allCollision.Add((true, Collision.GetCollision(meta.Value)));
                }
            }
            foreach (KeyValuePair<string, ModelMetadata> meta in Metadata.ModelMetadata)
            {
                if (meta.Value.CollisionPath != null)
                {
                    allCollision.Add((false, Collision.GetCollision(meta.Value)));
                    if (meta.Value.ExtraCollisionPath != null)
                    {
                        allCollision.Add((false, Collision.GetCollision(meta.Value, extra: true)));
                    }
                }
            }
            foreach ((bool room, CollisionInstance instance) in allCollision)
            {
                if (instance.Info is MphCollisionInfo collision)
                {
                    foreach (CollisionData data in collision.Data)
                    {
                    }
                }
                else if (instance.Info is FhCollisionInfo fhCollision)
                {
                }
            }
            Nop();
        }

        public static void TestAllFhCollision()
        {
            var allCollision = new List<(bool, CollisionInstance)>();
            foreach (KeyValuePair<string, RoomMetadata> meta in Metadata.RoomMetadata)
            {
                if (meta.Value.FirstHunt || meta.Value.Hybrid)
                {
                    allCollision.Add((true, Collision.GetCollision(meta.Value)));
                }
            }
            foreach (KeyValuePair<string, ModelMetadata> meta in Metadata.FirstHuntModels)
            {
                if (meta.Value.CollisionPath != null)
                {
                    allCollision.Add((false, Collision.GetCollision(meta.Value)));
                }
            }
            foreach ((bool room, CollisionInstance instance) in allCollision)
            {
                var collision = (FhCollisionInfo)instance.Info;
            }
            Nop();
        }

        public static void ConvertRoomToMph(string room, string? over = null)
        {
            RoomMetadata meta = Metadata.RoomMetadata[room];
            RoomMetadata? overMeta = null;
            if (over != null)
            {
                overMeta = Metadata.RoomMetadata[over];
            }
            Debug.Assert(meta.EntityPath != null && meta.NodePath != null);
            string folder = Paths.Combine(Paths.Export, "_pack");
            string fileSystem = meta.FirstHunt ? Paths.FhFileSystem : Paths.FileSystem;
            Console.WriteLine("Converting model...");
            // model, texure
            (byte[] model, byte[] texture) = Repack.RepackRoomModel(room, separateTextures: true);
            string modelPath = Path.GetFileName(overMeta?.ModelPath ?? meta.ModelPath);
            string modelDest = Paths.Combine(folder, modelPath);
            string texDest = Paths.Combine(folder, modelPath.Replace("_Model.bin", "_Tex.bin").Replace("_model.bin", "_tex.bin"));
            File.WriteAllBytes(modelDest, model);
            File.WriteAllBytes(texDest, texture);
            Console.WriteLine("Converting collision...");
            // collision
            byte[] collision = RepackCollision.RepackMphRoom(room);
            string colDest = Paths.Combine(folder, Path.GetFileName(overMeta?.CollisionPath ?? meta.CollisionPath));
            File.WriteAllBytes(colDest, collision);
            Console.WriteLine("Converting animation...");
            // animation
            string animSrc = Paths.Combine(fileSystem, meta.AnimationPath);
            string animDest = Paths.Combine(folder, Path.GetFileName(overMeta?.AnimationPath ?? meta.AnimationPath));
            File.Delete(animDest);
            File.Copy(animSrc, animDest);
            //entity, nodedata
            if (meta.Hybrid)
            {
                Console.WriteLine("Copying entities...");
                Console.WriteLine("Copying nodedata...");
                string entSrc = Paths.Combine(fileSystem, meta.EntityPath);
                string nodeSrc = Paths.Combine(fileSystem, meta.NodePath);
                string entDest = Paths.Combine(folder, meta.EntityPath);
                string nodeDest = Paths.Combine(folder, meta.NodePath);
                if (overMeta != null)
                {
                    Debug.Assert(overMeta.EntityPath != null && overMeta.NodePath != null);
                    entDest = Paths.Combine(folder, overMeta.EntityPath);
                    nodeDest = Paths.Combine(folder, overMeta.NodePath);
                }
                File.Delete(entDest);
                File.Delete(nodeDest);
                File.Copy(entSrc, entDest);
                File.Copy(nodeSrc, nodeDest);
            }
            else
            {
                Console.WriteLine("Converting entities...");
                byte[] entity = Repack.RepackMphEntities(room);
                string entDest = Paths.Combine(folder, Path.GetFileName(overMeta?.EntityPath ?? meta.EntityPath));
                File.WriteAllBytes(entDest, entity);
                // todo: nodedata
            }
            Console.Write("Creating archive...");
            // archive
            var files = new List<string>()
            {
                animDest,
                colDest,
                modelDest
            };
            string outPath = Paths.Combine(folder, "out.arc");
            Archive.Archiver.Archive(outPath, files);
            string archiveName = overMeta?.Archive ?? meta.Archive;
            Console.WriteLine(" Compressing...");
            LZ10.Compress(outPath, outPath.Replace("out.arc", $"{archiveName}.arc"));
            File.Delete(outPath);
            Console.WriteLine("Done.");
            Nop();
        }

        public static void ConvertRoomToFh(string room, string? over = null)
        {
            RoomMetadata meta = Metadata.RoomMetadata[room];
            RoomMetadata? overMeta = null;
            if (over != null)
            {
                overMeta = Metadata.RoomMetadata[over];
            }
            Debug.Assert(meta.EntityPath != null && meta.NodePath != null);
            string folder = Paths.Combine(Paths.Export, "_pack");
            string fileSystem = meta.FirstHunt ? Paths.FhFileSystem : Paths.FileSystem;
            RepackFilter filter = RepackFilter.All;
            if (!meta.FirstHunt && !meta.Hybrid)
            {
                filter = meta.Multiplayer ? RepackFilter.Multiplayer : RepackFilter.SinglePlayer;
            }
            Console.WriteLine("Converting model...");
            // model, texure
            (byte[] model, _) = Repack.RepackRoomModel(room, separateTextures: false, filter);
            string modelPath = Path.GetFileName(overMeta?.ModelPath ?? meta.ModelPath);
            string modelDest = Paths.Combine(folder, modelPath);
            File.WriteAllBytes(modelDest, model);
            Console.WriteLine("Converting collision...");
            // collision
            byte[] collision = RepackCollision.RepackFhRoom(room, filter);
            string colDest = Paths.Combine(folder, Path.GetFileName(overMeta?.CollisionPath ?? meta.CollisionPath));
            File.WriteAllBytes(colDest, collision);
            Console.WriteLine("Converting animation...");
            // animation
            string animSrc = Paths.Combine(fileSystem, meta.AnimationPath);
            string animDest = Paths.Combine(folder, Path.GetFileName(overMeta?.AnimationPath ?? meta.AnimationPath));
            File.Delete(animDest);
            File.Copy(animSrc, animDest);
            //entity, nodedata
            if (meta.Hybrid)
            {
                Console.WriteLine("Copying entities...");
                Console.WriteLine("Copying nodedata...");
                string entSrc = Paths.Combine(fileSystem, meta.EntityPath);
                string nodeSrc = Paths.Combine(fileSystem, meta.NodePath);
                string entDest = Paths.Combine(folder, meta.EntityPath);
                string nodeDest = Paths.Combine(folder, meta.NodePath);
                if (overMeta != null)
                {
                    Debug.Assert(overMeta.EntityPath != null && overMeta.NodePath != null);
                    entDest = Paths.Combine(folder, overMeta.EntityPath);
                    nodeDest = Paths.Combine(folder, overMeta.NodePath);
                }
                File.Delete(entDest);
                File.Delete(nodeDest);
                File.Copy(entSrc, entDest);
                File.Copy(nodeSrc, nodeDest);
            }
            else
            {
                Console.WriteLine("Converting entities...");
                byte[] entity = Repack.RepackFhEntities(room, filter);
                string entDest = Paths.Combine(folder, Path.GetFileName(overMeta?.EntityPath ?? meta.EntityPath));
                File.WriteAllBytes(entDest, entity);
                // todo: nodedata
            }
            Console.WriteLine("Done.");
            Nop();
        }

        public static void TestCameraShake()
        {
            for (int i = 0; i < 1; i++)
            {
                Rng.DoCameraShake(204);
            }
            Console.WriteLine();
            var chances = new List<int>() { 50, 50, 50 };
            foreach (int chance in chances)
            {
                bool spawn = Rng.GetRandomInt2(100) < chance;
                Console.WriteLine(spawn);
                if (spawn)
                {
                    Rng.GetRandomInt2(1);
                }
            }
            Console.ReadLine();
        }

        private static void Nop()
        {
        }
    }
}
