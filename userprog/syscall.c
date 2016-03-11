#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"

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
  if( get_user(argv + 3) != -1 && *argv != NULL) /* Test last byte of argument. */
    return *argv;
  thread_current()->exit_status = -1;
  thread_exit();
  NOT_REACHED();
}

/* Execute system call halt. */
static void
halt( void )
{
  power_off();
}

/* Execute system call create. */
static bool
create( void *esp )
{
  /* Get arguments. */
  const char* name = get_argument(esp, 0);
  unsigned int size = get_argument(esp, 1);
  
  /* Try to create file. */
  bool status = filesys_create(name, size);

  /* Return status of file creation to user program. */
  return status;
}

/* Execute system call open. */
static int
open( void *esp )
{
  /* Create file descriptor. */
  struct thread* t = thread_current();
  int fd = bitmap_scan_and_flip(t->fd_bitmap, 0, 1, 0); 

  if (fd == BITMAP_ERROR) /* Couldn't find a free file descriptor. */
    return -1;            /* Can't open file. */

  /* Open file in filesystem. */
  const char* name = get_argument(esp, 0);
  struct file* f = filesys_open(name); 
  
  if (f == NULL) {  /* Couldn't open file with given name. */
    bitmap_reset(t->fd_bitmap, fd); /* Reset unused file descriptor. */  
    return -1;
  }

  t->files[fd] = f;
  return fd + 2; /* Fd 0 & 1 are reserved for console. Skip those.*/
}

/* Check if file descriptor is valid. */
static bool 
valid_fd(int fd)
{   
  struct thread *t = thread_current();
  return fd >= 0 && 
          fd < FD_SIZE && 
          bitmap_test(t->fd_bitmap, fd);
}

/* Execute system call write. */
static int
write(void *esp)
{
  /* Get arguments. */
  int fd = get_argument(esp, 0);
  const void* buffer = get_argument(esp, 1);
  unsigned int size = get_argument(esp, 2);

  if(fd == STDOUT_FILENO) {
    int n = size;
    
    while(n >= CONSOLE_BUFFER_SIZE) {
      putbuf(buffer, CONSOLE_BUFFER_SIZE);
      n -= CONSOLE_BUFFER_SIZE;
      buffer += CONSOLE_BUFFER_SIZE;
    }
  
    putbuf(buffer, n);    
  
    return size;
  } else {  // Write to file with fd.
    /* File descriptors 0 & 1 are reserved for console. Skip those.*/
    fd -= 2; 

    /* Control file descriptor. */
    if(!valid_fd(fd))
      return -1;

    /* Write to file with file descriptor fd. */
    struct file* f = thread_current()->files[fd];
    return file_write(f, buffer, size);
  }

}

/* Execute system call read. */
static int
read( void *esp )
{
  /* Get arguments. */
  int fd = get_argument(esp, 0);
  uint8_t* buf = get_argument(esp, 1);
  unsigned int size = get_argument(esp, 2);

  if (fd == STDIN_FILENO) {
    int n = 0;
    
    while(n < size){
      *buf = input_getc(); /* Get a single key from keyboard. */
      buf++; /* Go to next element in buffer. */
      n++;
    }
    
    return n;
  } else {
    /* File descriptors 0 & 1 are reserved for console. Skip those.*/
    fd -= 2; 

    /* Control file descriptor. */
    if(!valid_fd(fd))
      return -1;

    /* Read from file with file descriptor fd. */
    struct file* f = thread_current()->files[fd];
    return file_read(f, buf, size);
  }
}

/* Execute system call close. */
static void
close( void *esp )
{
  /* Get arguments. */
  int fd = get_argument(esp, 0);

  /* Validate file descriptor. */
  if(!valid_fd(fd)) return;

  struct thread *t = thread_current();
  struct file *f = t->files[fd];

  /* Close file. */
  file_close(f); 
  /* Clear file descriptor. */
  bitmap_reset(t->fd_bitmap, fd);
}

/* Execute system call execute. */
static int
exec( void* esp )
{
  const char* cmd_line = get_argument(esp, 0);
  int pid = process_execute(cmd_line);
  return pid;
}

static int
wait( void* esp)
{
  int pid = get_argument(esp, 0);
  int exit_status = process_wait(pid);
  return exit_status;
}


/* Execute system call exit. */
static void
exit( void* esp )
{
  int status = get_argument(esp, 0);
  /* Set exit code. */
  thread_current()->exit_status = status;
  /* Exit process. */
  thread_exit();
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /* Get syscall number from stack. */
  int* esp = f->esp;
  
  /* Check so pointers aren't in kernel memory. */
  if(!is_user_vaddr(esp) || !is_user_vaddr(esp + 1) || !is_user_vaddr(esp + 2) || !is_user_vaddr(esp + 3)) {
    /* Exit process. */
    thread_current()->exit_status = -1;
    thread_exit();
  }
  
  if(get_user(esp) == -1 || get_user(esp + 1) == -1 || get_user(esp + 2) == -1 || get_user(esp + 3) == -1){
    /* Exit process. */
    thread_current()->exit_status = -1;
    thread_exit();
  }
    
  if(*esp < SYS_HALT || *esp > SYS_CLOSE) {
    /* Exit process. */
    thread_current()->exit_status = -1;
    thread_exit();
  }
  
  int sys_nr = *esp;

  switch(sys_nr) {
    case SYS_HALT: // Shutdown machine
      halt();
      NOT_REACHED();
      break;

    case SYS_CREATE: // Create a file with specified size.
      f->eax = create(esp);
      break;

    case SYS_OPEN: // If possible open file.
      f->eax = open(esp); 
      break;

    case SYS_WRITE: // Write to file or consol.
      f->eax = write(esp);
      break;

    case SYS_READ: // Read from file or console.
      f->eax = read(esp);
      break;

    case SYS_CLOSE: // Close file.
      close(esp);
      break;

    case SYS_EXEC: // Start a process
      f->eax = exec(esp);
      break;

    case SYS_WAIT:
      f->eax = wait(esp);
      break;

    case SYS_EXIT: // Exit process.
      exit(esp);
      break;

    default: 
      printf ("Unknown system call %d\n", sys_nr);
      thread_exit ();
  }

}
