/* Exercise the MSVC internal SSE calling convention, not the x86 cdecl/x87 convention. */
.text
#ifdef __x86_64__
.globl sse_binary_call
sse_binary_call:
  subq $56, %rsp
  movq %r9, 40(%rsp)
  movapd %xmm1, %xmm0
  movapd %xmm2, %xmm1
  call *%rcx
  movq 40(%rsp), %rax
  movsd %xmm0, (%rax)
  addq $56, %rsp
  ret
.globl sse_log_call
sse_log_call:
  subq $56, %rsp
  movq %r8, 40(%rsp)
  movapd %xmm1, %xmm0
  call *%rcx
  movq 40(%rsp), %rax
  movsd %xmm0, (%rax)
  addq $56, %rsp
  ret
#else
.globl _sse_binary_call
_sse_binary_call:
  movsd 8(%esp), %xmm0
  movsd 16(%esp), %xmm1
  call *4(%esp)
  movl 24(%esp), %eax
  movsd %xmm0, (%eax)
  ret
.globl _sse_log_call
_sse_log_call:
  movsd 8(%esp), %xmm0
  call *4(%esp)
  movl 16(%esp), %eax
  movsd %xmm0, (%eax)
  ret
#endif
