global using Color4 = OpenTK.Mathematics.Color4<OpenTK.Mathematics.Rgba>;
global using Matrix4 = System.Numerics.Matrix4x4;
global using Matrix4x3 = OpenTK.Mathematics.Matrix4x3;
global using Vector2i = OpenTK.Mathematics.Vector2i;
global using Vector3 = System.Numerics.Vector3;
global using Vector3i = OpenTK.Mathematics.Vector3i;
global using Vector4 = System.Numerics.Vector4;
global using Vector4i = OpenTK.Mathematics.Vector4i;

using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace MphRead
{
    public static class Numerics
    {
        extension(Vector3 vector)
        {
            public Vector3 Normalized()
            {
                return Vector3.Normalize(vector);
            }
        }

        extension(Vector4 vector)
        {
            public Vector3 Xyz
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => new Vector3(vector.X, vector.Y, vector.Z);
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    vector.X = value.X;
                    vector.Y = value.Y;
                    vector.Z = value.Z;
                }
            }

            public Vector4 Normalized()
            {
                return Vector4.Normalize(vector);
            }
        }

        extension(Matrix4 matrix)
        {
            public static Matrix4 Create(Vector4 row0, Vector4 row1, Vector4 row2, Vector4 row3)
            {
                return new Matrix4(
                    row0.X, row0.Y, row0.Z, row0.W,
                    row1.X, row1.Y, row1.Z, row1.W,
                    row2.X, row2.Y, row2.Z, row2.W,
                    row3.X, row3.Y, row3.Z, row3.W
                );
            }

            public static Matrix4 Create(Matrix3 other)
            {
                return new Matrix4(
                    other.Row0.X, other.Row0.Y, other.Row0.Z, 0,
                    other.Row1.X, other.Row1.Y, other.Row1.Z, 0,
                    other.Row2.X, other.Row2.Y, other.Row2.Z, 0,
                    0, 0, 0, 1
                );
            }

            public Matrix4 Inverted()
            {
                if (!Matrix4.Invert(matrix, out Matrix4 result))
                {
                    throw new InvalidOperationException("Matrix cannot be inverted.");
                }
                return result;
            }

            public Matrix4 ClearTranslation()
            {
                Matrix4 copy = matrix;
                copy.Row3_Xyz = Vector3.Zero;
                return copy;
            }

            public Matrix4 ClearScale()
            {
                Matrix4 copy = matrix;
                copy.Row0_Xyz = copy.Row0.Xyz.Normalized();
                copy.Row1_Xyz = copy.Row1.Xyz.Normalized();
                copy.Row2_Xyz = copy.Row2.Xyz.Normalized();
                return copy;
            }

            public Matrix4 ClearRotation()
            {
                Matrix4 copy = matrix;
                copy.Row0_Xyz = new Vector3(copy.Row0.Xyz.Length(), 0, 0);
                copy.Row1_Xyz = new Vector3(0, copy.Row1.Xyz.Length(), 0);
                copy.Row2_Xyz = new Vector3(0, 0, copy.Row2.Xyz.Length());
                return copy;
            }

            public Vector3 ExtractTranslation()
            {
                return matrix.Row3.Xyz;
            }

            public Vector3 ExtractScale()
            {
                return new Vector3(matrix.Row0.Xyz.Length(), matrix.Row1.Xyz.Length(), matrix.Row2.Xyz.Length());
            }

            public void ToEulerAngles(out Vector3 angles)
            {
                ((OpenTK.Mathematics.Matrix4)matrix).ExtractRotation().ToEulerAngles(out OpenTK.Mathematics.Vector3 anglesTk);
                angles = (Vector3)anglesTk;
            }

            public Vector3 ToEulerAngles()
            {
                return (Vector3)((OpenTK.Mathematics.Matrix4)matrix).ExtractRotation().ToEulerAngles();
            }

            public Vector4 Row0
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix[0];
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set => matrix[0] = value;
            }

            public Vector4 Row1
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix[1];
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set => matrix[1] = value;
            }

            public Vector4 Row2
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix[2];
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set => matrix[2] = value;
            }

            public Vector4 Row3
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix[3];
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set => matrix[3] = value;
            }

            public float Row0_X
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M11;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M11 = value;
                }
            }

            public float Row0_Y
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M12;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M12 = value;
                }
            }

            public float Row0_Z
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M13;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M13 = value;
                }
            }

            public float Row0_W
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M14;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M14 = value;
                }
            }

            public float Row1_X
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M21;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M21 = value;
                }
            }

            public float Row1_Y
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M22;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M22 = value;
                }
            }

            public float Row1_Z
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M23;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M23 = value;
                }
            }

            public float Row1_W
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M24;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M24 = value;
                }
            }

            public float Row2_X
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M31;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M31 = value;
                }
            }

            public float Row2_Y
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M32;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M32 = value;
                }
            }

            public float Row2_Z
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M33;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M33 = value;
                }
            }

            public float Row2_W
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M34;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M34 = value;
                }
            }

            public float Row3_X
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M41;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M41 = value;
                }
            }

            public float Row3_Y
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M42;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M42 = value;
                }
            }

            public float Row3_Z
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M43;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M43 = value;
                }
            }

            public float Row3_W
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix.M44;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M44 = value;
                }
            }

            public Vector3 Row0_Xyz
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix[0].Xyz;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M11 = value.X;
                    matrix.M12 = value.Y;
                    matrix.M13 = value.Z;
                }
            }

            public Vector3 Row1_Xyz
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix[1].Xyz;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M21 = value.X;
                    matrix.M22 = value.Y;
                    matrix.M23 = value.Z;
                }
            }

            public Vector3 Row2_Xyz
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix[2].Xyz;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M31 = value.X;
                    matrix.M32 = value.Y;
                    matrix.M33 = value.Z;
                }
            }

            public Vector3 Row3_Xyz
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get => matrix[3].Xyz;
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                set
                {
                    matrix.M41 = value.X;
                    matrix.M42 = value.Y;
                    matrix.M43 = value.Z;
                }
            }
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Matrix3 : IEquatable<Matrix3>
    {
        private Matrix4 _matrix;

        public static readonly Matrix3 Identity = new Matrix3(Matrix4.Identity);

        public Matrix3(Matrix4 matrix)
        {
            _matrix = new Matrix4(
                matrix.M11, matrix.M12, matrix.M13, 0,
                matrix.M21, matrix.M22, matrix.M23, 0,
                matrix.M31, matrix.M32, matrix.M33, 0,
                0, 0, 0, 1
            );
            M11 = _matrix.M11;
            M12 = _matrix.M12;
            M13 = _matrix.M13;
            M21 = _matrix.M21;
            M22 = _matrix.M22;
            M23 = _matrix.M23;
            M31 = _matrix.M31;
            M32 = _matrix.M32;
            M33 = _matrix.M33;
        }

        public Matrix3(float m11, float m12, float m13, float m21, float m22, float m23, float m31, float m32, float m33)
        {
            _matrix = new Matrix4(
                m11, m12, m13, 0,
                m21, m22, m23, 0,
                m31, m32, m33, 0,
                0, 0, 0, 1
            );
            M11 = _matrix.M11;
            M12 = _matrix.M12;
            M13 = _matrix.M13;
            M21 = _matrix.M21;
            M22 = _matrix.M22;
            M23 = _matrix.M23;
            M31 = _matrix.M31;
            M32 = _matrix.M32;
            M33 = _matrix.M33;
        }

        public Matrix3(OpenTK.Mathematics.Vector3 row0, OpenTK.Mathematics.Vector3 row1, OpenTK.Mathematics.Vector3 row2)
        {
            _matrix = new Matrix4(
                row0.X, row0.Y, row0.Z, 0,
                row1.X, row1.Y, row1.Z, 0,
                row2.X, row2.Y, row2.Z, 0,
                0, 0, 0, 1
            );
            M11 = _matrix.M11;
            M12 = _matrix.M12;
            M13 = _matrix.M13;
            M21 = _matrix.M21;
            M22 = _matrix.M22;
            M23 = _matrix.M23;
            M31 = _matrix.M31;
            M32 = _matrix.M32;
            M33 = _matrix.M33;
        }

        public Matrix3(Vector3 row0, Vector3 row1, Vector3 row2)
        {
            _matrix = new Matrix4(
                row0.X, row0.Y, row0.Z, 0,
                row1.X, row1.Y, row1.Z, 0,
                row2.X, row2.Y, row2.Z, 0,
                0, 0, 0, 1
            );
            M11 = _matrix.M11;
            M12 = _matrix.M12;
            M13 = _matrix.M13;
            M21 = _matrix.M21;
            M22 = _matrix.M22;
            M23 = _matrix.M23;
            M31 = _matrix.M31;
            M32 = _matrix.M32;
            M33 = _matrix.M33;
        }

        public readonly float M11;
        public readonly float M12;
        public readonly float M13;
        public readonly float M21;
        public readonly float M22;
        public readonly float M23;
        public readonly float M31;
        public readonly float M32;
        public readonly float M33;

        public readonly Matrix3 Inverted()
        {
            OpenTK.Mathematics.Matrix3 matrix = new OpenTK.Mathematics.Matrix3((OpenTK.Mathematics.Matrix4)_matrix).Inverted();
            return new Matrix3(
                matrix.Row0,
                matrix.Row1,
                matrix.Row2
            );
        }

        public static Matrix3 CreateFromAxisAngle(Vector3 axis, float angle)
        {
            OpenTK.Mathematics.Matrix3.CreateFromAxisAngle((OpenTK.Mathematics.Vector3)axis, angle, out OpenTK.Mathematics.Matrix3 matrix);
            return new Matrix3(
                matrix.Row0,
                matrix.Row1,
                matrix.Row2
            );
        }

        public static Matrix3 CreateRotationY(float angle)
        {
            OpenTK.Mathematics.Matrix3.CreateRotationY(angle, out OpenTK.Mathematics.Matrix3 matrix);
            return new Matrix3(
                matrix.Row0,
                matrix.Row1,
                matrix.Row2
            );
        }

        public Vector3 Row0
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            readonly get => _matrix[0].Xyz;
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            set => _matrix[0] = new Vector4(value, 0);
        }

        public Vector3 Row1
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            readonly get => _matrix[1].Xyz;
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            set => _matrix[1] = new Vector4(value, 0);
        }

        public Vector3 Row2
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            readonly get => _matrix[2].Xyz;
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            set => _matrix[2] = new Vector4(value, 0);
        }

        public static Vector3 operator *(Vector3 vector, Matrix3 matrix)
        {
            return Vector4.Transform(new Vector4(vector, 1), matrix._matrix).Xyz;
        }

        public readonly bool Equals(Matrix3 other)
        {
            return _matrix.Equals(other._matrix);
        }

        public readonly override bool Equals(object? obj)
        {
            return obj is Matrix3 matrix && Equals(matrix);
        }

        public static bool operator ==(Matrix3 left, Matrix3 right)
        {
            return left.Equals(right);
        }

        public static bool operator !=(Matrix3 left, Matrix3 right)
        {
            return !(left == right);
        }

        public readonly override int GetHashCode()
        {
            return HashCode.Combine(_matrix);
        }
    }
}
