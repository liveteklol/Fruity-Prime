using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using MphRead.Formats;
using OpenTK.Mathematics;

namespace MphRead.Mods.MapGen
{
    /// <summary>
    /// Works out where a bot can stand and how it gets from one place to
    /// another, and writes that as the node data the game's own AI reads.
    ///
    /// Without it a custom room has `nodePath` null, and the difference is not
    /// subtle: a bot with no node data never moves, never changes form and
    /// never fires. The same eight bots in a cartridge room fight. Everything
    /// else about a custom map worked long before this did, which is why it
    /// looked like a map problem rather than a missing file.
    ///
    /// Two lists per node, because the AI reads two. The neighbours are what
    /// it walks; the other is a routing table -- for every destination in the
    /// room, which neighbour to set off towards -- run-length encoded, since
    /// whole ranges of destinations lie the same way. That is the game's own
    /// format, not an invention here: the code that reads it walks pairs of
    /// (how many destinations this covers, which node to head for).
    /// </summary>
    public static class MapNodePacker
    {
        /// <summary>Distance between candidate standing places.</summary>
        private const float Spacing = 4f;
        /// <summary>Headroom a hunter needs to stand up.</summary>
        private const float Headroom = 1.9f;
        /// <summary>What a walk can climb, and what a fall can drop.</summary>
        private const float StepUp = 1.2f;
        private const float StepDown = 8f;
        /// <summary>Cosine of the steepest slope that counts as floor.</summary>
        private const float WalkNormal = 0.7f;
        /// <summary>The AI reads a fixed-size neighbour buffer; keep well inside it.</summary>
        private const int MaxNeighbours = 8;
        /// <summary>
        /// The routing table is a row per node over every node, so its cost is
        /// quadratic. Past this the spacing is widened instead -- a bot that
        /// navigates a room on 700 waypoints is not improved by 3000.
        /// </summary>
        private const int MaxNodes = 700;

        private sealed class Node
        {
            public Vector3 Position;
            public int Cell;
            public readonly List<int> Neighbours = new List<int>();
        }

        public static (byte[] Bytes, int Nodes, int Edges) Pack(IReadOnlyList<BuiltFace> solid)
        {
            float spacing = Spacing;
            List<Node> nodes;
            while (true)
            {
                nodes = Sample(solid, spacing);
                if (nodes.Count <= MaxNodes || spacing > 64)
                {
                    break;
                }
                spacing *= 1.4f;
            }
            Connect(solid, nodes, spacing);
            int edges = nodes.Sum(n => n.Neighbours.Count) / 2;
            return (Write(nodes, spacing), nodes.Count, edges);
        }

        /// <summary>
        /// Every place a hunter could stand: the floor under each point of a
        /// grid over the room, at every height where there is one, with room
        /// above it.
        /// </summary>
        private static List<Node> Sample(IReadOnlyList<BuiltFace> solid, float spacing)
        {
            Bounds(solid, out Vector3 min, out Vector3 max);
            var buckets = Buckets(solid, min, spacing, out int columns, out int rows);
            var nodes = new List<Node>();
            var heights = new List<float>();
            for (int row = 0; row < rows; row++)
            {
                for (int column = 0; column < columns; column++)
                {
                    float x = min.X + (column + 0.5f) * spacing;
                    float z = min.Z + (row + 0.5f) * spacing;
                    heights.Clear();
                    List<int>? bucket = buckets[row * columns + column];
                    if (bucket == null)
                    {
                        continue;
                    }
                    foreach (int index in bucket)
                    {
                        BuiltFace face = solid[index];
                        if (face.Normal.Y < WalkNormal || !Contains(face, x, z, out float y))
                        {
                            continue;
                        }
                        heights.Add(y);
                    }
                    heights.Sort();
                    float last = Single.MinValue;
                    foreach (float y in heights)
                    {
                        // one node per storey: a floor and the thing it rests
                        // on are not two places to stand
                        if (y - last < Headroom)
                        {
                            last = y;
                            continue;
                        }
                        last = y;
                        if (Blocked(solid, bucket, x, z, y))
                        {
                            continue;
                        }
                        nodes.Add(new Node()
                        {
                            Position = new Vector3(x, y + 0.5f, z),
                            Cell = row * columns + column
                        });
                    }
                }
            }
            return nodes;
        }

