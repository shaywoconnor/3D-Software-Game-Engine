## 3D Software Renderer

3D Game written in custom cpu-rendered engine with Quake-style movement.

Using Build-engine like portals with convex sectors to efficiently render 3D maps on the CPU.

Uses SDL for window creation, Input handling, and Multithreading, all game rendering is done from scratch in C using only the standard library.

I have plans to turn this into a more fleshed out game with procedural map generation, static and dynamic lighting, enemy AI and progression.

Heavily inspired by the Doom, Build, and Quake engines.

Run ./build.sh, and run with ./engine ./maps/level/level.lem
