﻿//Copyright © 2014 Sony Computer Entertainment America LLC. See License.txt.

using System;
using System.Runtime.InteropServices;

using Vector2 = Sce.Atf.VectorMath.Vec2F;
using Vector3 = Sce.Atf.VectorMath.Vec3F;
using Vector4 = Sce.Atf.VectorMath.Vec4F;
namespace RenderingInterop
{
    public struct VertexPN
    {
        public VertexPN(float x, float y, float z, float nx, float ny, float nz)
        {
            Position = new Vector3(x, y, z);
            Normal = new Vector3(nx, ny, nz);
        }
        public Vector3 Position;
        public Vector3 Normal;
    }


    public struct VertexPNT
    {
        public VertexPNT(float x, float y, float z, float nx, float ny, float nz, float u, float v)
        {
            Position = new Vector3(x, y, z);
            Normal = new Vector3(nx, ny, nz);
            Tex = new Vector2(u, v);
        }
        public Vector3 Position;
        public Vector3 Normal;
        public Vector2 Tex;
    }

    public struct VertexPC
    {
        public VertexPC(float x, float y, float z, uint col)
        {
            Position = new Vector3(x, y, z);
            Color = col;
        }
        public Vector3 Position;
        public uint Color;
    }

}