        /// <summary>Is there something in the way of standing up here?</summary>
        private static bool Blocked(IReadOnlyList<BuiltFace> solid, List<int> bucket, float x, float z, float y)
        {
            foreach (int index in bucket)
            {
                BuiltFace face = solid[index];
                if (face.Normal.Y > -0.3f)
                {
                    continue;
                }
                if (Contains(face, x, z, out float ceiling) && ceiling > y + 0.2f && ceiling < y + Headroom)
                {
                    return true;
                }
            }
            return false;
        }

        private static void Connect(IReadOnlyList<BuiltFace> solid, List<Node> nodes, float spacing)
        {
            Bounds(solid, out Vector3 min, out Vector3 max);
            var buckets = Buckets(solid, min, spacing, out int columns, out int rows);
            var byCell = new Dictionary<int, List<int>>();
            for (int i = 0; i < nodes.Count; i++)
            {
                if (!byCell.TryGetValue(nodes[i].Cell, out List<int>? list))
                {
                    list = new List<int>();
                    byCell.Add(nodes[i].Cell, list);
                }
                list.Add(i);
            }
            for (int i = 0; i < nodes.Count; i++)
            {
                Node node = nodes[i];
                int column = node.Cell % columns;
                int row = node.Cell / columns;
                for (int dr = -1; dr <= 1; dr++)
                {
                    for (int dc = -1; dc <= 1; dc++)
                    {
                        if (dr == 0 && dc == 0)
                        {
                            continue;
                        }
                        int nc = column + dc;
                        int nr = row + dr;
                        if (nc < 0 || nr < 0 || nc >= columns || nr >= rows)
                        {
                            continue;
                        }
                        if (!byCell.TryGetValue(nr * columns + nc, out List<int>? candidates))
                        {
                            continue;
                        }
                        foreach (int j in candidates)
                        {
                            if (j == i || node.Neighbours.Count >= MaxNeighbours || node.Neighbours.Contains(j))
                            {
                                continue;
                            }
                            float rise = nodes[j].Position.Y - node.Position.Y;
                            if (rise > StepUp || rise < -StepDown)
                            {
                                continue;
                            }
                            if (Wall(solid, buckets, columns, rows, min, spacing, node.Position, nodes[j].Position))
                            {
                                continue;
                            }
                            node.Neighbours.Add(j);
                            if (nodes[j].Neighbours.Count < MaxNeighbours && !nodes[j].Neighbours.Contains(i))
                            {
                                nodes[j].Neighbours.Add(i);
                            }
                        }
                    }
                }
            }
        }

