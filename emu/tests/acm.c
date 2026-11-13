/* msacm32 PCM converter + wininet offline behaviour */
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <msacm.h>
#include <wininet.h>
#include <stdio.h>
#include <math.h>
int main(void) {
  WAVEFORMATEX src = { WAVE_FORMAT_PCM, 1, 11025, 11025, 1, 8, 0 }, dst = { 0 };
  DWORD v = acmGetVersion(); printf("ver=%lx.%lx\n", (unsigned long)(v >> 24), (unsigned long)((v >> 16) & 0xff));
  MMRESULT r = acmFormatSuggest(0, &src, &dst, sizeof dst, ACM_FORMATSUGGESTF_WFORMATTAG | ACM_FORMATSUGGESTF_NSAMPLESPERSEC | ACM_FORMATSUGGESTF_NCHANNELS);
  /* ask: PCM tag, 22050 Hz stereo, bits free */
  dst.wFormatTag = WAVE_FORMAT_PCM; dst.nSamplesPerSec = 22050; dst.nChannels = 2;
  r = acmFormatSuggest(0, &src, &dst, sizeof dst, ACM_FORMATSUGGESTF_WFORMATTAG | ACM_FORMATSUGGESTF_NSAMPLESPERSEC | ACM_FORMATSUGGESTF_NCHANNELS);
  printf("suggest r=%u tag=%u ch=%u rate=%lu bits=%u align=%u avg=%lu\n", r, dst.wFormatTag, dst.nChannels, (unsigned long)dst.nSamplesPerSec, dst.wBitsPerSample, dst.nBlockAlign, (unsigned long)dst.nAvgBytesPerSec);
  HACMSTREAM has = 0; r = acmStreamOpen(&has, 0, &src, &dst, 0, 0, 0, ACM_STREAMOPENF_NONREALTIME); printf("open r=%u has=%d\n", r, has != 0);
  DWORD need = 0; r = acmStreamSize(has, 1000, &need, ACM_STREAMSIZEF_SOURCE); printf("size r=%u need=%lu\n", r, (unsigned long)need);
  static unsigned char in[1000]; for (int i = 0; i < 1000; i++) in[i] = (unsigned char)(128 + 100 * sin(i * 0.05));
  static short out[4096]; ACMSTREAMHEADER h = { 0 }; h.cbStruct = sizeof h; h.pbSrc = in; h.cbSrcLength = 1000; h.pbDst = (BYTE *)out; h.cbDstLength = need;
  r = acmStreamPrepareHeader(has, &h, 0); printf("prep r=%u st=%lx\n", r, (unsigned long)h.fdwStatus);
  r = acmStreamConvert(has, &h, ACM_STREAMCONVERTF_START | ACM_STREAMCONVERTF_END); printf("conv r=%u used=%lu out=%lu\n", r, (unsigned long)h.cbSrcLengthUsed, (unsigned long)h.cbDstLengthUsed);
  int frames = h.cbDstLengthUsed / 4; int ok = frames >= 1998 && frames <= 2002; double err = 0; for (int i = 0; i < frames; i++) { double want = 100 * sin(i * 0.025) * 256; err += fabs(out[i * 2] - want) + fabs(out[i * 2 + 1] - want); }
  printf("frames_ok=%d avgerr=%.0f\n", ok, err / (frames * 2));
  r = acmStreamUnprepareHeader(has, &h, 0); printf("unprep r=%u close r=%u\n", r, acmStreamClose(has, 0));
  WAVEFORMATEX adpcm = { WAVE_FORMAT_ADPCM, 1, 11025, 5588, 256, 4, 0 }; r = acmStreamOpen(&has, 0, &adpcm, &dst, 0, 0, 0, 0); printf("adpcm r=%u (notpossible=%d)\n", r, r == ACMERR_NOTPOSSIBLE);
  HINTERNET hi = InternetOpenA("test", INTERNET_OPEN_TYPE_PRECONFIG, 0, 0, 0); printf("inet open=%d", hi != 0);
  DWORD flags = 0; BOOL con = InternetGetConnectedState(&flags, 0); printf(" connected=%d flags=%lx", con, (unsigned long)flags);
  HINTERNET u = InternetOpenUrlA(hi, "http://example.com/", 0, 0, 0, 0); printf(" openurl=%d err=%lu", u != 0, (unsigned long)GetLastError());
  HINTERNET cn = InternetConnectA(hi, "example.com", 80, 0, 0, INTERNET_SERVICE_HTTP, 0, 0); HINTERNET rq = HttpOpenRequestA(cn, "GET", "/", 0, 0, 0, 0, 0);
  BOOL sent = HttpSendRequestA(rq, 0, 0, 0, 0); printf(" send=%d err=%lu", sent, (unsigned long)GetLastError());
  DWORD rd = 7; char buf[16]; BOOL rf = InternetReadFile(rq, buf, 16, &rd); printf(" read=%d rd=%lu close=%d%d%d\n", rf, (unsigned long)rd, InternetCloseHandle(rq), InternetCloseHandle(cn), InternetCloseHandle(hi));
  return 0;
}
