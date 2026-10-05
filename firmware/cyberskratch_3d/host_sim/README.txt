Desktop preview of the VJ view. Build (Git Bash, here):
  cp ../cyberskratch_3d.ino game.inc; cp ../Vocab_* .; g++ -O1 -std=gnu++17 -I. -o cs.exe main.cpp Vocab_*.cpp; mkdir -p out; ./cs.exe
Runs the real audio engine; the I2S hook advances time and draws a frame every 50 ms. SHOTS[] in main.cpp = one frame per mode / FX.
