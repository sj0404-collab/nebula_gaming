/* winmm mmio: create a RIFF/WAVE file chunk by chunk, read it back with FINDRIFF/FINDCHUNK, parse a memory stream, mmioGetInfo */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  MMCKINFO riff, fmt, data; memset(&riff, 0, sizeof riff); memset(&fmt, 0, sizeof fmt); memset(&data, 0, sizeof data);
  HMMIO h = mmioOpenA("C:\\tmp\\t.wav", 0, MMIO_CREATE | MMIO_WRITE);
  if (!h) { printf("create failed\n"); return 1; }
  riff.fccType = mmioFOURCC('W', 'A', 'V', 'E'); mmioCreateChunk(h, &riff, MMIO_CREATERIFF);
  fmt.ckid = mmioFOURCC('f', 'm', 't', ' '); mmioCreateChunk(h, &fmt, 0);
  WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 1, 22050, 44100, 2, 16, 0 }; mmioWrite(h, (char *)&wf, 16); mmioAscend(h, &fmt, 0);
  data.ckid = mmioFOURCC('d', 'a', 't', 'a'); mmioCreateChunk(h, &data, 0);
  short pcm[101]; for (int i = 0; i < 101; i++) pcm[i] = (short)(i * 300); mmioWrite(h, (char *)pcm, sizeof pcm);   /* odd byte count → pad */
  mmioAscend(h, &data, 0); mmioAscend(h, &riff, 0); mmioClose(h, 0);
  printf("written: fmt=%lu data=%lu riff=%lu\n", (unsigned long)fmt.cksize, (unsigned long)data.cksize, (unsigned long)riff.cksize);
  /* read back */
  h = mmioOpenA("C:\\tmp\\t.wav", 0, MMIO_READ); if (!h) { printf("open failed\n"); return 1; }
  MMCKINFO r2, c2; memset(&r2, 0, sizeof r2); r2.fccType = mmioFOURCC('W', 'A', 'V', 'E');
  int ok = mmioDescend(h, &r2, 0, MMIO_FINDRIFF) == 0;
  memset(&c2, 0, sizeof c2); c2.ckid = mmioFOURCC('d', 'a', 't', 'a'); ok = ok && mmioDescend(h, &c2, &r2, MMIO_FINDCHUNK) == 0;
  short back[101]; memset(back, 0, sizeof back); LONG got = mmioRead(h, (char *)back, c2.cksize); mmioAscend(h, &c2, 0);
  MMCKINFO c3; memset(&c3, 0, sizeof c3); c3.ckid = mmioFOURCC('f', 'm', 't', ' '); int missing_after = mmioDescend(h, &c3, &r2, MMIO_FINDCHUNK);   /* fmt is before data: not found from here */
  mmioSeek(h, 12, SEEK_SET); memset(&c3, 0, sizeof c3); c3.ckid = mmioFOURCC('f', 'm', 't', ' '); int found_fmt = mmioDescend(h, &c3, &r2, MMIO_FINDCHUNK); WAVEFORMATEX wb; memset(&wb, 0, sizeof wb); mmioRead(h, (char *)&wb, 16);
  mmioClose(h, 0);
  printf("read: riff_ok=%d data_ok=%d size=%lu got=%ld pcm_ok=%d fmt_after_data=%d fmt_ok=%d rate=%lu\n", ok, c2.cksize == sizeof pcm, (unsigned long)c2.cksize, (long)got, !memcmp(back, pcm, sizeof pcm), missing_after, found_fmt == 0, (unsigned long)wb.nSamplesPerSec);
  /* memory stream: the same file image through FOURCC_MEM, chunk walk without flags, mmioGetInfo buffer pointers */
  HANDLE f = CreateFileA("C:\\tmp\\t.wav", GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0); DWORD n = GetFileSize(f, 0), rd = 0; char *img = (char *)malloc(n); ReadFile(f, img, n, &rd, 0); CloseHandle(f);
  MMIOINFO mi; memset(&mi, 0, sizeof mi); mi.fccIOProc = FOURCC_MEM; mi.pchBuffer = img; mi.cchBuffer = n;
  HMMIO m = mmioOpenA(0, &mi, MMIO_READ); MMCKINFO a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
  int w1 = mmioDescend(m, &a, 0, 0); int w2 = mmioDescend(m, &b, &a, 0);   /* first chunk is RIFF, next inside is fmt */
  MMIOINFO gi; memset(&gi, 0, sizeof gi); mmioGetInfo(m, &gi, 0);
  printf("mem: first=%.4s type=%.4s next=%.4s getinfo_ok=%d close=%d str=%08lx\n", (char *)&a.ckid, (char *)&a.fccType, (char *)&b.ckid, w1 == 0 && w2 == 0 && gi.pchBuffer == img && gi.pchNext == img + 20 && gi.pchEndRead == img + n, mmioClose(m, 0), (unsigned long)mmioStringToFOURCCA("wav", MMIO_TOUPPER));
  return 0;
}
