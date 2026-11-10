/* Return 0 only if the COM callee popped this, hwnd and priority (12 bytes). */
.text
.globl _check_cooperative_stack
_check_cooperative_stack:
  pushl %ebx
  movl %esp, %ebx
  movl 8(%esp), %eax
  movl (%eax), %edx
  pushl $2
  pushl $0
  pushl %eax
  call *24(%edx)
  cmpl %ebx, %esp
  movl %ebx, %esp
  setne %al
  movzbl %al, %eax
  popl %ebx
  ret