        /// <summary>
        /// Is there a wall between these two places? Every near-vertical
        /// surface nearby is a plane; if the line between the two crosses one
        /// inside the surface's own extent, at a height a hunter occupies,
        /// they are not neighbours however close together they look.
        /// </summary>
        private static bool Wall(IReadOnlyList<BuiltFace> solid, List<int>?[] buckets, int columns, int rows,
            Vector3 min, float spacing, Vector3 from, Vector3 to)
        {
            float low = MathF.Min(from.Y, to.Y) + 0.3f;
            float high = MathF.Max(from.Y, to.Y) + Headroom;
            var seen = new HashSet<int>();
            foreach (int cell in Cells(from, to, min, spacing, columns, rows))
            {
                List<int>? bucket = buckets[cell];
                if (bucket == null)
                {
                    continue;
                }
                foreach (int index in bucket)
                {
                    if (!seen.Add(index))
                    {
                        continue;
                    }
                    BuiltFace face = solid[index];
                    if (MathF.Abs(face.Normal.Y) > 0.5f)
                    {
                        continue;
                    }
                    float d = Vector3.Dot(face.Normal, face.Points[0]);
                    float start = Vector3.Dot(face.Normal, from) - d;
                    float end = Vector3.Dot(face.Normal, to) - d;
                    if (start > 0 == end > 0 || MathF.Abs(start - end) < 0.0001f)
                    {
                        continue;
                    }
                    Vector3 crossing = from + (to - from) * (start / (start - end));
                    if (Extent(face, crossing, low, high))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        /// <summary>Does the surface actually reach the point the line crossed its plane at?</summary>
        private static bool Extent(BuiltFace face, Vector3 point, float low, float high)
        {
            float minX = Single.MaxValue;
            float maxX = Single.MinValue;
            float minY = Single.MaxValue;
            float maxY = Single.MinValue;
            float minZ = Single.MaxValue;
            float maxZ = Single.MinValue;
            foreach (Vector3 corner in face.Points)
            {
                minX = MathF.Min(minX, corner.X);
                maxX = MathF.Max(maxX, corner.X);
                minY = MathF.Min(minY, corner.Y);
                maxY = MathF.Max(maxY, corner.Y);
                minZ = MathF.Min(minZ, corner.Z);
                maxZ = MathF.Max(maxZ, corner.Z);
            }
            return point.X >= minX - 0.05f && point.X <= maxX + 0.05f
                && point.Z >= minZ - 0.05f && point.Z <= maxZ + 0.05f
                && maxY > low && minY < high;
        }

        private static IEnumerable<int> Cells(Vector3 from, Vector3 to, Vector3 min, float spacing,
            int columns, int rows)
        {
            int steps = 1 + (int)((to - from).Length / spacing);
            for (int i = 0; i <= steps; i++)
            {
                Vector3 point = from + (to - from) * (i / (float)steps);
                int column = (int)MathF.Floor((point.X - min.X) / spacing);
                int row = (int)MathF.Floor((point.Z - min.Z) / spacing);
                if (column >= 0 && row >= 0 && column < columns && row < rows)
                {
                    yield return row * columns + column;
                }
            }
        }

        private static List<int>?[] Buckets(IReadOnlyList<BuiltFace> solid, Vector3 min, float spacing,
            out int columns, out int rows)
        {
            Bounds(solid, out Vector3 low, out Vector3 high);
            columns = Math.Max(1, (int)MathF.Ceiling((high.X - low.X) / spacing) + 1);
            rows = Math.Max(1, (int)MathF.Ceiling((high.Z - low.Z) / spacing) + 1);
            var buckets = new List<int>?[columns * rows];
            for (int i = 0; i < solid.Count; i++)
            {
                BuiltFace face = solid[i];
                float minX = face.Points.Min(p => p.X);
                float maxX = face.Points.Max(p => p.X);
                float minZ = face.Points.Min(p => p.Z);
                float maxZ = face.Points.Max(p => p.Z);
                int c0 = Math.Clamp((int)MathF.Floor((minX - low.X) / spacing), 0, columns - 1);
                int c1 = Math.Clamp((int)MathF.Floor((maxX - low.X) / spacing), 0, columns - 1);
                int r0 = Math.Clamp((int)MathF.Floor((minZ - low.Z) / spacing), 0, rows - 1);
                int r1 = Math.Clamp((int)MathF.Floor((maxZ - low.Z) / spacing), 0, rows - 1);
                for (int r = r0; r <= r1; r++)
                {
                    for (int c = c0; c <= c1; c++)
                    {
                        (buckets[r * columns + c] ??= new List<int>()).Add(i);
                    }
                }
            }
            return buckets;
        }

        private static void Bounds(IReadOnlyList<BuiltFace> solid, out Vector3 min, out Vector3 max)
        {
            min = new Vector3(Single.MaxValue);
            max = new Vector3(Single.MinValue);
            foreach (BuiltFace face in solid)
            {
                foreach (Vector3 point in face.Points)
                {
                    min = Vector3.ComponentMin(min, point);
                    max = Vector3.ComponentMax(max, point);
                }
            }
        }

        /// <summary>Point-in-triangle in plan, and the height of the surface there.</summary>
        private static bool Contains(BuiltFace face, float x, float z, out float y)
        {
            y = 0;
            Vector3[] points = face.Points;
            bool inside = false;
            for (int i = 0, j = points.Length - 1; i < points.Length; j = i++)
            {
                if (points[i].Z > z != points[j].Z > z
                    && x < (points[j].X - points[i].X) * (z - points[i].Z)
                        / (points[j].Z - points[i].Z) + points[i].X)
                {
                    inside = !inside;
                }
            }
            if (!inside || MathF.Abs(face.Normal.Y) < 0.0001f)
            {
                return false;
            }
            float d = Vector3.Dot(face.Normal, points[0]);
            y = (d - face.Normal.X * x - face.Normal.Z * z) / face.Normal.Y;
            return true;
        }

        /// <summary>
        /// The file. Version 6: a header, one set holding one list, a struct
        /// per node, and then the shared array of 16-bit values both of each
        /// node's lists point into.
        /// </summary>
        private static byte[] Write(List<Node> nodes, float spacing)
        {
            var routes = Routes(nodes);
            using var stream = new MemoryStream();
            using var writer = new BinaryWriter(stream);
            const int indexOffset = 14;
            const int dataOffset = 16;
            int listOffset = 24;
            int nodeOffset = 32;
            int valuesOffset = nodeOffset + nodes.Count * 36;
            // where each node's two lists land in the shared array
            var routeOffset = new int[nodes.Count];
            var neighbourOffset = new int[nodes.Count];
            int cursor = valuesOffset;
            for (int i = 0; i < nodes.Count; i++)
            {
                routeOffset[i] = cursor;
                cursor += routes[i].Count * 2;
                neighbourOffset[i] = cursor;
                cursor += nodes[i].Neighbours.Count * 2;
            }
            writer.Write((ushort)6);
            writer.Write((ushort)1); // one set
            writer.Write((uint)indexOffset);
            writer.Write((uint)dataOffset);
            writer.Write((ushort)1); // one index
            writer.Write((ushort)0); // that index
            writer.Write((uint)listOffset); // the set holds one list
            writer.Write((ushort)1);
            writer.Write((ushort)0x5C);
            writer.Write((uint)nodeOffset); // the list holds the nodes
            writer.Write((ushort)nodes.Count);
            writer.Write((ushort)0x5C);
            for (int i = 0; i < nodes.Count; i++)
            {
                Node node = nodes[i];
                writer.Write((ushort)NodeType.Navigation);
                // the id is the index: the routing table is read as indices
                // into the list and the neighbour list as ids, and making them
                // the same number is what lets one node answer both
                writer.Write((ushort)i);
                writer.Write((ushort)0);
                writer.Write((ushort)node.Neighbours.Count);
                writer.Write(Fixed.ToInt(node.Position.X));
                writer.Write(Fixed.ToInt(node.Position.Y));
                writer.Write(Fixed.ToInt(node.Position.Z));
                writer.Write(Fixed.ToInt(spacing));
                writer.Write((uint)routeOffset[i]);
                writer.Write((uint)neighbourOffset[i]);
                writer.Write(0u);
            }
            for (int i = 0; i < nodes.Count; i++)
            {
                foreach ((ushort run, ushort hop) in routes[i])
                {
                    writer.Write(run);
                    writer.Write(hop);
                }
                foreach (int neighbour in nodes[i].Neighbours)
                {
                    writer.Write((ushort)neighbour);
                }
            }
            return stream.ToArray();
        }

        /// <summary>
        /// For every node, which way to set off for every other node, run-
        /// length encoded over the destinations. A breadth-first search from
        /// each destination gives the distances; the neighbour that is one
        /// step closer is the answer, and a node with nowhere to go points at
        /// itself so the reader's own loop still advances.
        /// </summary>
        private static List<(ushort, ushort)>[] Routes(List<Node> nodes)
        {
            int count = nodes.Count;
            var hop = new ushort[count, count];
            var distance = new int[count];
            var queue = new Queue<int>();
            for (int destination = 0; destination < count; destination++)
            {
                Array.Fill(distance, -1);
                distance[destination] = 0;
                queue.Clear();
                queue.Enqueue(destination);
                while (queue.Count > 0)
                {
                    int current = queue.Dequeue();
                    foreach (int neighbour in nodes[current].Neighbours)
                    {
                        if (distance[neighbour] < 0)
                        {
                            distance[neighbour] = distance[current] + 1;
                            queue.Enqueue(neighbour);
                        }
                    }
                }
                for (int source = 0; source < count; source++)
                {
                    ushort answer = (ushort)source;
                    if (source != destination && distance[source] > 0)
                    {
                        foreach (int neighbour in nodes[source].Neighbours)
                        {
                            if (distance[neighbour] == distance[source] - 1)
                            {
                                answer = (ushort)neighbour;
                                break;
                            }
                        }
                    }
                    hop[source, destination] = answer;
                }
            }
            var results = new List<(ushort, ushort)>[count];
            for (int source = 0; source < count; source++)
            {
                var runs = new List<(ushort, ushort)>();
                int start = 0;
                while (start < count)
                {
                    ushort value = hop[source, start];
                    int end = start + 1;
                    while (end < count && hop[source, end] == value && end - start < UInt16.MaxValue)
                    {
                        end++;
                    }
                    runs.Add(((ushort)(end - start), value));
                    start = end;
                }
                results[source] = runs;
            }
            return results;
        }
    }
}
