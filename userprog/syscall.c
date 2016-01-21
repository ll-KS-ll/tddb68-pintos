#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "r" (byte));
  return error_code != -1;
}


/* Execute system call halt. */
static void
halt( void )
{
	power_off();
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  

	// Get syscall number from stack.
	int* esp = f->esp;
	int sys_nr = *esp;

	/* Debug */
	printf("System call %d\n", sys_nr);

	switch(sys_nr) {
		case SYS_HALT: // Shutdown machine
			halt();
			break;

		default: 
			printf ("Unknown system call %d\n", sys_nr);
			thread_exit ();
	}

	NOT_REACHED();
}
