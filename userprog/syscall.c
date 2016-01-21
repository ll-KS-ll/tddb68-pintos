#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "threads/vaddr.h"

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

/* Validate and return argument n. */
static uint32_t
get_argument(void* esp,  int argn)
{
	uint32_t* argv = esp + (argn + 1) * 4;
	if( get_user(argv) != -1)
		return *argv;
	thread_exit();
	NOT_REACHED();
}

/* Execute system call halt. */
static void
halt( void )
{
	power_off();
}

/* Execute system call exit. */
static void
exit( void* esp )
{
	uint32_t arg = get_argument(esp, 0); 	// Get first argument
	thread_exit ();
}

static void
write(void *esp)
{
	/* Get arguments. */
	uint32_t fd = get_argument(esp, 0);
	const void* buffer = get_argument(esp, 1);
	unsigned int size = get_argument(esp, 2);

	if(fd == 1) { // Write to console
		int n = size;
		while(n >= CONSOLE_BUFFER_SIZE) {
			putbuf(buffer, CONSOLE_BUFFER_SIZE);
			n -= CONSOLE_BUFFER_SIZE;
			buffer += CONSOLE_BUFFER_SIZE;
		}
		putbuf(buffer, n);		
		return size;
	} else {	// Write to file with fd.
		printf("Write to file not implemented yet.\n");
		return -1;
	}

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
			NOT_REACHED();
			break;

		case SYS_WRITE: // Write to file or consol.
			write(esp);
			break;

		case SYS_EXIT: // Terminate user program and returns exit status to kernel
			exit(esp);
			NOT_REACHED();
			break;

		default: 
			printf ("Unknown system call %d\n", sys_nr);
			thread_exit ();
	}

}
