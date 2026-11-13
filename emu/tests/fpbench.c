#include <stdio.h>
#include <math.h>
/* float-heavy loop in the shape of a 2D game update: positions, velocities, gravity, bounces, a sqrt distance test */
int main(int argc, char **argv) {
  enum { N = 512 }; static double px[N], py[N], vx[N], vy[N]; int hits = 0;
  for (int i = 0; i < N; i++) { px[i] = i * 1.5; py[i] = (i % 17) * 3.25; vx[i] = (i % 5) - 2.0; vy[i] = 0; }
  int frames = argc > 1 ? 400 : 1500;
  for (int f = 0; f < frames; f++) {
    for (int i = 0; i < N; i++) {
      vy[i] += 9.81 * (1.0 / 60.0); px[i] += vx[i] * (1.0 / 60.0); py[i] += vy[i] * (1.0 / 60.0);
      if (py[i] > 480.0) { py[i] = 480.0; vy[i] = -vy[i] * 0.8; hits++; }
      if (px[i] < 0.0 || px[i] > 640.0) vx[i] = -vx[i];
      double dx = px[i] - 320.0, dy = py[i] - 240.0, d = sqrt(dx * dx + dy * dy);
      if (d < 50.0) hits++;
      float fx = (float)px[i] * 0.5f + 1.0f; if (fx > 1e6f) hits++;
    }
  }
  double s = 0; for (int i = 0; i < N; i++) s += px[i] + py[i];
  printf("fp hits=%d sum=%.3f\n", hits, s); return 0;
}
