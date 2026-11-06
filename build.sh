#!/bin/sh
cc -std=c99 -O3 -march=native -g -o engine src/game.c src/app.c src/platform.c src/render_pool.c src/input.c src/stats.c src/engine.c src/e_map.c $(pkg-config --cflags --libs sdl3) -lm
